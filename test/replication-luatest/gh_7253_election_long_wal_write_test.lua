local t = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')
local fiber = require('fiber')

local wait_timeout = 50

local g = t.group('gh-7253')

local function wait_pair_sync(server1, server2)
    -- Without retrying it fails sometimes when vclocks are empty and both
    -- instances are in 'connect' state instead of 'follow'.
    t.helpers.retrying({timeout = wait_timeout}, function()
        server1:wait_for_vclock_of(server2)
        server2:wait_for_vclock_of(server1)
        server1:assert_follows_upstream(server2:get_instance_id())
        server2:assert_follows_upstream(server1:get_instance_id())
    end)
end

local function wait_fullmesh(g)
    wait_pair_sync(g.server1, g.server2)
    wait_pair_sync(g.server2, g.server3)
    wait_pair_sync(g.server3, g.server1)
end

local function block_next_wal_write_f()
    box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
end

local function unblock_wal_write_f()
    box.error.injection.set('ERRINJ_WAL_DELAY', false)
end

local function get_election_state_f()
    return box.info.election.state
end

local function server_block_next_wal_write(server)
    server:exec(block_next_wal_write_f)
end

local function server_unblock_wal_write(server)
    server:exec(unblock_wal_write_f)
end

local function server_get_election_state(server)
    return server:exec(get_election_state_f)
end

local function check_wal_is_blocked_f()
    if not box.error.injection.get('ERRINJ_WAL_DELAY') then
        error('WAL still is not paused')
    end
end

local function server_wait_wal_is_blocked(server)
    t.helpers.retrying({timeout = wait_timeout}, server.exec, server,
                       check_wal_is_blocked_f)
end

local function server_set_replication(server, replication)
    server:exec(function(replication)
        box.cfg{replication = replication}
    end, {replication})
end

g.before_all(function(g)
    g.cluster = cluster:new({})
    local box_cfg = {
        replication_synchro_timeout = 1000,
        replication_synchro_quorum = 2,
        replication_timeout = 0.1,
        election_timeout = 1000,
        election_fencing_enabled = false,
        replication = {
            server.build_listen_uri('server1'),
            server.build_listen_uri('server2'),
            server.build_listen_uri('server3'),
        },
        bootstrap_strategy = 'legacy',
    }
    box_cfg.election_mode = 'manual'
    g.server1 = g.cluster:build_and_add_server({
        alias = 'server1', box_cfg = box_cfg
    })
    box_cfg.election_mode = 'voter'
    g.server2 = g.cluster:build_and_add_server({
        alias = 'server2', box_cfg = box_cfg
    })
    g.server3 = g.cluster:build_and_add_server({
        alias = 'server3', box_cfg = box_cfg
    })
    g.cluster:start()

    g.server1:exec(function()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
    end)
    wait_fullmesh(g)
end)

g.after_all(function(g)
    g.cluster:drop()
    g.server1 = nil
    g.server2 = nil
    g.server3 = nil
end)

g.test_fence_during_confirm_wal_write = function(g)
    -- Prevent server3 intervention.
    server_block_next_wal_write(g.server3)
    --
    -- Server1 starts a txn.
    --
    server_block_next_wal_write(g.server2)
    local f = fiber.new(g.server1.exec, g.server1, function()
        box.space.test:replace({1})
    end)
    f:set_joinable(true)
    --
    -- The txn is delivered to server2.
    --
    server_wait_wal_is_blocked(g.server2)
    --
    -- Server1 receives an ack and blocks on CONFIRM WAL write.
    --
    server_block_next_wal_write(g.server1)
    server_unblock_wal_write(g.server2)
    server_wait_wal_is_blocked(g.server1)
    --
    -- Server2 sends a new term to server1.
    --
    local term = g.server1:get_election_term()
    fiber.create(g.server2.exec, g.server2, function()
        box.cfg{
            election_mode = 'manual',
            election_timeout = 1000,
        }
        -- Silence any exceptions. Only term bump matters and it is checked
        -- below.
        pcall(box.ctl.promote)
    end)
    g.server1:wait_for_election_term(term + 1)
    --
    -- Server1 finishes CONFIRM WAL write and sees that the synchro queue was
    -- frozen during the WAL write. Shouldn't affect the result.
    --
    server_unblock_wal_write(g.server1)
    local ok, err = f:join(wait_timeout)
    t.assert_equals(err, nil)
    t.assert(ok)
    t.assert(g.server1:exec(function()
        return box.space.test:get{1} ~= nil
    end))
    --
    -- Cleanup.
    --
    g.server2:exec(function()
        box.cfg{
            election_mode = 'voter',
            election_timeout = box.NULL,
        }
    end)
    server_wait_wal_is_blocked(g.server3)
    server_unblock_wal_write(g.server3)
    g.server1:exec(function()
        box.ctl.promote()
    end)
    g.server1:wait_for_election_leader()
    g.server1:exec(function()
        box.space.test:truncate()
    end)
    wait_fullmesh(g)
