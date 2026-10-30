local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('replica1', cg.replica_set.id),
            server.build_listen_uri('replica2', cg.replica_set.id),
            server.build_listen_uri('replica3', cg.replica_set.id),
        },
        election_mode = 'candidate',
        election_timeout = 0.4,
        election_fencing_mode = 'off',
        replication_timeout = 1,
         -- To reveal more election logs.
        log_level = 6,
    }
    local replica1 = cg.replica_set:build_and_add_server{
        alias = 'replica1',
        box_cfg = box_cfg,
    }
    cg.replica_set:build_and_add_server{
        alias = 'replica2',
        box_cfg = box_cfg,
    }
    cg.replica_set:build_and_add_server{
        alias = 'replica3',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    replica1:wait_until_election_leader_found()
    local leader = cg.replica_set:get_leader()
    t.assert_not_equals(leader, nil)
    leader:exec(function()
        box.ctl.wait_rw()
        box.schema.space.create('s', {is_sync = true}):create_index('p')
    end)
    for _, srv in ipairs(cg.replica_set.servers) do
        if leader ~= srv then
            leader:wait_for_downstream_to(srv)
        end
    end
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_stress_election_qsync = function(cg)
    local leader = cg.replica_set:get_leader()
    t.assert_not_equals(leader, nil)
    for i = 1, 10 do
        leader:exec(function(j)
            box.cfg{replication_synchro_quorum = 4,
                    replication_synchro_timeout = 1000}
            local lsn = box.info.lsn
            box.atomic({wait = 'submit'}, function()
                box.space.s:insert{j}
            end)
            t.helpers.retrying({timeout = 120}, function()
                t.assert_equals(box.info.lsn, lsn + 1)
            end)
        end, {i})
        local leader_idx = nil
        for j, srv in ipairs(cg.replica_set.servers) do
            if leader ~= srv then
                leader:wait_for_downstream_to(srv)
            else
                leader_idx = j
            end
        end
        local old_leader_id = leader:get_instance_id()
        leader:stop()

        local follower1 = cg.replica_set.servers[leader_idx % 3 + 1]
        local follower2 = cg.replica_set.servers[(leader_idx + 1) % 3 + 1]
        follower1:exec(function(old_leader_id)
            t.helpers.retrying({timeout = 120}, function()
                t.assert_not_equals(box.info.election.leader, 0)
                t.assert_not_equals(box.info.election.leader, old_leader_id)
            end)
        end, {old_leader_id})
        local new_leader = follower1:exec(function()
            return box.info.election.state == 'leader'
        end) and follower1 or follower2
        t.assert_not_equals(new_leader, leader)
        local old_leader = leader
        leader = new_leader
        leader:exec(function(j)
            box.cfg{replication_synchro_timeout = 1000}
            box.ctl.wait_rw()
            t.assert_equals(box.info.synchro.queue.len, 0)
            t.assert_not_equals(box.space.s:get{j}, nil)
        end, {i})
        old_leader:restart()
        cg.replica_set:wait_for_fullmesh()
        leader:wait_for_downstream_to(old_leader)
    end
    leader:exec(function()
        t.assert_equals(box.space.s:count(), 10)
    end)
end
