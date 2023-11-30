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
-- Usage (failure case):
--
-- | replicaset.startup_error(g, config, error_message)

local yaml = require('yaml')
local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local function init(g)
    treegen.init(g)
end

-- Stop all the managed instances using <server>:drop().
local function drop(_g)
    -- Not implemented yet.
end

local function clean(g)
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
    startup_error = startup_error,
}
