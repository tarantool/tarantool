local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_all(cluster.clean)
g.after_each(cluster.drop)

local REDUNDANCY_FACTOR = 3

local function test_config()
    local sharding_role = {
        privileges = {{permissions = {'execute'}, universe = true}},
    }
    local builder = cbuilder.new()
    :set_global_option('credentials.roles.sharding', sharding_role)
    :set_global_option('credentials.users.storage.roles', {'sharding'})
    :set_global_option('credentials.users.storage.password', 'secret_storage')
    :set_global_option('iproto.advertise.sharding.login', 'storage')
    :use_group('test')
    builder:use_replicaset('router'):add_instance('router', {})
    builder:use_replicaset('storage')
           :set_replicaset_option('replication.failover', 'election')
    for i = 1, REDUNDANCY_FACTOR do
        builder:add_instance('storage' .. i, {})
    end
    return builder:config()
end

-- Get a valid storage instance name different from given.
local function get_another_storage_name(storage_name)
    local num = tonumber(string.sub(storage_name, 8))
    num = num + 1
    if num > REDUNDANCY_FACTOR then
        num = 1
    end
    return string.sub(storage_name, 1, 7) .. num
end

local function get_connect_cfg(cluster)
    local connect_cfg = {
        name = 'storage',
        instances = {},
    }
    for i = 1, REDUNDANCY_FACTOR do
        local instance_name = 'storage' .. i
        local uri = cluster[instance_name].net_box_uri
        local instance_config = {
            endpoint = {
                uri = uri,
                login = 'storage',
                password = 'secret_storage',
            }
        }
        connect_cfg.instances[instance_name] = instance_config
    end
    return connect_cfg
end

-- Test errors of replicaset connector.
g.test_net_replicaset_error = function(cg)
    local config = test_config()
    config.groups.test.replicasets.storage.replication.failover = 'supervised'
    local cluster = cluster.new(cg, config)
    cluster:start()
    local net_replicaset = require('internal.net.replicaset')
    local connect_cfg = get_connect_cfg(cluster)

    -- Check that unknown replicaset does not work.
    cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')
        local err_msg = 'The replicaset was not found by its name'
        local succ, err = pcall(net_replicaset.connect, 'moon')
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'moon')
    end)

    -- Preconnect to the replicaset.
    local rs = net_replicaset.connect(connect_cfg)
    local info = rs:call_leader('box.info')
    t.assert_equals(info.ro, false)

    -- Make all instances read-only.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                box.cfg{read_only = true}
            end)
        end
    end)

    -- Check that call_leader does not work without a leader.
    local err_msg = 'Writable instance was not found in replicaset'
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        local succ, err = pcall(rs.call_leader, rs, 'box.info',
                                {}, {timeout = 0.1})
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'storage')
    end)

    -- Reconnect and try that same.
    rs:close()
    rs = net_replicaset.connect(connect_cfg)
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        local succ, err = pcall(rs.call_leader, rs, 'box.info',
                                {}, {timeout = 0.1})
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'storage')
    end)

    -- Make all instances writable.
    config.groups.test.replicasets.storage.replication.failover = 'off'
    cluster:reload(config)
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                box.cfg{read_only = false}
            end)
        end
    end)

    -- Check that call_leader does not work without a leader.
    local err_msg = 'More than one writable was found in replicaset'
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        local succ, err = pcall(rs.call_leader, rs, 'box.info',
                                {}, {timeout = 0.1})
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'storage')
    end)
    -- Reconnect and try that same.
    rs:close()
    rs = net_replicaset.connect(connect_cfg)
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        local succ, err = pcall(rs.call_leader, rs, 'box.info',
                                {}, {timeout = 0.1})
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'storage')
    end)
    rs:close()
end

-- Test the normal work of replicaset connector.
g.test_net_replicaset_basics = function(cg)
    local config = test_config()
    local cluster = cluster.new(cg, config)
    cluster:start()
    local net_replicaset = require('internal.net.replicaset')
    local connect_cfg = get_connect_cfg(cluster)

    -- Connect using replicaset name.
    local leader_name = cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')
        local rs = net_replicaset.connect('storage')
        local info = rs:call_leader('box.info')
        t.assert_equals(info.ro, false)
        rs:close()
        return info.name
    end)

    -- Connect using config.
    local rs = net_replicaset.connect(connect_cfg)
    local info = rs:call_leader('box.info')
    t.assert_equals(info.ro, false)
    t.assert_equals(info.name, leader_name)

    -- Change the leader.
    local new_leader_name = get_another_storage_name(leader_name)
    cluster[new_leader_name]:exec(function()
        box.ctl.promote()
    end)

    -- Check rebind to the new leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        info = rs:call_leader('box.info')
        t.assert_equals(info.ro, false)
    end)
        t.assert_equals(info.name, new_leader_name)
end

-- Test that replicaset is deleted successfully by GC.
g.test_net_replicaset_gc = function(cg)
    local config = test_config()
    local cluster = cluster.new(cg, config)
    cluster:start()
    local net_replicaset = require('internal.net.replicaset')
    local connect_cfg = get_connect_cfg(cluster)

    local rs = net_replicaset.connect(connect_cfg)
    local info = rs:call_leader('box.info')
    t.assert_equals(info.ro, false)

    local weak_ref = setmetatable({rs = rs}, {__mode = 'v'})
    -- Test also that replicaset internals also collected.
    for instance_name, instance in pairs(rs.instances) do
        weak_ref[instance_name] = instance
        weak_ref[instance_name .. '_conn'] = instance.conn
    end

    rs = nil
    -- Calm down luacheck about unused values.
    t.assert(rs == nil)
    collectgarbage()
    -- Double collect to avoid possible resurrection effects.
    collectgarbage()
    -- Check reference to replicaset explicitly.
    t.assert(weak_ref.rs == nil)
    -- Check all other references too.
    for _, v in pairs(weak_ref) do
        t.assert(v == nil)
    end
end
