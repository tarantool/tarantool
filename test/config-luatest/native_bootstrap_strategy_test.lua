local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

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
    t.assert_equals(leader, exp_leader)
end

-- }}} Verify database mode / bootstrap leader

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
