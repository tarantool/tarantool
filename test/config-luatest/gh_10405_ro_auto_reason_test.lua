local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local it = require('test.interactive_tarantool')

local function assert_ro_reason(server, reason)
    server:exec(function(reason)
        t.assert_equals(box.info.ro, reason ~= nil)
        t.assert_equals(box.info.ro_reason, reason)
    end, {reason})
end

local function cleanup(g)
    if g.it ~= nil then
        g.it:close()
        g.it = nil
    end
    if g.cluster ~= nil then
        g.cluster:drop()
        g.cluster = nil
    end
end

local g = t.group()

g.after_each(cleanup)

-- Check explicit and default database modes with failover disabled.
g.test_failover_off = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :set_instance_option('i-001', 'database.mode', 'rw')
        :set_instance_option('i-002', 'database.mode', 'ro')
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    assert_ro_reason(g.cluster['i-001'], nil)
    assert_ro_reason(g.cluster['i-002'], 'database.mode is set to "ro"')
    assert_ro_reason(g.cluster['i-003'],
        'database.mode defaults to "ro" for a multi-instance replicaset')
end

-- Check manual failover, isolated mode precedence, and safe startup.
g.test_manual_isolated_and_safe_startup = function(g)
    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    -- Only a non-leader gets the manual failover reason.
    assert_ro_reason(g.cluster['i-001'], nil)
    assert_ro_reason(g.cluster['i-002'],
        'not the leader in manual failover mode')

    -- Isolated mode overrides the reason that made the instance RO before it.
    g.it = it.connect(g.cluster['i-002'])
    local config_isolated = cbuilder:new(config)
        :set_instance_option('i-002', 'isolated', true)
        :config()
    g.cluster:sync(config_isolated)
    g.it:roundtrip("require('config'):reload()")
    g.it:roundtrip('box.info.ro_reason', 'isolated mode is enabled')

    -- Leaving isolated mode restores the manual failover reason.
    local config_not_isolated = cbuilder:new(config_isolated)
        :set_instance_option('i-002', 'isolated', nil)
        :config()
    g.cluster:sync(config_not_isolated)
    g.it:roundtrip("require('config'):reload()")
    g.it:roundtrip('box.info.ro_reason',
        'not the leader in manual failover mode')
    g.it:close()
    g.it = nil

    -- A restarted new leader is temporarily RO during safe startup. The final
    -- state is RW, so verify the transient reason in the log.
    g.cluster['i-002']:stop()
    local config_new_leader = cbuilder:new(config_not_isolated)
        :set_replicaset_option('leader', 'i-002')
        :config()
    g.cluster:sync(config_new_leader)
    g.cluster['i-002']:start()

    assert_ro_reason(g.cluster['i-002'], nil)
    t.assert(g.cluster['i-002']:grep_log('safe startup mode is enabled'))
end

-- Check forced RO when an instance does not participate in elections.
g.test_election_mode_off = function(g)
    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'election')
        :add_instance('i-001', {})
        :add_instance('i-002', {
            replication = {election_mode = 'off'},
        })
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-002']:exec(function()
        t.assert_equals(box.info.ro, true)
        t.assert_equals(box.cfg.ro_reason,
            'replication.election_mode is set to "off"')
        t.assert_equals(box.info.ro_reason, 'synchro')
    end)
end

-- Check supervised bootstrap reasons for existing and newly added replicas.
g.test_supervised_auto = function(g)
    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'supervised')
        :set_replicaset_option('replication.bootstrap_strategy', 'auto')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    assert_ro_reason(g.cluster['i-002'],
        'not the bootstrap leader in supervised failover mode')

    local config_upscaled = cbuilder:new(config)
        :add_instance('i-000', {})
        :config()
    g.cluster:sync(config_upscaled)
    g.cluster:start_instance('i-000')

    assert_ro_reason(g.cluster['i-000'],
        'joined an existing replicaset as a replica')
end

local g_external = t.group('external', {
    {strategy = 'supervised'},
    {strategy = 'native'},
})

g_external.after_each(cleanup)

-- Check RO while an external coordinator controls instance writability.
g_external.test_wait_for_coordinator = function(g)
    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'supervised')
        :set_replicaset_option('replication.bootstrap_strategy',
            g.params.strategy)
        :add_instance('i-001', {})
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start({wait_until_ready = false})

    -- The instance stays RO waiting for the coordinator, so its control socket
    -- may appear with a delay. Retry until the console connection succeeds.
    t.helpers.retrying({timeout = 60}, function()
        g.it = it.connect(g.cluster['i-001'])
    end)
    g.it:roundtrip([[
        require('fiber').new(function()
            box.ctl.make_bootstrap_leader({graceful = true})
        end)
    ]])
    t.helpers.retrying({timeout = 60}, function()
        local status = g.it:roundtrip("require('config'):info().status")
        t.assert_equals(status, 'ready')
    end)
    g.it:roundtrip('box.info.ro', true)
    g.it:roundtrip('box.info.ro_reason',
        'waiting for an external failover coordinator')
end
