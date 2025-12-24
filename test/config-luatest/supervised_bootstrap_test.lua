local yaml = require('yaml')
local fio = require('fio')
local socket = require('socket')
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

g.test_basic = function()
    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'election')
        :set_replicaset_option('replication.bootstrap_strategy', 'supervised')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start({wait_until_ready = false})

    -- Connect to a text console.
    local control_path = fio.pathjoin(cluster._dir,
        'var/run/i-002/tarantool.control')
    local s = t.helpers.retrying({timeout = 60}, function()
        local s, err = socket.tcp_connect('unix/', control_path)
        if s == nil then
            error(err)
        end
        -- Skip the greeting.
        s:read('\n')
        s:read('\n')
        return s
    end)

    -- Issue box.ctl.make_bootstrap_leader() command on i-002.
    --
    -- We should perform the retrying, because we can reach the
    -- instance with the command before it calls the first
    -- box.cfg(). In this case the command raises the following
    -- error.
    --
    -- > box.ctl.make_bootstrap_leader() does not support
    -- > promoting this instance before box.cfg() is called
    t.helpers.retrying({timeout = 60}, function()
        s:write('do box.ctl.make_bootstrap_leader() return "done" end\n')
        local reply = yaml.decode(s:read('...\n'))
        t.assert_equals(reply, {'done'})
    end)
    s:close()

    -- Wait till all the servers will finish the bootstrap.
    cluster:each(function(server)
        server:wait_until_ready()
    end)

    -- Verify that all the instances are healthy.
    cluster:each(function(server)
        t.helpers.retrying({timeout = 60}, function()
            server:exec(function()
                t.assert_equals(box.info.status, 'running')
            end)
        end)
    end)

    -- Verify that the given instance was acted as a bootstrap
    -- leader.
    cluster['i-002']:exec(function()
        t.assert_equals(box.info.id, 1)
    end)
end

g.test_upscale_is_not_stuck = function()
    local config_1 = cbuilder:new()
        :use_replicaset('r-001')
        :set_replicaset_option('replication.failover', 'supervised')
        :set_replicaset_option('replication.bootstrap_strategy', 'auto')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :config()

    local cluster = cluster:new(config_1)
    cluster:start()

    local config_2 = cbuilder:new(config_1)
        :use_replicaset('r-001')
        :add_instance('i-003', {})
        :set_global_option('failover.replicasets.r-001.priority', {
            ['i-003'] = 1,
        })
        :config()
    cluster:sync(config_2)
    cluster:start_instance('i-003')

    cluster['i-003']:exec(function()
        local config = require('config')
        local t = require('luatest')
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(config:info().status, 'ready')
            t.assert_equals(box.info.ro, true)
            t.assert_not_equals(box.info.id, 1)
        end)
    end)
end
