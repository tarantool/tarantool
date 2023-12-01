-- Replicaset management utils.
--
-- Add the following code into a test file.
--
-- | local g = t.group()
-- |
-- | g.before_all(replicaset.init)
-- | g.after_each(replicaset.drop)
-- | g.after_all(replicaset.clean)
--
-- It properly initializes the module and cleans up all the
-- resources between and after test cases.
--
-- Usage (success case):
--
-- | local replicaset = replicaset_new(g, config)
-- | replicaset:start()
-- | replicaset['instance-001']:exec(<...>)
-- | replicaset:each(function(server)
-- |     server:exec(<...>)
-- | end)
--
-- Usage (failure case):
--
-- | replicaset.startup_error(g, config, error_message)

local fun = require('fun')
local yaml = require('yaml')
local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')
local server = require('test.luatest_helpers.server')

local function init(g)
    treegen.init(g)
end

-- Stop all the managed instances using <server>:drop().
local function drop(g)
    if g.replicaset ~= nil then
        g.replicaset:drop()
    end
    g.replicaset = nil
end

local function clean(g)
    assert(g.replicaset == nil)
    treegen.clean(g)
end

-- {{{ Helpers

-- Collect names of all the instances in replicaset-001 of
-- group-001 in the alphabetical order.
local function instance_names_from_config(config)
    local group = config.groups['group-001']
    local replicaset = group.replicasets['replicaset-001']
    local instance_names = {}
    for name, _ in pairs(replicaset.instances) do
        table.insert(instance_names, name)
    end
    table.sort(instance_names)
    return instance_names
end

-- }}} Helpers

-- {{{ Replicaset management

local function replicaset_each(self, f)
    fun.iter(self._servers):each(function(server)
        f(server)
    end)
end

local function replicaset_size(self)
    return #self._servers
end

-- Start all the instances of replicaset-001 (from group-001).
local function replicaset_start(self)
    self:each(function(server)
        server:start({wait_until_ready = false})
    end)

    self:each(function(server)
        server:wait_until_ready()
    end)

    self:each(function(server)
        server:exec(function()
            t.assert_equals(box.info.replicaset.name, 'replicaset-001')
        end)
    end)
end

-- Start the given instance.
local function replicaset_start_instance(self, instance_name)
    local server = self._server_map[instance_name]
    assert(server ~= nil)
    server:start()

    server:exec(function()
        t.assert_equals(box.info.replicaset.name, 'replicaset-001')
    end)
end

local function replicaset_stop(self)
    for _, server in ipairs(self._servers or {}) do
        server:stop()
    end
end

local function replicaset_drop(self)
    for _, server in ipairs(self._servers or {}) do
        server:drop()
    end
    self._servers = nil
    self._server_map = nil
end

-- Sync the replicaset object with the new config.
--
-- It performs the following actions.
--
-- * Write the new config into the config file.
-- * Update the internal list of instances.
local function replicaset_sync(self, config)
    local instance_names = instance_names_from_config(config)

    treegen.write_script(self._dir, self._config_file_rel,
                         yaml.encode(config))

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
local function replicaset_reload(self, config)
    -- Rewrite the configuration file if a new config is provided.
    if config ~= nil then
        treegen.write_script(self._dir, self._config_file_rel,
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
    each = replicaset_each,
    size = replicaset_size,
    start = replicaset_start,
    start_instance = replicaset_start_instance,
    stop = replicaset_stop,
    drop = replicaset_drop,
    sync = replicaset_sync,
    reload = replicaset_reload,
}

local replicaset_mt = {
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

local function new(g, config, server_opts)
    assert(g.replicaset == nil)

    -- Prepare a temporary directory and write a configuration
    -- file.
    local dir = treegen.prepare_directory(g, {}, {})
    local config_file_rel = 'config.yaml'
    local config_file = treegen.write_script(dir, config_file_rel,
                                             yaml.encode(config))

    -- Collect names of all the instances in replicaset-001 of
    -- group-001 in the alphabetical order.
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

    -- Create a replicaset object and store it in 'g'.
    g.replicaset = setmetatable({
        _servers = servers,
        _server_map = server_map,
        _dir = dir,
        _config_file_rel = config_file_rel,
        _server_opts = server_opts,
    }, replicaset_mt)
    return g.replicaset
end

-- }}} Replicaset management

-- {{{ Replicaset that can't start

-- Starts a all instance of a replicaset from the given config and
-- ensure that all the instances fails to start and reports the
-- given error message.
local function startup_error(g, config, exp_err)
    -- Prepare a temporary directory and write a configuration
    -- file.
    local dir = treegen.prepare_directory(g, {}, {})
    local config_file_rel = 'config.yaml'
    local config_file = treegen.write_script(dir, config_file_rel,
                                             yaml.encode(config))

    -- Collect names of all the instances in replicaset-001 of
    -- group-001 in the alphabetical order.
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