end

g.test_vote_during_txn_wal_write = function(g)
    --
    -- Make the following topology:
    --   server1 <-> server2 <-> server3
    --
    g.server1:exec(function()
        local replication = table.copy(box.cfg.replication)
        rawset(_G, 'old_replication', table.copy(replication))
        table.remove(replication, 3)
        box.cfg{replication = replication}
    end)
    g.server3:exec(function()
        local replication = table.copy(box.cfg.replication)
        rawset(_G, 'old_replication', table.copy(replication))
        table.remove(replication, 1)
        box.cfg{replication = replication}
    end)
    --
    -- Server2 gets a foreign txn going to WAL too long.
    --
    server_block_next_wal_write(g.server2)
    local f = fiber.new(g.server1.exec, g.server1, function()
        box.space.test:replace({1})
    end)
    f:set_joinable(true)
    server_wait_wal_is_blocked(g.server2)
    --
    -- Server3 tries to become a leader by requesting a vote from server2.
    --
    local term = g.server2:get_election_term()
    fiber.create(g.server3.exec, g.server3, function()
        box.cfg{
            election_mode = 'manual',
            election_timeout = 1000,
        }
        pcall(box.ctl.promote)
    end)
    g.server2:wait_for_election_term(term + 1)
    --
    -- Server2 shouldn't have persisted a vote yet. Instead, when it finishes
    -- the txn WAL write, it sees that its vclock is > server3's one and it
    -- cancels the vote.
    --
    server_unblock_wal_write(g.server2)
    --
    -- Server1 gets the new term via server2.
    --
    g.server1:wait_for_election_term(term + 1)
    g.server3:wait_for_vclock_of(g.server2)
    t.assert_equals(server_get_election_state(g.server1), 'follower')
    t.assert_equals(server_get_election_state(g.server2), 'follower')
    t.assert_not_equals(server_get_election_state(g.server3), 'leader')
    -- Restore server3 original params.
    g.server3:exec(function()
        box.cfg{
            election_mode = 'voter',
            election_timeout = box.NULL,
        }
    end)
    -- Restore server1 leadership in the new term.
    g.server1:exec(function()
        box.ctl.promote()
    end)
    --
    -- Server1's txn ends fine. Server3 wasn't able to roll it back via own
    -- PROMOTE.
    --
    local ok, err = f:join(wait_timeout)
    t.assert_equals(err, nil)
    t.assert(ok)
    t.assert(g.server1:exec(function()
        return box.space.test:get{1} ~= nil
    end))
    --
    -- Cleanup.
    --
    g.server3:exec(function()
        box.cfg{replication = _G.old_replication}
        _G.old_replication = nil
    end)
    g.server1:exec(function()
        box.cfg{replication = _G.old_replication}
        _G.old_replication = nil
    end)
    g.server1:wait_for_election_leader()
    g.server1:exec(function()
        box.space.test:truncate()
    end)
    wait_fullmesh(g)
end

