local fio = require('fio')
local instance_config = require('internal.config.instance_config')
local cluster_config = require('internal.config.cluster_config')
local configdata = require('internal.config.configdata')
local aboard = require('internal.config.utils.aboard')
local tarantool = require('tarantool')

-- Tarantool Enterprise Edition has its own additions
-- for config module.
--
-- Tarantool Community Edition may be extended using the same
-- mechanism.
local function load_extras()
    local has_extras, extras = pcall(require, 'internal.config.extras')

    -- The module is built into Tarantool EE executable. If it
    -- can't be loaded, something is definitely going wrong.
    if not has_extras and tarantool.package == 'Tarantool Enterprise' then
        error('Tarantool Enterprise Edition build seems broken: built-in ' ..
            'module internal.config.extras is not found')
    end

    -- The module may be provided by a user for Tarantool CE, but
    -- it is optional.
    if not has_extras then
        return
    end

    -- Verify the contract.
    assert(type(extras) == 'table')
    assert(type(extras.initialize) == 'function')
    assert(type(extras.post_apply) == 'function')

    return extras
end

local extras = load_extras()

-- {{{ Helpers

-- Remove indent from a text.
--
-- Similar to Python's textwrap.dedent().
--
-- It strips all newlines from beginning and all whitespace
-- characters from the end for convenience use with multiline
-- string literals ([[ <...> ]]).
local function dedent(s)
    local lines = s:lstrip('\n'):rstrip():split('\n')

    local indent = math.huge
    for _, line in ipairs(lines) do
        if #line ~= 0 then
            indent = math.min(indent, #line:match('^ *'))
        end
    end

    local res = {}
    for _, line in ipairs(lines) do
        table.insert(res, line:sub(indent + 1))
    end
    return table.concat(res, '\n')
end

-- Extract all fields from a table except ones that start from
-- the underscore.
--
-- Useful for __serialize.
local function filter_out_private_fields(t)
    local res = {}
    for k, v in pairs(t) do
        if not k:startswith('_') then
            res[k] = v
        end
    end
    return res
end

-- }}} Helpers

local methods = {}
local mt = {
    __index = methods,
    __serialize = filter_out_private_fields,
}

local function selfcheck(self, method_name)
    if type(self) ~= 'table' or getmetatable(self) ~= mt then
        local fmt_str = 'Use config:%s(<...>) instead of config.%s(<...>)'
        error(fmt_str:format(method_name, method_name), 0)
    end
end

local function initcheck(self, method_name, cluster_or_instance)
    if self._status ~= 'uninitialized' then
        return
    end

    assert(self._configdata_applied == nil)
    local fmt_str = 'config:%s(): no %s config available yet'
    error(fmt_str:format(method_name, cluster_or_instance), 0)
end

local function broadcast(self)
    box.broadcast('config.info', self:info())
end

function methods._meta(self, source_name, key, value)
    local data = self._metadata[source_name] or {}
    data[key] = value
    self._metadata[source_name] = data
end

function methods._register_source(self, source)
    assert(type(source) == 'table')
    assert(source.name ~= nil)
    assert(source.type == 'instance' or source.type == 'cluster')
    assert(source.sync ~= nil)
    assert(source.get ~= nil)
    table.insert(self._sources, source)
end

function methods._register_applier(self, applier)
    assert(type(applier) == 'table')
    assert(applier.name ~= nil)
    assert(applier.apply ~= nil)
    table.insert(self._appliers, applier)
end

function methods._initialize(self)
    -- The sources are synchronized in the order of registration:
    -- env, file, etcd (present in Tarantool EE), env for
    -- defaults.
    --
    -- The configuration values from the first source has highest
    -- priority. The menthal rule here is the following: values
    -- closer to the process are preferred: env first, then file,
    -- then etcd (if available). And only then the env source with
    -- defaults.
    self:_register_source(require('internal.config.source.env').new())

    if self._config_file ~= nil then
        self:_register_source(require('internal.config.source.file').new())
    end

    self:_register_applier(require('internal.config.applier.lua'))
    self:_register_applier(require('internal.config.applier.compat'))
    self:_register_applier(require('internal.config.applier.mkdir'))
    self:_register_applier(require('internal.config.applier.console'))
    self:_register_applier(require('internal.config.applier.runtime_priv'))
    self:_register_applier(require('internal.config.applier.box_cfg'))
    self:_register_applier(require('internal.config.applier.box_status'))
    self:_register_applier(require('internal.config.applier.credentials'))
    self:_register_applier(require('internal.config.applier.fiber'))
    self:_register_applier(require('internal.config.applier.sharding'))
    self:_register_applier(require('internal.config.applier.roles'))
    self:_register_applier(require('internal.config.applier.app'))

    if extras ~= nil then
        extras.initialize(self)
    end

    self:_register_source(require('internal.config.source.env').new({
        env_var_suffix = 'default',
    }))
end

-- Collect data from configuration sources.
--
-- Return instance config, cluster config and sources information.
--
-- This method doesn't store anything in the config object.
--
-- It can be used with self._instance_name == nil to obtain a
-- cluster configuration. All the instance_name related checks are
-- in the self:_store() method.
function methods._collect(self, opts)
    assert(type(opts) == 'table')
    local sync_source = opts.sync_source

    local iconfig = {}
    local cconfig = {}

    -- For error reporting.
    local source_info = {}

    for _, source in ipairs(self._sources) do
        -- Gather config values.
        --
        -- The configdata object is not constructed yet, so we
        -- pass currently collected instance config as the second
        -- argument. The 'config' section of the config may
        -- contain a configuration needed for a source.
        if sync_source == source.name or sync_source == 'all' then
            source:sync(self, iconfig)
        end

        -- Validate configurations gathered from the sources.
        if source.type == 'instance' then
            instance_config:validate(source:get())
        elseif source.type == 'cluster' then
            cluster_config:validate(source:get())
        else
            assert(false)
        end

        -- Merge configurations from the sources.
        --
        -- Instantiate a cluster config to an instance config for
        -- cluster config sources.
        --
        -- The configuration values from a first source has highest
        -- priority. We should keep already gathered values in the
        -- accumulator and fill only missed ones from next sources.
        --
        -- :merge() prefers values from the second argument, so the
        -- accumulator is passed as the second.
        local source_iconfig
        if source.type == 'cluster' then
            local source_cconfig = source:get()

            -- Extract and merge conditional sections into the
            -- data from the source.
            --
            -- It is important to call :apply_conditional() before
            -- :merge() for each cluster config.
            --
            -- The 'conditional' field is an array and if several
            -- sources have the field, the last one replaces all
            -- the previous ones. If we merge all the configs and
            -- then call :apply_conditional(), we loss all the
            -- conditional sections except the last one.
            --
            -- The same is applicable for config sources that
            -- construct a config from several separately stored
            -- parts using :merge(). They should call
            -- :apply_conditional() for each of the parts before
            -- :merge().
            source_cconfig = cluster_config:apply_conditional(source_cconfig)

            cconfig = cluster_config:merge(source_cconfig, cconfig)
            source_iconfig = cluster_config:instantiate(cconfig,
                self._instance_name)
        elseif source.type == 'instance' then
            source_iconfig = source:get()
        else
            assert(false)
        end
        iconfig = instance_config:merge(source_iconfig, iconfig)

        -- If a source returns an empty table, mark it as ones
        -- that provide no data.
        local has_data = next(source:get()) ~= nil
        table.insert(source_info, ('* %q [type: %s]%s'):format(source.name,
            source.type, has_data and '' or ' (no data)'))
    end

    return iconfig, cconfig, source_info
end

-- Store the given configuration in the config object.
--
-- The method accepts collected instance config, cluster config
-- and configuration source information and based on the given
-- values performs several checks and stores the configuration
-- within the config object as a configdata object.
function methods._store(self, iconfig, cconfig, source_info)
    if next(cconfig) == nil then
        local is_reload = self._status == 'reload_in_progress'
        local action = is_reload and 'Reload' or 'Startup'
        local source_info_str = table.concat(source_info, '\n')
        error(dedent([[
            %s failure.

            No cluster config received from the given configuration sources.

            %s

            The %q instance cannot find itself in the group/replicaset/instance
            topology and it is unknown, whether it should join a replicaset or
            create its own database.

            Recipes:

            * Use --config <file> command line option.
            * Use TT_CONFIG_ETCD_* environment variables (available on Tarantool
              Enterprise Edition).
        ]]):format(action, source_info_str, self._instance_name), 0)
    end

    if cluster_config:find_instance(cconfig, self._instance_name) == nil then
        local is_reload = self._status == 'reload_in_progress'
        local action = is_reload and 'Reload' or 'Startup'
        local source_info_str = table.concat(source_info, '\n')
        error(dedent([[
            %s failure.

            Unable to find instance %q in the group/replicaset/instance
            topology provided by the given cluster configuration sources.

            %s

            It is unknown, whether the instance should join a replicaset or
            create its own database.

            Minimal cluster config:

            groups:
              group-001:
                replicasets:
                  replicaset-001:
                    instances:
                      instance-001: {}
        ]]):format(action, self._instance_name, source_info_str), 0)
    end

    self._configdata = configdata.new(iconfig, cconfig, self._instance_name)
end

-- Invoke the appliers depending on the phase. The first phase
-- might be long due to the long recovery process.
function methods._apply_on_startup(self, opts)
    local first_phase_appliers = {
        lua = true,
        compat = true,
        mkdir = true,
        console = true,
        runtime_priv = true,
        box_cfg = true,
    }

    assert(type(opts) == 'table')
    local phase = opts.phase

    if phase == 1 then
        local needs_retry = false
        for _, applier in ipairs(self._appliers) do
            if first_phase_appliers[applier.name] then
                local res = applier.apply(self)
                if res ~= nil and res.needs_retry then
                    needs_retry = true
                end
            end
        end
        return needs_retry
    elseif phase == 2 then
        for _, applier in ipairs(self._appliers) do
            if not first_phase_appliers[applier.name] then
                applier.apply(self)
            end
        end

        self._configdata_applied = self._configdata
    else
        assert(false)
    end
end

function methods._apply(self)
    for _, applier in ipairs(self._appliers) do
        applier.apply(self)
    end

    self._configdata_applied = self._configdata
end

function methods._post_apply(self)
    for _, applier in ipairs(self._appliers) do
        if applier.post_apply ~= nil then
            applier.post_apply(self)
        end
    end
end

-- Set proper status depending on received alerts.
function methods._set_status_based_on_alerts(self)
    self._status = self._aboard:status()
    broadcast(self)
end

function methods._startup(self, instance_name, config_file)
    assert(self._status == 'uninitialized')

    local ok, err = cluster_config:validate_name(instance_name)
    if not ok then
        error(('[--name] %s'):format(err), 0)
    end

    self._status = 'startup_in_progress'
    self._instance_name = instance_name
    -- box.cfg() changes a current working directory that
    -- invalidates relative paths.
    --
    -- Let's calculate an absolute path to access the file later
    -- on :reload().
    if config_file ~= nil then
        self._config_file = fio.abspath(config_file)
    end
    broadcast(self)

    self:_initialize()

    -- Startup phase 1/2.
    --
    -- Start first-phase appliers. The box_cfg applier may force
    -- the read-only mode on this phase.
    --
    -- This phase may take a long time due to recovery.
    self:_store(self:_collect({sync_source = 'all'}))
    local needs_retry = self:_apply_on_startup({phase = 1})

    -- Startup phase 2/2.
    --
    -- If the previous phase is considered as potentially taking a
    -- long time, re-read the configuration and apply the fresh
    -- one.
    --
    -- Otherwise finish applying the existing configuration.
    if needs_retry then
        self:_store(self:_collect({sync_source = 'all'}))
        self:_apply()
    else
        self:_apply_on_startup({phase = 2})
    end

    self:_post_apply()
    self:_set_status_based_on_alerts()
    if self._status == 'ready' or self._status == 'check_warnings' then
        self._active_metadata = self._metadata
    end
    if extras ~= nil then
        extras.post_apply(self)
    end
end

function methods._print_env_list(self)
    local env_source = require('internal.config.source.env').new()
    io.stdout:write(env_source:_env_list())
end

-- Note: There is a difference between missing opts.instance and
-- opts.instance that is equal to the given instance name. The
-- former returns an instance configuration taking into account
-- instance configuration sources (environment variables). The
-- latter takes into account only cluster configuration, so the
-- environment variables are ignored.
function methods.get(self, path, opts)
    selfcheck(self, 'get')
    initcheck(self, 'get', 'instance')

    opts = opts or {}

    return self._configdata_applied:get(path, {
        instance = opts.instance,
        use_default = true,
    })
end

function methods._reload_noexc(self, opts)
    assert(type(opts) == 'table')
    if self._status == 'uninitialized' then
        return false, 'config:reload(): no instance config available yet'
    end
    if self._status == 'startup_in_progress' or
       self._status == 'reload_in_progress' then
        return false, 'config:reload(): instance configuration is already in '..
                      'progress'
    end
    self._status = 'reload_in_progress'
    broadcast(self)

    self._aboard:clean()
    self._metadata = {}

    local ok, err = pcall(function(opts)
        self:_store(self:_collect(opts))
        self:_apply()
        self:_post_apply()
    end, opts)

    assert(not ok or err == nil)
    if not ok then
        self._aboard:set({type = 'error', message = err})
    end

    self:_set_status_based_on_alerts()
    if self._status == 'ready' or self._status == 'check_warnings' then
        self._active_metadata = self._metadata
    end
    if extras ~= nil then
        extras.post_apply(self)
    end

    return ok, err
end

function methods.reload(self)
    selfcheck(self, 'reload')
    local ok, err = self:_reload_noexc({sync_source = 'all'})
    if not ok then
        error(err, 0)
    end
end

function methods._hierarchy_info(self)
    if self._configdata_applied == nil then
        return {}
    end

    local names = self._configdata_applied:names()
    return {
        group = names.group_name,
        replicaset = names.replicaset_name,
        instance = names.instance_name,
    }
end

function methods.info(self, version)
    selfcheck(self, 'info')
    version = version == nil and 'v1' or version
    local info = {}
    local alerts = self._aboard:alerts()
    local hierarchy = self:_hierarchy_info()
    info['v1'] = {
        alerts = alerts,
        meta = self._metadata,
        status = self._status,
        hierarchy = hierarchy,
    }
    info['v2'] = {
        alerts = alerts,
        meta = {
            active = self._active_metadata,
            last = self._metadata,
        },
        status = self._status,
        hierarchy = hierarchy,
    }
    if info[version] ~= nil then
        return info[version]
    end

    local supported_versions = {}
    for key in pairs(info) do
        table.insert(supported_versions, key)
    end
    table.sort(supported_versions)

    error(('config:info() expects %s or nil as an argument, got %q'):format(
        table.concat(supported_versions, ', '), version), 0)
end

-- Cluster configuration (internal method).
--
-- It is given as is after merging from all the configuration
-- sources. Default values are NOT applied. Variables are NOT
-- substituted.
--
-- Use config:get() to receive an instance config for a particular
-- instance with applied defaults and substituted variables.
function methods._cconfig(self)
    selfcheck(self, '_cconfig')
    initcheck(self, '_cconfig', 'cluster')

    return self._configdata_applied:cconfig()
end

-- List all instances of the cluster.
--
-- Returns a table of the following format.
--
-- {
--     [<instance_name>] = {
--         instance_name = <...>,
--         replicaset_name = <...>,
--         group_name = <...>,
--     },
--     <...>
-- }
function methods.instances(self)
    selfcheck(self, 'instances')
    initcheck(self, 'instances', 'cluster')

    local res = {}

    for group_name, group in pairs(self:_cconfig().groups or {}) do
        for replicaset_name, replicaset in pairs(group.replicasets or {}) do
            for instance_name, _ in pairs(replicaset.instances or {}) do
                assert(res[instance_name] == nil)
                res[instance_name] = {
                    instance_name = instance_name,
                    replicaset_name = replicaset_name,
                    group_name = group_name,
                }
            end
        end
    end

    return res
end

function methods.instance_uri(self, uri_type, opts)
    selfcheck(self, 'instance_uri')
    initcheck(self, 'instance_uri', 'cluster')
    uri_type = uri_type == nil and 'peer' or uri_type
    if uri_type ~= 'peer' and uri_type ~= 'sharding' then
        error(('Expected "peer" or "sharding", got %q'):format(uri_type), 0)
    end
    opts = opts == nil and {} or opts
    if type(opts) ~= 'table' then
        error(('Expected table, got %s'):format(type(opts)), 0)
    end
    if opts.instance ~= nil and type(opts.instance) ~= 'string' then
        error(('Expected string, got %s'):format(type(opts.instance)), 0)
    end

    local uri_opts = {
        instance = opts.instance,
        use_default = true,
    }
    return self._configdata_applied:_instance_uri(uri_type, uri_opts)
end

-- The object is a singleton. The constructor should be called
-- only once.
local function new()
    local self = setmetatable({
        _sources = {},
        _appliers = {},
        -- There are values the module need to hold, which are not
        -- part of the configuration. They're stored here.
        _instance_name = nil,
        _config_file = nil,
        -- Collected config values.
        _configdata = nil,
        -- Track applied config values as well.
        _configdata_applied = nil,
        -- Metadata from sources.
        _metadata = {},
        -- Current status.
        _status = 'uninitialized',
    }, mt)

    -- Track situations when something is going wrong.
    self._aboard = aboard.new({
        -- Transit from 'check_warnings' to 'ready' if the last
        -- warning is dropped.
        on_drop = function(_, _)
            if not self._aboard:is_empty() then
                return
            end
            if self._status == 'check_warnings' then
                self._status = 'ready'
            end
        end,
    })

    return self
end

return new()
