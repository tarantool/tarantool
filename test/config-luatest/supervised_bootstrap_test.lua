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

-- gh-12970: rebootstrap (wipe + restart) of the instance with the
-- minimal name must not bring it up in the RW mode when the
-- replicaset is already bootstrapped and has an appointed leader.
--
-- Before the fix such an instance considered itself a bootstrap
-- leader (empty data directory + minimal name), started in RW and
-- the post-box.cfg check passed erroneously, because the instance
-- rejoined into its old registration with replica id 1. As a
-- result there were two RW instances in the replicaset.
--
-- Now the bootstrap leader candidate starts with read_only =
-- 'unless_bootstrap' and a rejoined instance not only ends up in RO, but never
-- reports a writable state at all.
g.test_rebootstrap_of_min_name_instance_goes_ro = function()
    local config = cbuilder:new()
        :use_replicaset('r-001')
        :set_replicaset_option('replication.failover', 'supervised')
        :set_replicaset_option('replication.bootstrap_strategy', 'auto')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start()

    -- i-001 has the minimal name, so it bootstraps the replicaset
    -- and starts in RW. The configured 'unless_bootstrap' value is normalized
    -- to a boolean after the startup.
    cluster['i-001']:exec(function()
        t.assert_equals(box.info.ro, false)
        t.assert_equals(box.cfg.read_only, false)
    end)

    -- Imitate the failover coordinator: move the leadership to
    -- i-002.
    cluster['i-001']:exec(function()
        box.cfg({read_only = true})
    end)
    cluster['i-002']:exec(function()
        box.cfg({read_only = false})
    end)

    -- Rebootstrap i-001: stop it, wipe its data directory and
    -- start it again. It rejoins the replicaset (from i-002) into
    -- its old registration.
    cluster['i-001']:stop()
    local workdir = fio.pathjoin(cluster._dir, 'var/lib/i-001')
    for _, file in ipairs(fio.glob(fio.pathjoin(workdir, '*'))) do
        fio.unlink(file)
    end
    cluster['i-001']:start()

    -- The appointed leader must stay RW...
    cluster['i-002']:exec(function()
        t.assert_equals(box.info.ro, false)
    end)

    -- ...and the rebootstrapped instance must NOT become a second
    -- master: it should end up in RO despite its minimal name and
    -- empty data directory at startup.
    cluster['i-001']:exec(function()
        t.helpers.retrying({timeout = 10}, function()
            t.assert_equals(box.info.status, 'running')
            t.assert_equals(box.info.ro, true)
            t.assert_equals(box.cfg.read_only, true)
        end)
    end)

    -- Moreover, it must not go through a transient RW state: the
    -- log of the new incarnation must not contain the RW
    -- transition at all.
    --
    -- grep_log() with the default reset option looks only after
    -- the last restart banner, so the first (bootstrap) incarnation
    -- of i-001, which was legitimately RW, does not affect the
    -- check.
    t.assert_not(cluster['i-001']:grep_log('box switched to rw'))
    -- A positive control that the log is being read: the new
    -- incarnation joins the replicaset from scratch.
    t.assert(cluster['i-001']:grep_log('bootstrapping replica from'))
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