--
-- Old leader doesn't know about a new term, makes a sync transaction in the old
-- term. The transaction is not yet delivered anywhere. At the same time another
-- instance bumps the term, wins elections, starts writing PROMOTE.
--
-- The transaction from the old leader shouldn't be CONFIRMed by it. At least
-- one of the surrounding instances from quorum should respond "we ack this, but
-- there is a new term - you can't write CONFIRM".
--
-- New leader PROMOTE would rollback that txn eventually with a non-critical
-- error. Not split-brain.
--
g.test_old_leader_txn_during_promote_write = function(g)
    --
    -- Build the topology:
    --   server1  server2 <-> server3
    --
    local server2_uri = server.build_listen_uri('server2')
    local server3_uri = server.build_listen_uri('server3')
    server_set_replication(g.server1, {})
    server_set_replication(g.server2, {server2_uri, server3_uri})
    server_set_replication(g.server3, {server2_uri, server3_uri})
    --
    -- Server3 bumps raft term, but takes a long time to write PROMOTE.
    --
    server_block_next_wal_write(g.server3)
    local f_promote = fiber.new(g.server3.exec, g.server3, function()
        box.cfg{election_mode = 'manual'}
        box.ctl.promote()
    end)
    f_promote:set_joinable(true)
    g.server3:play_wal_until_synchro_queue_is_busy()
    local new_term, old_term = g.server3:exec(function()
        local info = box.info
        return info.election.term, info.synchro.queue.term
    end)
    t.assert_equals(old_term + 1, new_term)
    g.server2:wait_for_election_term(new_term)
    --
    -- Server1 doesn't see the new term yet and makes an attempt to do a sync
    -- transaction.
    --
    local f_insert = fiber.new(g.server1.exec, g.server1, function()
        box.space.test:replace{1}
    end)
    f_insert:set_joinable(true)
    --
    -- Restore connectivity server1 -> server2.
    --
    server_set_replication(g.server2, g.server2.box_cfg.replication)
    --
    -- Server2 gets the bad txn.
    --
    g.server1:wait_for_downstream_to(g.server2)
    t.assert_equals(g.server2:exec(function()
        return box.space.test:count()
    end), 1)
    --
    -- PROMOTE finally is written. The server1's transaction is rolled back
    -- gracefully and no split brain has happened.
    --
    server_unblock_wal_write(g.server3)
    local ok, err = f_promote:join()
    t.assert_equals(err, nil)
    t.assert(ok)
    g.server3:wait_for_election_leader()
    --
    -- Server1 gets the PROMOTE and fails its rogue txn.
    --
    server_set_replication(g.server1, g.server1.box_cfg.replication)
    ok, err = f_insert:join()
    t.assert_not_equals(err, nil)
    t.assert(not ok)
    t.assert_equals(g.server1:exec(function()
        return box.space.test:count()
    end), 0)
    --
    -- Cleanup.
    --
    server_set_replication(g.server3, g.server3.box_cfg.replication)
    g.server3:exec(function()
        box.cfg{election_mode = 'voter'}
    end)
    g.server1:exec(function()
        box.ctl.promote()
    end)
    g.server1:wait_for_election_leader()
    wait_fullmesh(g)
end

