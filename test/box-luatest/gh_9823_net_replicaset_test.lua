local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_all(cluster.clean)
g.after_each(cluster.drop)

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
    builder:use_replicaset('router')
           :add_instance('router', {})
    builder:use_replicaset('storage')
            :set_replicaset_option('replication.failover', 'election')
    for i = 1, 3 do
        builder:add_instance('storage' .. i, {})
    end
    return builder:config()
end

-- Test errors of replicaset connector.
g.test_net_replicaset_error = function(cg)
    local config = test_config()
    config.groups.test.replicasets.storage.replication.failover = 'supervised'
    local cluster = cluster.new(cg, config)
    cluster:start()

    -- Check unknown replicaset does not work.
    cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')
        local err_msg = 'The replicaset was not found by its name'
        local succ, err = pcall(net_replicaset.connect, 'moon')
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'moon')
    end)

    -- Make all instances read-only.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                box.cfg{read_only = true}
            end)
        end
    end)

    -- Check that the replicaset does not work.
    cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')
        local rs = net_replicaset.connect('storage')
        local err_msg = 'Writable instance was not found in replicaset'
        local succ, err = pcall(rs.call_leader, rs, 'box.info',
                                {}, {timeout = 1})
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'storage')
        rs:close()
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

    -- Check that the replicaset does not work.
    cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')
        local rs = net_replicaset.connect('storage')
        t.helpers.retrying({timeout = 2, delay = 0.1}, function()
            local err_msg = 'More than one writable was found in replicaset'
            local succ, err = pcall(rs.call_leader, rs, 'box.info',
                                    {}, {timeout = 1})
            t.assert_equals(succ, false)
            t.assert_equals(err.message, err_msg)
            t.assert_equals(err.replicaset, 'storage')
        end)
        rs:close()
    end)
end

-- Test the normal work of replicaset connector.
g.test_net_replicaset_basics = function(cg)
    local config = test_config()
    local cluster = cluster.new(cg, config)
    cluster:start()

    -- Connect using config.
    local leader_name = cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')

        local connect_cfg = {
            name = 'storage',
            instances = {},
        }
        for i = 1, 3 do
            local instance_name = 'storage' .. i
            local uri = 'unix/:./' .. instance_name .. '.iproto'
            local instance_config = {
                endpoint = {
                    uri = uri,
                    login = 'storage',
                    password = 'secret_storage',
                }
            }
            connect_cfg.instances[instance_name] = instance_config
        end
        local rs = net_replicaset.connect(connect_cfg)
        local info = rs:call_leader('box.info')
        t.assert_equals(info.ro, false)
        rs:close()
        return info.name
    end)

    -- Connect using replicaset name.
    local another_leader_name = cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')
        local rs = net_replicaset.connect('storage')
        local info = rs:call_leader('box.info')
        t.assert_equals(info.ro, false)
        local test_data = {
            rs = rs,
        }
        rawset(_G, 'test_data', test_data)
        return info.name
    end)
    t.assert_equals(leader_name, another_leader_name)

    -- Change leader.
    local new_leader_name = leader_name == 'storage1' and 'storage2' or
        leader_name == 'storage2' and 'storage3' or 'storage1'
    cluster[new_leader_name]:exec(function()
        box.ctl.promote()
    end)

    local leader_name = cluster.router:exec(function()
        local test_data = rawget(_G, 'test_data')
        local rs = test_data.rs
        local info = t.helpers.retrying({timeout = 2, delay = 0.1}, function()
            local info = rs:call_leader('box.info')
            t.assert_equals(info.ro, false)
            return info
        end)

        -- Test that replicaset is deleted successfully by GC.
        local weak_ref = setmetatable({rs = rs}, {__mode = 'v'})
        -- Test also replicaset internals also collected.
        for instance_name, instance in pairs(rs.instances) do
            weak_ref[instance_name] = instance
            weak_ref[instance_name .. '_conn'] = instance.conn
        end
        test_data.rs = nil
        rs = nil
        collectgarbage()
        collectgarbage()
        -- Check reference to replicaset explicitly.
        t.assert(weak_ref.rs == nil)
        -- Check all other references too.
        for _, v in pairs(weak_ref) do
            t.assert(v == nil)
        end

        return info.name
    end)

    t.assert_equals(leader_name, new_leader_name)
end

