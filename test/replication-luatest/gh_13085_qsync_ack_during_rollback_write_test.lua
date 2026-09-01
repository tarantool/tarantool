local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- gh-13085: there was the following bug.
--
-- When a synchronous transaction's waiter times out, it writes a ROLLBACK into
-- the WAL. The write yields, and the doomed entry is still in the limbo,
-- visible to the rest of the system.
--
-- The ACK processing doesn't take the limbo latch and doesn't check for an
-- ongoing rollback. An ACK completing a quorum during the ROLLBACK WAL write
-- would bump the volatile confirmed LSN up to the entry being rolled back and
-- wake the limbo worker up.
--
-- The rollback, when applied after its WAL write, didn't revert the volatile
-- confirmed LSN. The worker then found it ahead of the persistent one and wrote
-- a CONFIRM for the very LSN just covered by the ROLLBACK, even though the
-- limbo was empty by then.
--
-- Locally the CONFIRM was a nop - the transaction was already gone. But it
-- bumped the persistent confirmed LSN, which made the split-brain detection
-- blind. A replica which ACKed the transaction and got promoted before
-- receiving the ROLLBACK would confirm the transaction via its PROMOTE. The
-- PROMOTE's LSN would exactly match the old master's bogus confirmed LSN, and
-- the old master would silently follow the new one, with the instances having
-- diverged: the transaction is committed on the new master and rolled back on
-- the old one.
--
g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.replica_set.id),
            server.build_listen_uri('replica', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_timeout = 60,
        replication_synchro_quorum = 2,
        election_mode = 'off',
    }
    -- Master - owns the limbo, writes the transaction, times it out and
    -- rolls it back while the quorum arrives.
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = box_cfg,
    }
    -- Replica - completes the quorum right in the middle of the master's
    -- ROLLBACK WAL write, and later confirms the transaction via its own
    -- PROMOTE.
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        rawset(_G, 'fiber', require('fiber'))
        box.ctl.promote()
        box.schema.space.create('s', {is_sync = true}):create_index('p')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_each(function(cg)
    for _, instance in pairs({cg.master, cg.replica}) do
        pcall(instance.exec, instance, function()
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
    end
    cg.replica_set:drop()
end)

g.test_ack_during_rollback_write = function(cg)
    local master_id = cg.master:get_instance_id()
    local replica_id = cg.replica:get_instance_id()
    -- Block the replica's WAL, so the transaction can't be written there and
    -- hence can't be ACKed until allowed explicitly.
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
    end)
    -- The transaction is written on the master and is waiting for the quorum,
    -- which can't be reached while the replica's WAL is blocked.
    local lsn = cg.master:exec(function()
        local lsn = box.info.lsn
        local f = _G.fiber.create(function() box.space.s:replace{1} end)
        f:set_joinable(true)
        rawset(_G, 'txn_fiber', f)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_gt(box.info.lsn, lsn)
        end)
        return box.info.lsn
    end)
    -- Time the waiter out and park its ROLLBACK right in the WAL write.
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        box.cfg{replication_synchro_timeout = 0.001}
        --
        -- Let the WAL writes through one by one until the one made under the
        -- limbo latch is reached - that is the ROLLBACK.
        --
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            if box.info.synchro.queue.busy then
                return
            end
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            t.fail('Not the synchro request write yet')
        end)
    end)
    -- Release the replica's WAL. Its ACK completes the quorum while the
    -- master's ROLLBACK for the same transaction is still being written.
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    cg.master:exec(function(replica_id, master_id, lsn)
        t.helpers.retrying({timeout = 60}, function()
            local down = box.info.replication[replica_id].downstream
            t.assert_not_equals(down, nil)
            t.assert_equals(down.vclock[master_id], lsn)
        end)
        -- The ACK is processed while the ROLLBACK is still parked.
        t.assert(box.info.synchro.queue.busy)
        t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
    end, {replica_id, master_id, lsn})
    -- Detach the replica, so it never learns about the rollback and keeps the
    -- transaction pending in its limbo.
    cg.replica:exec(function()
        box.cfg{replication = {}}
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
    -- Let the ROLLBACK finish. The transaction is rolled back on the master.
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok, err = rawget(_G, 'txn_fiber'):join(60)
        t.assert_not(ok)
        t.assert_equals(err.code, box.error.SYNC_QUORUM_TIMEOUT)
        t.assert_equals(box.space.s:select(), {})
        t.assert_equals(box.info.synchro.queue.len, 0)
    end)
    -- The replica confirms the transaction via its own PROMOTE - a perfectly
    -- legal thing to do. It has the quorum of one, and no idea about the
    -- rollback. The instances have diverged now.
    cg.replica:exec(function()
        box.cfg{replication_synchro_quorum = 1}
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.space.s:select(), {{1}})
    end)
    --
    -- The master must notice the split-brain: the PROMOTE confirms an LSN which
    -- the master has rolled back. With the bug the bogus CONFIRM would bump the
    -- master's confirmed LSN to exactly the PROMOTE's LSN, and the PROMOTE
    -- would be silently applied.
    --
    local outcome = cg.master:exec(function(replica_id)
        local result
        t.helpers.retrying({timeout = 60}, function()
            if box.info.synchro.queue.owner == replica_id then
                result = 'accepted'
                return
            end
            result = box.info.replication[replica_id].upstream.status
            t.assert_equals(result, 'stopped')
        end)
        return result
    end, {replica_id})
    t.assert_equals(outcome, 'stopped')
    cg.master:exec(function(master_id, lsn)
        -- Nothing but the ROLLBACK went into the WAL after the transaction.
        t.assert_equals(box.info.lsn, lsn + 1)
        t.assert_equals(box.info.synchro.queue.owner, master_id)
        t.assert_equals(box.space.s:select(), {})
    end, {master_id, lsn})
end