--
-- Similar to the test case without '_complex' suffix. But the transaction is
-- delivered to a conflicting node in an intricate way. Server1 - old leader,
-- server3 - new leader, server2 - between them.
--
-- In this test server2 gets the bad txn not directly from server1, but via
-- another instance (server4). Server4 gets the txn from server1, then gets
-- its term bumped by server2, then sends the txn to server2. This way server2
-- sees that the txn is coming from an instance having the new term.
--
-- The purpose is to ensure that server2 anyway later will send ACK with a new
-- term to server1. It wouldn't be enough to simply reject any txns from nodes
-- having an old term, because in this test server4 has a new term but still
-- forwards the bad txn.
--
g.test_old_leader_txn_during_promote_write_complex = function(g)
    local server1_uri = server.build_listen_uri('server1')
    local server2_uri = server.build_listen_uri('server2')
    local server3_uri = server.build_listen_uri('server3')
    local server4_uri = server.build_listen_uri('server4')
    local server5_uri = server.build_listen_uri('server5')
    --
    -- Build the topology:
    --   server4 <- fullmesh(server1, server2, server3)
    --   server3 <-> server5
    --
    g.server4 = g.cluster:build_and_add_server({
        alias = 'server4', box_cfg = g.server3.box_cfg
    })
    g.server4:start()
    -- Server5 is needed only to make server3 able to win elections without
    -- participation of server1. Could also lower the quorum, but it wouldn't be
    -- fair. The test is too complex for these tricks.
    g.server5 = g.cluster:build_and_add_server({
        alias = 'server5', box_cfg = g.server3.box_cfg
    })
    g.server5:start()
    server_set_replication(g.server5, {server3_uri})
    server_set_replication(g.server3, {server1_uri, server2_uri, server5_uri})
    for _, s in pairs({
        g.server1, g.server2, g.server3, g.server4, g.server5
    }) do
        s:exec(function()
            box.cfg{replication_synchro_quorum = 3}
        end)
    end
    --
    -- Build the topology:
    --   server1  server2 <-> server3 <-> server5
    --      V
    --   server4
    --
    server_set_replication(g.server1, {})
    server_set_replication(g.server2, {server3_uri})
    server_set_replication(g.server3, {server2_uri, server5_uri})
    server_set_replication(g.server4, {server1_uri})
    --
    -- Server3 bumps raft term, gets stuck writing PROMOTE to WAL.
    --
    local old_term = g.server3:get_election_term()
    server_block_next_wal_write(g.server3)
    local f_promote = fiber.new(g.server3.exec, g.server3, function()
        box.cfg{election_mode = 'manual'}
        box.ctl.promote()
    end)
    f_promote:set_joinable(true)
    g.server3:play_wal_until_synchro_queue_is_busy()
    local new_term = old_term + 1
    t.assert_equals(g.server2:get_election_term(), new_term)
    --
    -- Server1 doesn't see the new term yet and makes an attempt to do a sync
    -- transaction.
    --
    local lsn = g.server1:exec(function() return box.info.lsn end)
    local f_insert = fiber.new(g.server1.exec, g.server1, function()
        box.space.test:replace{1}
    end)
    f_insert:set_joinable(true)
    t.helpers.retrying({}, function()
        assert(g.server1:exec(function() return box.info.lsn end) > lsn)
    end)
    --
    -- Server4 gets the transaction.
    --
    g.server4:wait_for_vclock_of(g.server1)
    t.assert_equals(g.server4:exec(function()
        return box.space.test:count()
    end), 1)
    t.assert_equals(g.server4:get_election_term(), old_term)
    --
    -- Build the topology:
    --   server1  server2 <-> server3 <-> server5
    --              V
    --            server4
    --
    -- Server4 gets new term from server2.
    server_set_replication(g.server4, {server2_uri})
    g.server4:wait_for_election_term(new_term)
    --
    -- Build the topology:
    --   server1  server2 <-> server3 <-> server5
    --              ^
    --              v
    --            server4
    --
    -- Server2 gets bad txn from server4 which has a new term too.
    server_set_replication(g.server2, {server3_uri, server4_uri})
    g.server2:wait_for_vclock_of(g.server4)
    t.assert_equals(g.server2:exec(function()
        return box.space.test:count()
    end), 1)
    --
    -- Build the topology:
    --   server1 -> server2 <-> server3 <-> server5
    --
    server_set_replication(g.server4, {})
    server_set_replication(g.server2, {server1_uri, server3_uri})
    g.server1:wait_for_downstream_to(g.server2)
    --
    -- Server3 ends PROMOTE. All should be fine and dandy.
    --
    server_unblock_wal_write(g.server3)
    g.server3:wait_for_election_leader()
    --
    -- Restore the original topology.
    --
    server_set_replication(g.server5, {})
    server_set_replication(g.server1, g.server1.box_cfg.replication)
    server_set_replication(g.server2, g.server2.box_cfg.replication)
    server_set_replication(g.server3, g.server3.box_cfg.replication)
    wait_fullmesh(g)
    local ok, err = f_promote:join()
    t.assert_equals(err, nil)
    t.assert(ok)
    ok, err = f_insert:join()
    t.assert_not_equals(err, nil)
    t.assert(not ok)
    t.assert_equals(g.server1:exec(function()
        return box.space.test:count()
    end), 0)
    --
    -- Cleanup.
    --
    local server4_id = g.server4:get_instance_id()
    local server5_id = g.server5:get_instance_id()
    for _, s in pairs({g.server4, g.server5}) do
        s:drop()
        g.cluster:delete_server(s.alias)
        g[s.alias] = nil
    end
    g.server3:exec(function()
        box.cfg{
            replication_synchro_quorum = 2,
            election_mode = 'voter',
        }
    end)
    g.server2:exec(function()
        box.cfg{replication_synchro_quorum = 2}
    end)
    g.server1:exec(function()
        box.cfg{replication_synchro_quorum = 2}
        box.ctl.promote()
    end)
    g.server1:wait_for_election_leader()
    g.server1:exec(function(server4_id, server5_id)
        box.space._cluster:delete(server4_id)
        box.space._cluster:delete(server5_id)
    end, {server4_id, server5_id})
    wait_fullmesh(g)
end
