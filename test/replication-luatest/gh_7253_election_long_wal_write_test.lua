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

g.before_all(function(g)
    g.cluster = cluster:new({})
    local box_cfg = {
        replication_synchro_timeout = 1000,
        replication_synchro_quorum = 2,
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('server1'),
            server.build_listen_uri('server2'),
            server.build_listen_uri('server3'),
        },
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
    wait_fullmesh(g)
end
