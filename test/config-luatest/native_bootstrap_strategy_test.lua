local fiber = require('fiber')
local net_box = require('net.box')
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local it = require('test.interactive_tarantool')

local g = t.group()

g.after_each(function(g)
    if g.it ~= nil then
        g.it:close()
    end
end)

-- Shortcut to make test cases more readable.
local function wait(f, ...)
    return t.helpers.retrying({timeout = 60}, f, ...)
end

local function admin(g, server, command)
    g.it = it.connect(server)
    g.it:roundtrip(command)
    g.it:close()
end

-- {{{ Verify database mode / bootstrap leader

local function verify_database_mode(server, mode)
    assert(server ~= nil)
    assert(mode == 'ro' or mode == 'rw')
    server:exec(function(mode)
        t.assert_equals(box.info.ro, mode == 'ro')
    end, {mode})
end

local function verify_cluster_mode(cluster, exp_mode_list)
    local i = 1
    cluster:each(function(server)
        verify_database_mode(server, exp_mode_list[i])
        i = i + 1
    end)
    t.assert_equals(i - 1, #exp_mode_list)
end

local function find_rw(cluster)
    local leader
    cluster:each(function(server)
        local ok, info = pcall(server.call, server, 'box.info')
        if ok and not info.ro then
            t.assert_equals(leader, nil)
            leader = info.name
        end
    end)
    t.assert_not_equals(leader, nil)
    return leader
end

local function verify_bootstrap_leader(cluster, exp_leader)
    local uuid2name = {}
    local leader_uuid
    cluster:each(function(server)
        if server.process == nil then
            return
        end

        local uuid, name, leader_uuid_tmp = server:exec(function()
            local leader_uuid_tmp =
                box.space._schema:get({'bootstrap_leader_uuid'})[2]
            return box.info.uuid, box.info.name, leader_uuid_tmp
        end)

        if leader_uuid == nil then
            leader_uuid = leader_uuid_tmp
        else
            t.assert_equals(leader_uuid_tmp, leader_uuid)
        end

        uuid2name[uuid] = name
    end)

    t.assert_not_equals(leader_uuid, nil)
    local leader = uuid2name[leader_uuid]
    t.assert_not_equals(leader, nil)

    if type(exp_leader) == 'string' then
        t.assert_equals(leader, exp_leader)
    elseif type(exp_leader) == 'table' then
        t.assert_items_include(exp_leader, {leader})
    else
        assert(false)
    end

    return leader
end

local function verify_initial_bootstrap_leader(cluster, exp_leader)
    local leader
    cluster:each(function(server)
        local info = server:call('box.info')
        if info.id == 1 then
            t.assert_equals(leader, nil)
            leader = info.name
        end
    end)
    t.assert_not_equals(leader, nil)
    t.assert_equals(leader, exp_leader)
end

-- }}} Verify database mode / bootstrap leader

g.test_failover_off_singlemaster = function()
    local config = cbuilder:new()
        :use_replicaset('r-001')
        :set_replicaset_option('replication.failover', 'off')
        :set_replicaset_option('replication.bootstrap_strategy', 'native')
        :add_instance('i-001', {database = {mode = 'rw'}})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    -- Bootstrap.
    local cluster = cluster:new(config)
    cluster:start()
    verify_cluster_mode(cluster, {'rw', 'ro', 'ro'})
    verify_bootstrap_leader(cluster, 'i-001')

    -- Change leader.
    local config_2 = cbuilder:new(config)
        :use_replicaset('r-001')
        :set_instance_option('i-001', 'database.mode', nil)
        :set_instance_option('i-002', 'database.mode', 'rw')
        :config()
    cluster:reload(config_2)
    verify_cluster_mode(cluster, {'ro', 'rw', 'ro'})
    verify_bootstrap_leader(cluster, 'i-002')

    -- Reload config w/o leadership changes.
    cluster:reload()
    verify_cluster_mode(cluster, {'ro', 'rw', 'ro'})
    verify_bootstrap_leader(cluster, 'i-002')

    -- Join a replica.
    local config_3 = cbuilder:new(config_2)
        :use_replicaset('r-001')
        :add_instance('i-004', {})
        :config()
    cluster:sync(config_3)
    cluster:start_instance('i-004')
    verify_cluster_mode(cluster, {'ro', 'rw', 'ro', 'ro'})
    verify_bootstrap_leader(cluster, 'i-002')
end

g.test_failover_off_multimaster = function()
    local config = cbuilder:new()
        :use_replicaset('r-001')
        :set_replicaset_option('replication.failover', 'off')
        :set_replicaset_option('replication.bootstrap_strategy', 'native')
        :add_instance('i-001', {})
        :add_instance('i-002', {database = {mode = 'rw'}})
        :add_instance('i-003', {})
        :add_instance('i-004', {database = {mode = 'rw'}})
        :add_instance('i-005', {})
        :config()

    -- Bootstrap.
    local cluster = cluster:new(config)
    cluster:start()
    verify_cluster_mode(cluster, {'ro', 'rw', 'ro', 'rw', 'ro'})
    wait(verify_bootstrap_leader, cluster, {'i-002', 'i-004'})
    verify_initial_bootstrap_leader(cluster, 'i-002')

    -- Change leader.
    local config_2 = cbuilder:new(config)
        :use_replicaset('r-001')
        :set_instance_option('i-001', 'database.mode', 'rw')
        :set_instance_option('i-002', 'database.mode', 'ro')
        :set_instance_option('i-003', 'database.mode', 'rw')
        :set_instance_option('i-004', 'database.mode', 'ro')
        :set_instance_option('i-005', 'database.mode', 'rw')
        :config()
    cluster:reload(config_2)
    verify_cluster_mode(cluster, {'rw', 'ro', 'rw', 'ro', 'rw'})
    wait(verify_bootstrap_leader, cluster, {'i-001', 'i-003', 'i-005'})

    -- Reload config w/o leadership changes.
    cluster:reload()
    verify_cluster_mode(cluster, {'rw', 'ro', 'rw', 'ro', 'rw'})
    wait(verify_bootstrap_leader, cluster, {'i-001', 'i-003', 'i-005'})

    -- Join a replica.
    local config_3 = cbuilder:new(config_2)
        :use_replicaset('r-001')
        :add_instance('i-006', {})
        :config()
    cluster:sync(config_3)
    cluster:start_instance('i-006')
    verify_cluster_mode(cluster, {'rw', 'ro', 'rw', 'ro', 'rw', 'ro'})
    local leader = verify_bootstrap_leader(cluster, {'i-001', 'i-003', 'i-005'})

    -- Ensure that all the instances have the actual
    -- configuration. Otherwise the next scenario may restart
    -- upstream connections after we stop one of the instances
    -- and wait for replication.connect_timeout.
    cluster:reload()

    -- If the bootstrap leader goes off or becomes inaccessible,
    -- other RW instance doesn't take the bootstrap leader role
    -- automatically (unlike the 'auto' strategy).
    --
    -- It means that new (non-anonymous) replicas can't register
    -- in the replicaset.
    --
    -- The next scenario shows how to repair this situation by
    -- assigning another RW instance as a bootstrap leader.

    -- Stop the bootstrap leader.
    local old_leader = leader
    cluster[old_leader]:stop()

    -- Now we have to repair the replicaset. Let's choose the next
    -- bootstrap leader across remaining RW instances.
    local new_leader = ({
        ['i-001'] = 'i-003',
        ['i-003'] = 'i-005',
        ['i-005'] = 'i-001',
    })[old_leader]

    -- Switch new_leader mode: RW -> RO -> RW.
    local config_4 = cbuilder:new(config_3)
        :use_replicaset('r-001')
        :set_instance_option(new_leader, 'database.mode', 'ro')
        :config()
    cluster:sync(config_4)
    cluster[new_leader]:exec(function()
        require('config'):reload()
    end)
    local config_5 = cbuilder:new(config_4)
        :use_replicaset('r-001')
        :set_instance_option(new_leader, 'database.mode', 'rw')
        :config()
    cluster:sync(config_5)
    cluster[new_leader]:exec(function()
        require('config'):reload()
    end)

    -- Ensure that now it is the bootstrap leader.
    wait(verify_bootstrap_leader, cluster, new_leader)
end

g.test_failover_manual = function()
    local config = cbuilder:new()
        :use_replicaset('r-001')
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('replication.bootstrap_strategy', 'native')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    -- Bootstrap.
    local cluster = cluster:new(config)
    cluster:start()
    verify_cluster_mode(cluster, {'rw', 'ro', 'ro'})
    verify_bootstrap_leader(cluster, 'i-001')

    -- Change leader.
    local config_2 = cbuilder:new(config)
        :use_replicaset('r-001')
        :set_replicaset_option('leader', 'i-002')
        :config()
    cluster:reload(config_2)
    verify_cluster_mode(cluster, {'ro', 'rw', 'ro'})
    verify_bootstrap_leader(cluster, 'i-002')

    -- Reload config w/o leadership changes.
    cluster:reload()
    verify_cluster_mode(cluster, {'ro', 'rw', 'ro'})
    verify_bootstrap_leader(cluster, 'i-002')

    -- Join a replica.
    local config_3 = cbuilder:new(config_2)
        :use_replicaset('r-001')
        :add_instance('i-004', {})
        :config()
    cluster:sync(config_3)
    cluster:start_instance('i-004')
    verify_cluster_mode(cluster, {'ro', 'rw', 'ro', 'ro'})
    verify_bootstrap_leader(cluster, 'i-002')
end

g.test_failover_election = function()
    local config = cbuilder:new()
        :use_replicaset('r-001')
        :set_replicaset_option('replication.failover', 'election')
        :set_replicaset_option('replication.bootstrap_strategy', 'native')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    -- Verify the test case predicate: there is one RW instance
    -- and it is a bootstrap leader.
    --
    -- We can see some intermediate states:
    --
    -- * there is no RW instance at the moment
    -- * the new RW instance is promoted, but the new bootstrap
    --   leader record is not written yet
    --
    -- So, the predicate is implemented with retrying.
    local function verify(cluster)
        return wait(function()
            local leader = find_rw(cluster)
            verify_bootstrap_leader(cluster, leader)
            return leader
        end)
    end

    -- Bootstrap.
    local cluster = cluster:new(config)
    cluster:start()
    local initial_leader = verify(cluster)

    -- Stop the RW node and wait for another RW.
    cluster[initial_leader]:stop()
    verify(cluster)

    -- Reload config.
    cluster:each(function(server)
        if server.process == nil then
            return
        end

        server:exec(function()
            require('config'):reload()
        end)
    end)
    verify(cluster)

    -- Join a replica.
    local config_2 = cbuilder:new(config)
        :use_replicaset('r-001')
        :add_instance('i-004', {})
        :config()
    cluster:sync(config_2)
    cluster:start_instance('i-004')
    verify(cluster)
end

-- Run the same test for replication.bootstrap_strategy =
-- 'supervised' and 'native', because we expect the same behavior.
local g_supervised_and_native = t.group('supervised_and_native', {
    {bootstrap_strategy = 'supervised'},
    {bootstrap_strategy = 'native'},
})

-- Just ensure that no leadership management actions are performed
-- by the instance itself.
--
-- An external agent (the failover coordinator) is responsible for
-- them.
g_supervised_and_native.test_failover_supervised = function(g)
    local strategy = g.params.bootstrap_strategy

    local config = cbuilder:new()
        :use_replicaset('r-001')
        :set_replicaset_option('replication.failover', 'supervised')
        :set_replicaset_option('replication.bootstrap_strategy', strategy)
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    -- No bootstrap, wait for the coordinator.
    local cluster = cluster:new(config)
    cluster:start({wait_until_ready = false})

    -- Give the instances a chance to perform a replicaset
    -- bootstrap. It shouldn't occur and this is the idea of the
    -- test.
    fiber.sleep(1)

    cluster:each(function(server)
        -- No bootstrapped database, so no privileges granted.
        --
        -- Let's use a watch request that doesn't need any
        -- permissions.
        local uri = server.net_box_uri
        local status = wait(function()
            local conn = net_box.connect(uri, {fetch_schema = false})
            return conn:watch_once('box.status')
        end)
        -- Verify that we still in the 'loading' status, not
        -- 'running'. Also, this way we ensure that the process
        -- is alive and waits for the agent's command.
        t.assert_type(status, 'table')
        t.assert_equals(status.status, 'loading')
    end)

    -- Imitate the external agent using the admin socket.
    admin(g, cluster['i-001'], [[
        require('fiber').new(function()
            box.ctl.make_bootstrap_leader({graceful = true})
            box.cfg({read_only = false})
        end)
    ]])
    cluster:each(function(server)
        server:wait_until_ready()
    end)
    verify_cluster_mode(cluster, {'rw', 'ro', 'ro'})
    verify_bootstrap_leader(cluster, 'i-001')

    -- Reconfiguration doesn't drop the RW mode to RO.
    cluster['i-001']:exec(function()
        local config = require('config')

        config:reload()
        t.assert_equals(box.info.ro, false)
    end)
end

-- Same as the previous test case, but cover a replicaset with one
-- instance.
g_supervised_and_native.test_failover_supervised_singleton = function(g)
    local strategy = g.params.bootstrap_strategy

    local config = cbuilder:new()
        :use_replicaset('r-001')
        :set_replicaset_option('replication.failover', 'supervised')
        :set_replicaset_option('replication.bootstrap_strategy', strategy)
        :add_instance('i-001', {})
        :config()

    -- No bootstrap, wait for the coordinator.
    local cluster = cluster:new(config)
    cluster:start({wait_until_ready = false})

    -- Give the instances a chance to perform a replicaset
    -- bootstrap. It shouldn't occur and this is the idea of the
    -- test.
    fiber.sleep(1)

    cluster:each(function(server)
        -- No bootstrapped database, so no privileges granted.
        --
        -- Let's use a watch request that doesn't need any
        -- permissions.
        local uri = server.net_box_uri
        local status = wait(function()
            local conn = net_box.connect(uri, {fetch_schema = false})
            return conn:watch_once('box.status')
        end)
        -- Verify that we still in the 'loading' status, not
        -- 'running'. Also, this way we ensure that the process
        -- is alive and waits for the agent's command.
        t.assert_type(status, 'table')
        t.assert_equals(status.status, 'loading')
    end)

    -- Imitate the external agent using the admin socket.
    admin(g, cluster['i-001'], [[
        require('fiber').new(function()
            box.ctl.make_bootstrap_leader({graceful = true})
            box.cfg({read_only = false})
        end)
    ]])
    cluster:each(function(server)
        server:wait_until_ready()
    end)
    verify_cluster_mode(cluster, {'rw'})
    verify_bootstrap_leader(cluster, 'i-001')

    -- Reconfiguration doesn't drop the RW mode to RO.
    cluster['i-001']:exec(function()
        local config = require('config')

        config:reload()
        t.assert_equals(box.info.ro, false)
    end)
end
