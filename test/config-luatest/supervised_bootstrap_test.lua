local yaml = require('yaml')
local fio = require('fio')
local socket = require('socket')
local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

g.test_basic = function(g)
    local config = cbuilder.new()
        :set_replicaset_option('replication.failover', 'election')
        :set_replicaset_option('replication.bootstrap_strategy', 'supervised')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    local cluster = cluster.new(g, config)
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
