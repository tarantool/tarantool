local luatest = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')

local wait_timeout = 120

local g = luatest.group('gh-6842')

local function cluster_create(g)
    g.cluster = cluster:new({})
    local box_cfg = {
        replication_timeout = 0.1,
        replication_synchro_quorum = 2,
        replication_synchro_timeout = 1000,
        replication = {
            server.build_listen_uri('server_gh6842_1'),
            server.build_listen_uri('server_gh6842_2'),
        },
    }
    g.server1 = g.cluster:build_and_add_server({
        alias = 'server_gh6842_1', box_cfg = box_cfg
    })
    -- For stability. To guarantee server1 is first, server2 is second.
    box_cfg.read_only = true
    g.server2 = g.cluster:build_and_add_server({
        alias = 'server_gh6842_2', box_cfg = box_cfg
    })
    g.cluster:start()

    g.server2:exec(function()
        box.cfg{
            read_only = false
        }
    end)
end

local function cluster_drop(g)
    g.cluster:drop()
    g.server1 = nil
    g.server2 = nil
end

g.before_all(cluster_create)
g.after_all(cluster_drop)

g.after_each(function(g)
    -- Restore cluster state like it was on start.
    g.server1:exec(function()
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 1000,
        }
        box.ctl.demote()
    end)
    g.server2:exec(function()
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 1000,
        }
        box.ctl.demote()
        if box.space.test then
            box.space.test:drop()
        end
    end)
    g.server1:wait_for_vclock_of(g.server2)
    g.server2:wait_for_vclock_of(g.server1)
end)

--
-- Wait until the server sees synchro queue owner as the given ID.
--
local function wait_synchro_owner(server, owner_id)
    luatest.helpers.retrying({timeout = wait_timeout}, server.exec, server,
                             function(id)
        if box.info.synchro.queue.owner ~= id then
            error('Waiting for queue transition')
        end
    end, {owner_id})
end

--
-- Wait until the server sees synchro queue has the given length.
--
local function wait_synchro_len(server, len_arg)
    luatest.helpers.retrying({timeout = wait_timeout}, server.exec, server,
                             function(len)
        if box.info.synchro.queue.len ~= len then
            error('Waiting for queue len')
        end
    end, {len_arg})
end

--
-- Wait until the server sees synchro queue as busy.
--
local function wait_synchro_is_busy(server)
    luatest.helpers.retrying({timeout = wait_timeout}, server.exec, server,
                             function()
        if not box.info.synchro.queue.busy then
            error('Waiting for busy queue')
        end
    end)
end

--
-- Wait until the server sees the given upstream status for a specified
-- replica ID.
--
local function wait_upstream_status(server, id_arg, status_arg)
    luatest.helpers.retrying({timeout = wait_timeout}, server.exec, server,
                             function(id, status)
        if box.info.replication[id].upstream.status ~= status then
            error('Waiting for upstream status')
        end
    end, {id_arg, status_arg})
end

