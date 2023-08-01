local t = require('luatest')
local server = require('luatest.server')
local replicaset = require('luatest.replica_set')

local g = t.group('gh-6860')

local function replicaset_create(g)
    g.replicaset = replicaset:new({})
    local box_cfg = {
        replication_timeout = 0.1,
        replication_synchro_quorum = 2,
        replication_synchro_timeout = 1000,
        replication = {
            server.build_listen_uri('server1', g.replicaset.id),
            server.build_listen_uri('server2', g.replicaset.id),
        },
    }
    g.server1 = g.replicaset:build_and_add_server({
        alias = 'server1', box_cfg = box_cfg
    })
    -- For stability. To guarantee server-1 is first, server-2 is second.
    box_cfg.read_only = true
    g.server2 = g.replicaset:build_and_add_server({
        alias = 'server2', box_cfg = box_cfg
    })
    g.replicaset:start()
    g.server2:update_box_cfg{read_only = false}
end

local function replicaset_drop(g)
    g.replicaset:drop()
    g.server1 = nil
    g.server2 = nil
end

g.before_all(replicaset_create)
g.after_all(replicaset_drop)

g.after_each(function(g)
    local function restore()
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 1000,
            election_mode = box.NULL,
        }
        box.ctl.demote()
    end
    g.server1:exec(restore)
    -- If server-1 started demote but it is not delivered to server-2 yet, then
    -- server-2 might start a concurrent one and fail to finish it due to term
    -- clash. Need to wait.
    g.server2:wait_for_vclock_of(g.server1)
    g.server2:exec(restore)
    g.server1:wait_for_vclock_of(g.server2)
end)

local function check_synchro_owner(server, owner_id)
    server:exec(function(owner_id)
        t.assert_equals(box.info.synchro.queue.owner, owner_id)
    end, {owner_id})
end

local function check_is_ro(server, value)
    server:exec(function(value)
        t.assert_equals(box.info.ro, value)
    end, {value})
end

--
-- Demote in off-mode disowns the synchro queue if it belongs to this instance.
-- Regardless of the queue term.
--
g.test_election_off_demote_self_no_leader = function(g)
    g.server2:update_box_cfg{election_mode = 'manual'}
    g.server1:exec(function()
        box.cfg{election_mode = 'manual'}
        box.ctl.promote()
    end)
    -- Wait the queue ownership to be persisted to check it below reliably.
    g.server1:wait_for_synchro_queue_term(g.server1:get_election_term())
    g.server1:exec(function()
        box.ctl.demote()
        box.cfg{election_mode = 'off'}
        local info = box.info
        -- Demote in the manual mode doesn't disown the queue. It would make no
        -- sense because the instance won't be writable unless it is the leader
        -- anyway.
        t.assert_lt(info.synchro.queue.owner, info.election.term)
        -- In off-mode the ownership is dropped. The idea is exactly to become
        -- writable. Election state won't interfere if there is no leader.
        box.ctl.demote()
    end)
    g.server2:wait_for_synchro_queue_term(g.server1:get_election_term())
    check_synchro_owner(g.server1, 0)
    check_is_ro(g.server1, false)
    check_synchro_owner(g.server2, 0)
    -- Server-2 is still in the manual mode. Hence read-only.
    check_is_ro(g.server2, true)
end

--
-- Demote in off-mode disowns the synchro queue even if it belongs to another
-- instance in a term < current one. And there can't be an election leader in
-- sight.
--
g.test_election_off_demote_other_no_leader = function(g)
    g.server1:update_box_cfg{election_mode = 'manual'}
    g.server2:update_box_cfg{election_mode = 'manual'}
    g.server1:exec(function()
        box.ctl.promote()
    end)
    -- Server-2 sees that the queue is owned by server-1.
    g.server2:wait_for_synchro_queue_term(g.server1:get_election_term())
    g.server1:exec(function()
        box.ctl.demote()
        box.cfg{election_mode = 'off'}
    end)
    -- Server-2 sees that server-1 is no longer a leader. But the queue still
    -- belongs to the latter.
    g.server2:wait_for_election_term(g.server1:get_election_term())
    g.server2:exec(function(owner_id)
        t.assert_equals(box.info.synchro.queue.owner, owner_id)
        box.cfg{election_mode = 'off'}
        box.ctl.demote()
    end, {g.server1:get_instance_id()})
    g.server1:wait_for_synchro_queue_term(g.server2:get_election_term())
    check_synchro_owner(g.server1, 0)
    check_is_ro(g.server1, false)
    check_synchro_owner(g.server2, 0)
    check_is_ro(g.server2, false)
end

--
-- Demote in off-mode won't do anything if the queue is owned by another
-- instance in the current term.
--
g.test_election_off_demote_other_same_term = function(g)
    g.server1:update_box_cfg{election_mode = 'manual'}
    g.server2:update_box_cfg{election_mode = 'manual'}
    g.server1:exec(function()
        box.ctl.promote()
    end)
    -- Server-2 sees that the queue is owned by server-1.
    g.server2:wait_for_synchro_queue_term(g.server1:get_election_term())
    g.server1:exec(function()
        box.cfg{election_mode = 'off'}
    end)
    -- Server-2 sees that server-1 is no longer a leader. But the queue still
    -- belongs to the latter in the current term.
    t.helpers.retrying({}, g.server2.exec, g.server2, function()
        if box.info.election.leader ~= 0 then
            error('Leader did not resign')
        end
    end)
    t.assert_equals(g.server2:get_election_term(),
                    g.server1:get_election_term())
    local owner_id = g.server1:get_instance_id()
    g.server2:exec(function(owner_id)
        local info = box.info
        t.assert_equals(info.synchro.queue.owner, owner_id)
        t.assert_equals(info.synchro.queue.term, info.election.term)
        box.cfg{election_mode = 'off'}
        box.ctl.demote()
    end, {owner_id})
    check_synchro_owner(g.server1, owner_id)
    check_is_ro(g.server1, false)
    check_synchro_owner(g.server2, owner_id)
    check_is_ro(g.server2, true)
end

--
-- Demote in off-mode fails if there is an election leader in sight. Off-mode
-- only makes sense if all the instances in the replicaset use it. If there is a
-- leader, then apparently someone is still in non-off-mode.
--
g.test_election_off_demote_other_leader = function(g)
    g.server1:update_box_cfg{election_mode = 'manual'}
    g.server2:update_box_cfg{election_mode = 'manual'}
    g.server1:exec(function()
        box.ctl.promote()
    end)
    local election_term = g.server1:get_election_term()
    g.server2:wait_for_synchro_queue_term(election_term)
    g.server2:exec(function()
        box.cfg{election_mode = 'off'}
        t.assert_error_msg_contains('The instance is not a leader',
                                    box.ctl.demote)
    end)
    local leader_id = g.server1:get_instance_id()
    -- Term wasn't bumped.
    t.assert_equals(election_term, g.server1:get_election_term())
    check_synchro_owner(g.server1, leader_id)
    check_is_ro(g.server1, false)
    check_synchro_owner(g.server2, leader_id)
    check_is_ro(g.server2, true)
end
