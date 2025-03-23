local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

-- Shortcut to make test cases more readable.
local function wait(f, ...)
    return t.helpers.retrying({timeout = 60}, f, ...)
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