--
-- Server 1 was a synchro queue owner. Then it receives a foreign PROMOTE which
-- goes to WAL but is not applied yet. Server 1 during that tries to make a
-- synchro transaction. It is expected to be aborted, because server gets fenced
-- seeing a new term where it isn't the limbo owner.
--
g.test_local_txn_during_remote_promote = function(g)
    -- Server 1 takes the synchro queue.
    g.server1:exec(function()
        box.ctl.promote()
        box.cfg{
            -- To hang own transactions in the synchro queue.
            replication_synchro_quorum = 3,
        }
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        -- Other server will send a promote - it should get stuck in the WAL.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    -- Deliver server 1 promotion to 2. Otherwise server 2 might fail trying to
    -- start its own promotion simultaneously.
    g.server2:wait_for_vclock_of(g.server1)

    -- Server 2 sends PROMOTE to server 1.
    g.server2:exec(function()
        require('fiber').create(box.ctl.promote)
    end)

    -- PROMOTE is stuck in the WAL on server 1.
    g.server1:play_wal_until_synchro_queue_is_busy()

    -- Server 1 shouldn't be able to make new transactions while a foreign
    -- PROMOTE goes to WAL.
    local ok1, err1, ok2, err2 = g.server1:exec(function()
        local s = box.space.test
        local fiber = require('fiber')
        -- More than one transaction to ensure that it works not just for one.
        local f1 = fiber.new(s.replace, s, {1})
        f1:set_joinable(true)
        local f2 = fiber.new(s.replace, s, {2})
        f2:set_joinable(true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        -- Multi-return of form `join(), join()` doesn't work as one might
        -- expect. Do it manually.
        local ok1, err1 = f1:join()
        local ok2, err2 = f2:join()
        return ok1, err1, ok2, err2
    end)
    luatest.assert(not ok1 and not ok2 and err1 and err2,
                   'both transactions failed')
    luatest.assert_equals(err1.code, err2.code, 'same error')
    luatest.assert_equals(err1.code, box.error.READONLY,
                          'error is read-only')

    -- Server 1 correctly processed the remote PROMOTE.
    wait_synchro_owner(g.server1, g.server2:get_instance_id())

    -- The synchronous replication is functional - new owner can use the queue.
    g.server2:exec(function()
        box.space.test:replace{3}
    end)
    g.server1:wait_for_vclock_of(g.server2)
    local content = g.server1:exec(function()
        return box.space.test:select{}
    end)
    luatest.assert_equals(content, {{3}}, 'synchro transactions work')
end

--
-- Server 1 was a synchro queue owner. It starts a synchro txn. The txn is
-- written to WAL but not reported to TX yet. Relay manages to send it. Server 2
-- receives the txn and makes a PROMOTE including this txn.
--
-- Then the PROMOTE is delivered to server 1 and goes to WAL too. Then the txn
-- and PROMOTE WAL writes are processed by TX thread in a single batch without
-- yields.
--
-- The bug was that the txn's synchro queue entry didn't get an LSN right after
-- WAL write and PROMOTE couldn't process synchro entries not having an LSN.
--
g.test_remote_promote_during_local_txn_including_it = function(g)
    -- Start synchro txns on server 1.
    local fids = g.server1:exec(function()
        box.ctl.promote()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        box.cfg{
            -- To hang own transactions in the synchro queue.
            replication_synchro_quorum = 3,
        }
        box.error.injection.set('ERRINJ_RELAY_FASTER_THAN_TX', true)
        local fiber = require('fiber')
        -- More than one transaction to ensure that it works not just for one.
        local f1 = fiber.new(s.replace, s, {1})
        f1:set_joinable(true)
        local f2 = fiber.new(s.replace, s, {2})
        f2:set_joinable(true)
        return {f1:id(), f2:id()}
    end)

    -- The txns are delivered to server 2.
    wait_synchro_len(g.server2, #fids)

    -- Server 2 confirms the txns and sends a PROMOTE covering it to server 1.
    g.server2:exec(function()
        box.cfg{
            replication_synchro_quorum = 1,
            -- To make it not wait for confirm from server 1. Just confirm via
            -- the promotion ASAP.
            replication_synchro_timeout = 0.001,
        }
        box.ctl.promote()
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 1000,
        }
    end)

    -- Server 1 receives the foreign PROMOTE. Now the local txns and a remote
    -- PROMOTE are being written to WAL.
    wait_synchro_is_busy(g.server1)

    local rows = g.server1:exec(function(fids)
        -- Simulate the TX thread being slow. To increase likelihood of the txn
        -- and PROMOTE WAL writes being processed by TX in a single batch.
        box.error.injection.set('ERRINJ_TX_DELAY_PRIO_ENDPOINT', 0.1)
        -- Let them finish WAL writes.
        box.error.injection.set('ERRINJ_RELAY_FASTER_THAN_TX', false)

        local fiber = require('fiber')
        for _, fid in pairs(fids) do
            fiber.find(fid):join()
        end
        box.error.injection.set('ERRINJ_TX_DELAY_PRIO_ENDPOINT', 0)
        return box.space.test:select()
    end, {fids})
    -- The txn was confirmed by another instance even before it was confirmed
    -- locally.
    luatest.assert_equals(rows, {{1}, {2}}, 'txn was confirmed')

    local rows2 = g.server2:exec(function()
        return box.space.test:select()
    end)
    luatest.assert_equals(rows2, rows, 'on instance 2 too')

    -- The synchronous replication is functional - new owner can use the queue.
    g.server2:exec(function()
        box.space.test:replace{3}
    end)
    g.server1:wait_for_vclock_of(g.server2)
    rows = g.server1:exec(function()
        return box.space.test:select{}
    end)
    luatest.assert_equals(rows, {{1}, {2}, {3}}, 'synchro transactions work')
end

--
-- Server 1 was a synchro queue owner. It starts a synchro txn. The txn is
-- not written to WAL yet. Server 2 makes a PROMOTE which doesn't include that
-- txn.
--
-- Then the PROMOTE is delivered to server 1 and goes to WAL too. Then the txn
-- and PROMOTE WAL writes are processed by TX thread in a single batch without
-- yields.
--
-- The bug was that the fiber authored the txn wasn't ready to a WAL write
-- being success, txn being marked as non-synchro anymore (it happened on
-- rollback in the synchro queue), but the txn signature being bad.
--
g.test_remote_promote_during_local_txn_not_including_it = function(g)
    -- Start a synchro txn on server 1.
    local fids = g.server1:exec(function()
        box.ctl.promote()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        box.cfg{
            replication_synchro_quorum = 3,
        }
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local fiber = require('fiber')
        -- More than one transaction to ensure that it works not just for one.
        local f1 = fiber.new(s.replace, s, {1})
        f1:set_joinable(true)
        local f2 = fiber.new(s.replace, s, {2})
        f2:set_joinable(true)
        return {f1:id(), f2:id()}
    end)
    -- Deliver server 1 promotion to 2. Otherwise server 2 might fail trying to
    -- start its own promotion simultaneously.
    g.server2:wait_for_vclock_of(g.server1)

    -- Server 2 sends a PROMOTE not covering the txns to server 1.
    g.server2:exec(function()
        box.cfg{
            replication_synchro_quorum = 1,
        }
        box.ctl.promote()
    end)

    -- Server 1 receives the PROMOTE.
    wait_synchro_is_busy(g.server1)

    local rows = g.server1:exec(function(fids)
        -- Simulate the TX thread being slow. To increase likelihood of the txn
        -- and PROMOTE WAL writes being processed by TX in a single batch.
        box.error.injection.set('ERRINJ_TX_DELAY_PRIO_ENDPOINT', 0.1)
        -- Let them finish WAL writes.
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        local fiber = require('fiber')
        for _, fid in pairs(fids) do
            fiber.find(fid):join()
        end
        box.error.injection.set('ERRINJ_TX_DELAY_PRIO_ENDPOINT', 0)
        return box.space.test:select()
    end, {fids})
    luatest.assert_equals(rows, {}, 'txns were rolled back')

    -- Wait until the stale txns (written before promotion) arrive to server2.
    -- It should simply ignore them.
    g.server2:wait_for_vclock_of(g.server1)
    wait_upstream_status(g.server2, g.server1:get_instance_id(), 'follow')
    rows = g.server2:exec(function()
        return box.space.test:select()
    end)
    luatest.assert_equals(rows, {}, 'server 2 received it but did not apply')

    -- Recreate the cluster because it is broken now.
    cluster_drop(g)
    cluster_create(g)
end
