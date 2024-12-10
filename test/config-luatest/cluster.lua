-- Cluster management utils.
--
-- Add the following code into a test file.
--
-- | local g = t.group()
-- |
-- | g.before_all(cluster.init)
-- | g.after_each(cluster.drop)
-- | g.after_all(cluster.clean)
--
-- It properly initializes the module and cleans up all the
-- resources between and after test cases.
--
-- Usage (success case):
--
-- | local cluster = cluster_new(g, config)
-- | cluster:start()
-- | cluster['instance-001']:exec(<...>)
-- | cluster:each(function(server)
-- |     server:exec(<...>)
-- | end)
--
-- Usage (failure case):
--
-- | cluster.startup_error(g, config, error_message)

local fun = require('fun')
local yaml = require('yaml')
local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local server = require('luatest.server')

-- Temporary stub.
local function init(g)
    assert(g)  -- temporary stub to not fail luacheck due to unused var
end

-- Stop all the managed instances using <server>:drop().
local function drop(g)
    if g.cluster ~= nil then
        g.cluster:drop()
    end
    g.cluster = nil
end

local function clean(g)
    assert(g.cluster == nil)
end

-- {{{ Helpers

-- Collect names of all the instances defined in the config
-- in the alphabetical order.
local function instance_names_from_config(config)
    local instance_names = {}
    for _, group in pairs(config.groups or {}) do
        for _, replicaset in pairs(group.replicasets or {}) do
            for name, _ in pairs(replicaset.instances or {}) do
                table.insert(instance_names, name)
            end
        end
    end
    table.sort(instance_names)
    return instance_names
end

-- }}} Helpers

-- {{{ Cluster management

local function cluster_each(self, f)
    fun.iter(self._servers):each(function(server)
        f(server)
    end)
end

local function cluster_size(self)
    return #self._servers
end

-- Start all the instances.
local function cluster_start(self, opts)
    self:each(function(server)
        server:start({wait_until_ready = false})
    end)

    -- wait_until_ready is true by default.
    local wait_until_ready = true
    if opts ~= nil and opts.wait_until_ready ~= nil then
        wait_until_ready = opts.wait_until_ready
    end

    if wait_until_ready then
        self:each(function(server)
            server:wait_until_ready()
        end)
    end

    -- wait_until_running is equal to wait_until_ready by default.
    local wait_until_running = wait_until_ready
    if opts ~= nil and opts.wait_until_running ~= nil then
        wait_until_running = opts.wait_until_running
    end

    if wait_until_running then
        self:each(function(server)
            server:exec(function()
                t.helpers.retrying({timeout = 60}, function()
                    t.assert_equals(box.info.status, 'running')
                end)
            end)
        end)
    end
end

-- Start the given instance.
local function cluster_start_instance(self, instance_name)
    local server = self._server_map[instance_name]
    assert(server ~= nil)
    server:start()
end

local function cluster_stop(self)
    for _, server in ipairs(self._servers or {}) do
        server:stop()
    end
end

local function cluster_drop(self)
    for _, server in ipairs(self._servers or {}) do
        server:drop()
    end
    self._servers = nil
    self._server_map = nil
end

-- Sync the cluster object with the new config.
--
-- It performs the following actions.
--
-- * Write the new config into the config file.
-- * Update the internal list of instances.
local function cluster_sync(self, config)
    local instance_names = instance_names_from_config(config)

    treegen.write_file(self._dir, self._config_file_rel, yaml.encode(config))

    for i, name in ipairs(instance_names) do
        if self._server_map[name] == nil then
            local server = server:new(fun.chain(self._server_opts, {
                alias = name,
            }):tomap())
            table.insert(self._servers, i, server)
            self._server_map[name] = server
        end
    end
end

-- Reload configuration on all the instances.
local function cluster_reload(self, config)
    -- Rewrite the configuration file if a new config is provided.
    if config ~= nil then
        treegen.write_file(self._dir, self._config_file_rel,
                           yaml.encode(config))
    end

    -- Reload config on all the instances.
    self:each(function(server)
        -- Assume that all the instances are started.
        --
        -- This requirement may be relaxed if needed, it is just
        -- for simplicity.
        assert(server.process ~= nil)

        server:exec(function()
            local config = require('config')

            config:reload()
        end)
    end)
end

local methods = {
    each = cluster_each,
    size = cluster_size,
    start = cluster_start,
    start_instance = cluster_start_instance,
    stop = cluster_stop,
    drop = cluster_drop,
    sync = cluster_sync,
    reload = cluster_reload,
}

local cluster_mt = {
    __index = function(self, k)
        if methods[k] ~= nil then
            return methods[k]
        end
        if self._server_map[k] ~= nil then
            return self._server_map[k]
        end
        return rawget(self, k)
    end
}

local function new(g, config, server_opts, opts)
    assert(config._config == nil, "Please provide cbuilder:new():config()")
    assert(g.cluster == nil)

    -- Prepare a temporary directory and write a configuration
    -- file.
    local dir = opts and opts.dir or treegen.prepare_directory({}, {})
    local config_file_rel = 'config.yaml'
    local config_file = treegen.write_file(dir, config_file_rel,
                                           yaml.encode(config))

    -- Collect names of all the instances defined in the config
    -- in the alphabetical order.
    local instance_names = instance_names_from_config(config)

    -- Generate luatest server options.
    local server_opts = fun.chain({
        config_file = config_file,
        chdir = dir,
        net_box_credentials = {
            user = 'client',
            password = 'secret',
        },
    }, server_opts or {}):tomap()

    -- Create luatest server objects.
    local servers = {}
    local server_map = {}
    for _, name in ipairs(instance_names) do
        local server = server:new(fun.chain(server_opts, {
            alias = name,
        }):tomap())
        table.insert(servers, server)
        server_map[name] = server
    end

    -- Create a cluster object and store it in 'g'.
    g.cluster = setmetatable({
        _servers = servers,
        _server_map = server_map,
        _dir = dir,
        _config_file_rel = config_file_rel,
        _server_opts = server_opts,
    }, cluster_mt)
    return g.cluster
end

-- }}} Replicaset management

-- {{{ Replicaset that can't start

-- Starts a all instance of a cluster from the given config and
-- ensure that all the instances fails to start and reports the
-- given error message.
local function startup_error(g, config, exp_err)
    assert(g)  -- temporary stub to not fail luacheck due to unused var
    assert(type(config) == 'table')
    assert(config._config == nil, "Please provide cbuilder:new():config()")
    -- Prepare a temporary directory and write a configuration
    -- file.
    local dir = treegen.prepare_directory({}, {})
    local config_file_rel = 'config.yaml'
    local config_file = treegen.write_file(dir, config_file_rel,
                                           yaml.encode(config))

    -- Collect names of all the instances defined in the config
    -- in the alphabetical order.
    local instance_names = instance_names_from_config(config)

    for _, name in ipairs(instance_names) do
        local env = {}
        local args = {'--name', name, '--config', config_file}
        local opts = {nojson = true, stderr = true}
        local res = justrun.tarantool(dir, env, args, opts)

        t.assert_equals(res.exit_code, 1)
        t.assert_str_contains(res.stderr, exp_err)
    end
end

-- }}} Replicaset that can't start

return {
    init = init,
    drop = drop,
    clean = clean,
    new = new,
    startup_error = startup_error,
}