-- Test watch_leader method.
g.test_watch_leader = function(cg)
    local config = test_config()
    local cluster = cluster.new(cg, config)
    cluster:start()

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
    local leader_name = cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')
        local rs = net_replicaset.connect('storage')
        local info = rs:call_leader('box.info')
        local leader_name = info.name

        local test_data = {
            rs = rs,
            leader_name = leader_name,
            key1_value = 0,
            key2_value = 0,
            long_call_count = 0,
            long_wait = false,
        }
        rawset(_G, 'test_data', test_data)

        local function func1(key, value, instance_name)
            t.assert_equals(key, 'key1')
            t.assert_equals(instance_name, test_data.leader_name)
            if value then
                local old_value = test_data.key1_value
                test_data.key1_value = bit.bor(old_value, value)
            end
        end
        test_data.watcher1 = rs:watch_leader('key1', func1)

        local function func2(key, value, instance_name)
            t.assert_equals(key, 'key2')
            t.assert_equals(instance_name, test_data.leader_name)
            if value then
                local old_value = test_data.key2_value
                test_data.key2_value = bit.bor(old_value, value)
            end
        end
        test_data.watcher2 = rs:watch_leader('key2', func2)

        local function long_func(_key, _value, instance_name)
            t.assert_equals(instance_name, test_data.leader_name)
            test_data.long_call_count = test_data.long_call_count + 1
            test_data.long_wait = true
            -- Don't leave until somebody resets long_wait.
            while test_data.long_wait do
                require('fiber').sleep(0.1)
            end
        end
        test_data.long_watcher = rs:watch_leader('long', long_func)

        t.helpers.retrying({timeout = 2, delay = 0.1}, function()
            t.assert_equals(test_data.key1_value, 1)
        end)
        t.assert_equals(test_data.key2_value, 0)
        t.helpers.retrying({timeout = 2, delay = 0.1}, function()
            t.assert_equals(test_data.long_call_count, 1)
        end)

        return leader_name
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
    cluster.router:exec(function()
        local test_data = rawget(_G, 'test_data')

        t.helpers.retrying({timeout = 2, delay = 0.1}, function()
            t.assert_equals(test_data.key1_value, 1 + 4)
            t.assert_equals(test_data.key2_value, 8)
            t.assert_equals(test_data.long_call_count, 1)
        end)
    end)

    -- Change the leader.
    local new_leader_name = leader_name == 'storage1' and 'storage2' or
            leader_name == 'storage2' and 'storage3' or 'storage1'
    cluster.router:exec(function(new_leader_name)
        local test_data = rawget(_G, 'test_data')
        test_data.leader_name = new_leader_name
    end, {new_leader_name})
    cluster[new_leader_name]:exec(function()
        box.ctl.promote()
    end)

    -- Check that events was received after the leader change.
    cluster.router:exec(function()
        local test_data = rawget(_G, 'test_data')

        t.helpers.retrying({timeout = 2, delay = 0.1}, function()
            t.assert_equals(test_data.key1_value, 1 + 4 + 16)
            t.assert_equals(test_data.key2_value, 8 + 32)
            t.assert_equals(test_data.long_call_count, 1)
            test_data.long_wait = false
            t.helpers.retrying({timeout = 2, delay = 0.1}, function()
                t.assert_equals(test_data.long_call_count, 2)
            end)
        end)
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
    cluster.router:exec(function()
        local test_data = rawget(_G, 'test_data')

        t.helpers.retrying({timeout = 2, delay = 0.1}, function()
            t.assert_equals(test_data.key1_value, 1 + 4 + 16 + 64)
            t.assert_equals(test_data.key2_value, 8 + 32 + 128)
            t.assert_equals(test_data.long_call_count, 2)
            test_data.long_wait = false
            t.helpers.retrying({timeout = 2, delay = 0.1}, function()
                t.assert_equals(test_data.long_call_count, 3)
            end)
        end)
    end)

    -- Unregister watcher1 and check that there no more updates.
    cluster.router:exec(function()
        local test_data = rawget(_G, 'test_data')
        test_data.watcher1:unregister()
    end)
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
    cluster.router:exec(function()
        local test_data = rawget(_G, 'test_data')

        t.helpers.retrying({timeout = 2, delay = 0.1}, function()
            t.assert_equals(test_data.key1_value, 1 + 4 + 16 + 64)
            t.assert_equals(test_data.key2_value, 8 + 32 + 128 + 2048)
        end)
    end)

    -- Change the leader again.
    leader_name = new_leader_name
    local new_leader_name = leader_name == 'storage1' and 'storage2' or
            leader_name == 'storage2' and 'storage3' or 'storage1'
    cluster.router:exec(function(new_leader_name)
        local test_data = rawget(_G, 'test_data')
        test_data.leader_name = new_leader_name
    end, {new_leader_name})
    cluster[new_leader_name]:exec(function()
        box.ctl.promote()
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
    cluster.router:exec(function()
        local test_data = rawget(_G, 'test_data')

        t.helpers.retrying({timeout = 2, delay = 0.1}, function()
            t.assert_equals(test_data.key1_value, 1 + 4 + 16 + 64)
            t.assert_equals(test_data.key2_value, 8 + 32 + 128 + 2048 + 16)
            t.assert_equals(test_data.long_call_count, 3)
            test_data.long_wait = false
            t.helpers.retrying({timeout = 2, delay = 0.1}, function()
                t.assert_equals(test_data.long_call_count, 4)
            end)
        end)
    end)
end
