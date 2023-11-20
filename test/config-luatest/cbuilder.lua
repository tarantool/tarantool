-- Configuration builder.
--
-- Basic usage:
--
-- local config = cbuilder_new()
--     :add_instance('instance-001', {
--         database = {
--             mode = 'rw',
--         },
--     })
--     :add_instance('instance-002', {})
--     :add_instance('instance-003', {})
--     :config()
--
-- The instances are added into replicaset-001 in group-001.
--
-- The default credentials and iproto options are added to
-- setup replication and to allow a test to connect to the
-- instances.
--
-- There is a few other methods:
--
-- * :set_replicaset_option('foo.bar', value)
-- * :set_instance_option('instance-001', 'foo.bar', value)
--
-- All the methods supports chaining (returns the builder object
-- back).
--
-- Beware: It is written to construct a configuration with a
-- single replicaset. The module can be extended to a more general
-- case using methods that stores current group/replicaset.
--
-- local config = cbuilder_new()
--     :use_group('group-001')
--     :use_replicaset('replicaset-001')
--     :add_instance(<...>)
--     :add_instance(<...>)
--     :add_instance(<...>)
--
--     :use_group('group-002')
--     :use_replicaset('replicaset-002')
--     :add_instance(<...>)
--     :add_instance(<...>)
--     :add_instance(<...>)
--
--     :config()
--
-- It is not implemented yet.

local fun = require('fun')
local cluster_config = require('internal.config.cluster_config')

local base_config = {
    credentials = {
        users = {
            replicator = {
                password = 'secret',
                roles = {'replication'},
            },
            client = {
                password = 'secret',
                roles = {'super'},
            },
        },
    },
    iproto = {
        listen = {{
            uri = 'unix/:./{{ instance_name }}.iproto'
        }},
        advertise = {
            peer = {
                login = 'replicator',
            }
        },
    },
    replication = {
        -- The default value is 1 second. It is good for a real
        -- usage, but often suboptimal for testing purposes.
        --
        -- If an instance can't connect to another instance (say,
        -- because it is not started yet), it retries the attempt
        -- after so called 'replication interval', which is equal
        -- to replication timeout.
        --
        -- One second waiting means one more second for a test
        -- case and, if there are many test cases with a
        -- replicaset construction, it affects the test timing a
        -- lot.
        --
        -- replication.timeout = 0.1 second reduces the timing
        -- by half for my test.
        timeout = 0.1,
    },
}

-- Set an option for replicaset-001.
local function cbuilder_set_replicaset_option(self, path, value)
    assert(type(path) == 'string')
    path = fun.chain({
        'groups', 'group-001',
        'replicasets', 'replicaset-001',
    }, path:split('.')):totable()

    -- <schema object>:set() validation is too tight. Workaround
    -- it. Maybe we should reconsider this :set() behavior in a
    -- future.
    if value == nil then
        local cur = self._config
        for i = 1, #path - 1 do
            -- Create missed fields.
            local component = path[i]
            if cur[component] == nil then
                cur[component] = {}
            end

            cur = cur[component]
        end
        cur[path[#path]] = value
        return self
    end

    cluster_config:set(self._config, path, value)
    return self
end

-- Set an option of a particular instance.
local function cbuilder_set_instance_option(self, instance_name, path, value)
    assert(type(path) == 'string')
    path = fun.chain({
        'groups', 'group-001',
        'replicasets', 'replicaset-001',
        'instances', instance_name,
    }, path:split('.')):totable()

    cluster_config:set(self._config, path, value)
    return self
end

-- Add an instance with the given options.
--
-- All the instances are added into replicaset 'replicaset-001' of
-- group 'group-001'.
local function cbuilder_add_instance(self, instance_name, iconfig)
    local path = {
        'groups', 'group-001',
        'replicasets', 'replicaset-001',
        'instances', instance_name,
    }
    cluster_config:set(self._config, path, iconfig)
    return self
end

-- Return the resulting configuration.
local function cbuilder_config(self)
    return self._config
end

local cbuilder_mt = {
    set_replicaset_option = cbuilder_set_replicaset_option,
    set_instance_option = cbuilder_set_instance_option,
    add_instance = cbuilder_add_instance,
    config = cbuilder_config,
}

cbuilder_mt.__index = cbuilder_mt

local function cbuilder_new(config)
    config = table.deepcopy(config or base_config)
    return setmetatable({
        _config = config,
    }, cbuilder_mt)
end

return {
    new = cbuilder_new,
}
