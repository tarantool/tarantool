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
