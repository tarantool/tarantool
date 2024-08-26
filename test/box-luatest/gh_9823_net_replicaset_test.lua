local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
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
    local builder = cbuilder:new()
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
        t.helpers.retrying({timeout = 10, delay = 0.1}, box.ctl.promote)
        box.ctl.wait_rw()
    end)

    -- Check rebind to the new leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        info = rs:call_leader('box.info')
        t.assert_equals(info.ro, false)
        t.assert_equals(info.name, new_leader_name)
    end)
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
    local status = {}
    local watcher = rs:watch_leader('box.status', function(_key, value)
        status = value
    end)
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(status.is_ro, false)
    end)

    local weak_ref = setmetatable({rs = rs}, {__mode = 'v'})
    -- Test also that replicaset internals also collected.
    for instance_name, instance in pairs(rs.instances) do
        weak_ref[instance_name] = instance
        weak_ref[instance_name .. '_conn'] = instance.conn
    end
    weak_ref.watcher = watcher

    watcher:unregister()
    watcher = nil
    rs = nil
    -- Calm down luacheck about unused values.
    t.assert(rs == nil)
    t.assert(watcher == nil)
    collectgarbage()
    -- Double collect to avoid possible resurrection effects.
    collectgarbage()
    -- Check reference to replicaset and watcher explicitly.
    t.assert(weak_ref.rs == nil)
    t.assert(weak_ref.watcher == nil)
    -- Check all other references too.
    for _, v in pairs(weak_ref) do
        t.assert(v == nil)
    end
end

-- Test watch_leader method.
g.test_watch_leader = function(cg)
    local config = test_config()
    local cluster = cluster.new(cg, config)
    cluster:start()
    local net_replicaset = require('internal.net.replicaset')
    local connect_cfg = get_connect_cfg(cluster)

    -- Wait for the leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        local leader_count = 0
        cluster:each(function(server)
            if server.alias:startswith('storage') then
                leader_count = leader_count + server:exec(function()
                    return box.info.ro and 0 or 1
                end)
            end
        end)
        t.assert_equals(leader_count, 1)
    end)

    -- Set initial value to event 'key1'.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                if not box.info.ro then
                    box.broadcast('key1', 1)
                else
                    box.broadcast('key1', 2)
                end
            end)
        end
    end)

    -- Subscribe and check that event was received only from leader.
    local rs = net_replicaset.connect(connect_cfg)
    local info = rs:call_leader('box.info')
    local leader_name = info.name
    local key1_value = 0
    local key2_value = 0
    local long_call_count = 0
    local long_wait = false

    local function func1(key, value, instance_name)
        t.assert_equals(key, 'key1')
        t.assert_equals(instance_name, leader_name)
        if value then
            key1_value = bit.bor(key1_value, value)
        end
    end
    local watcher1 = rs:watch_leader('key1', func1)

    local function func2(key, value, instance_name)
        t.assert_equals(key, 'key2')
        t.assert_equals(instance_name, leader_name)
        if value then
            key2_value = bit.bor(key2_value, value)
        end
    end
    local watcher2 = rs:watch_leader('key2', func2)

    local function long_func(_key, _value, instance_name)
        t.assert_equals(instance_name, leader_name)
        long_call_count = long_call_count + 1
        long_wait = true
        -- Don't leave until somebody resets long_wait.
        while long_wait do
            require('fiber').sleep(0.1)
        end
    end
    local long_watcher = rs:watch_leader('long', long_func)

    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1)
    end)
    t.assert_equals(key2_value, 0)
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(long_call_count, 1)
    end)

    -- Set initial value to event 'key2' and update 'key1' and 'long'.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                if not box.info.ro then
                    box.broadcast('key1', 4)
                    box.broadcast('key2', 8)
                else
                    box.broadcast('key1', 16)
                    box.broadcast('key2', 32)
                end
                box.broadcast('long', math.random())
            end)
        end
    end)

    -- Check that events was received only from leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4)
        t.assert_equals(key2_value, 8)
        t.assert_equals(long_call_count, 1)
    end)

    -- Change the leader.
    leader_name = get_another_storage_name(leader_name)
    cluster[leader_name]:exec(function()
        t.helpers.retrying({timeout = 10, delay = 0.1}, box.ctl.promote)
        box.ctl.wait_rw()
    end)

    -- Check that events was received after the leader change.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4 + 16)
        t.assert_equals(key2_value, 8 + 32)
        t.assert_equals(long_call_count, 1)
    end)
    long_wait = false
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(long_call_count, 2)
    end)

    -- Set new values again.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                if not box.info.ro then
                    box.broadcast('key1', 64)
                    box.broadcast('key2', 128)
                else
                    box.broadcast('key1', 256)
                    box.broadcast('key2', 512)
                end
                box.broadcast('long', math.random())
            end)
        end
    end)

    -- Check that events was received from the leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4 + 16 + 64)
        t.assert_equals(key2_value, 8 + 32 + 128)
        t.assert_equals(long_call_count, 2)
    end)
    long_wait = false
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(long_call_count, 3)
    end)

    -- Unregister watcher1 and check that there no more updates.
    watcher1:unregister()

    -- Set new values again.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                if not box.info.ro then
                    box.broadcast('key1', 1024)
                    box.broadcast('key2', 2048)
                else
                    box.broadcast('key1', 4096)
                    box.broadcast('key2', 8192)
                end
            end)
        end
    end)

    -- Check that events1 was not while event2 was received from the leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4 + 16 + 64)
        t.assert_equals(key2_value, 8 + 32 + 128 + 2048)
    end)

    -- Change the leader again.
    leader_name = get_another_storage_name(leader_name)
    cluster[leader_name]:exec(function()
        t.helpers.retrying({timeout = 10, delay = 0.1}, box.ctl.promote)
        box.ctl.wait_rw()
    end)

    -- Check that events1 was not while event2 was received from the leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4 + 16 + 64)
        t.assert_equals(key2_value, 8 + 32 + 128 + 2048 + 8192)
        t.assert_equals(long_call_count, 3)
    end)

    -- Set new values again.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                if not box.info.ro then
                    box.broadcast('key1', 1024)
                    box.broadcast('key2', 16)
                else
                    box.broadcast('key1', 4096)
                    box.broadcast('key2', 64)
                end
            end)
        end
    end)

    -- Check that events1 was not while event2 was received from the leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4 + 16 + 64)
        t.assert_equals(key2_value, 8 + 32 + 128 + 2048 + 8192 + 16)
        t.assert_equals(long_call_count, 3)
    end)
    long_wait = false
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(long_call_count, 4)
    end)

    watcher1:unregister()
    watcher2:unregister()
    long_watcher:unregister()
    rs:close()
end
