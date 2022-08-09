local t = require('luatest')
local server = require('test.luatest_helpers.server')
local cluster = require('test.luatest_helpers.cluster')
local fiber = require('fiber')

local wait_timeout = 50

local g = t.group('gh-7253')

local function wait_pair_sync(server1, server2)
    -- Without retrying it fails sometimes when vclocks are empty and both
    -- instances are in 'connect' state instead of 'follow'.
    t.helpers.retrying({timeout = wait_timeout}, function()
        server1:wait_vclock_of(server2)
        server2:wait_vclock_of(server1)
        server1:assert_follows_upstream(server2:instance_id())
        server2:assert_follows_upstream(server1:instance_id())
    end)
end

local function wait_fullmesh(g)
    wait_pair_sync(g.server1, g.server2)
end

local function block_next_wal_write_f()
    box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
end

local function unblock_wal_write_f()
    box.error.injection.set('ERRINJ_WAL_DELAY', false)
end

local function server_block_next_wal_write(server)
    server:exec(block_next_wal_write_f)
end

local function server_unblock_wal_write(server)
    server:exec(unblock_wal_write_f)
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
            server.build_instance_uri('server1'),
            server.build_instance_uri('server2'),
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
end)

g.test_fence_during_confirm_wal_write = function(g)
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
    local term = g.server1:election_term()
    fiber.create(g.server2.exec, g.server2, function()
        box.cfg{
            election_mode = 'manual',
            election_timeout = 1000,
        }
        -- Silence any exceptions. Only term bump matters and it is checked
        -- below.
        pcall(box.ctl.promote)
    end)
    g.server1:wait_election_term(term + 1)
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
    g.server1:exec(function()
        box.ctl.promote()
    end)
    wait_fullmesh(g)
end
