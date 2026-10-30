local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- gh-13095: there was the following bug.
--
-- A synchronous transaction, whose submission into the limbo got blocked on
-- the queue being full, was parked inside the submission code waiting for free
-- space, not yet sent to the journal.
--
-- The protection against new transactions joining the queue during an ongoing
-- synchro request was only applied at the submission start.
--
-- It could happen that a transaction started the submission while there was no
-- fencing yet, then it got blocked on the synchro queue being full, and then
-- the fencing was activated.
--
-- The parked transaction then could be woken up in the middle of, for example,
-- a ROLLBACK WAL write, not notice the fencing, but notice free space in the
-- synchro queue. Then it would happily become 'submitted' and get sent to WAL
-- right after the ROLLBACK.
--
-- After the ROLLBACK was written, the transaction itself would be rolled back
-- in memory as a part of the same rollback process.
--
-- Local recovery would resurrect it as a pending synchronous transaction again.
-- Also replicas would apply it even without any restarts.
--
-- This test covers entirely the ROLLBACK request, which is expected to be
-- deprecated eventually. Same situation happened with PROMOTE/DEMOTE requests,
-- which are tested separately using only the APIs, which aren't scheduled for
-- deprecation.
--
g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new{
        alias = 'master',
        box_cfg = {
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 60,
            election_mode = 'off',
        },
    }
    cg.server:start()
    cg.server:exec(function()
        box.ctl.promote()
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        -- One synchronous transaction already overflows the queue.
        box.cfg{replication_synchro_queue_max_size = 1}
    end)
end)

g.after_each(function(cg)
    pcall(cg.server.exec, cg.server, function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    cg.server:drop()
end)

g.test_txn_during_rollback_write = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        -- Transaction 1 - is written to the WAL and fills the queue up to its
        -- max size. The quorum of 2 is unreachable on a single instance, so
        -- the transaction can only end by the timeout.
        local lsn = box.info.lsn
        local f1 = fiber.create(function() box.space.s:replace{1} end)
        f1:set_joinable(true)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_gt(box.info.lsn, lsn)
        end)
        lsn = box.info.lsn
        t.assert_equals(box.info.synchro.queue.len, 1)
        -- Transaction 2 - the queue is full, so it parks inside the submission
        -- waiting for free space. It has no WAL row yet and is not accounted
        -- in the queue.
        local f2 = fiber.create(function() box.space.s:replace{2} end)
        f2:set_joinable(true)
        t.assert_equals(box.info.lsn, lsn)
        t.assert_equals(box.info.synchro.queue.len, 1)
        -- Time the first transaction out and park its ROLLBACK right in the
        -- WAL write.
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
        -- Give the parked transaction 2 a reason to wake up in the middle of
        -- the ROLLBACK write: raise the max size, which on its own doesn't
        -- wake anybody up, and then poke the queue with a synchro parameter
        -- change, which does.
        box.cfg{replication_synchro_queue_max_size = 1024 * 1024}
        box.cfg{replication_synchro_timeout = 60}
        fiber.sleep(0.1)
        -- The transaction must not have gone to the journal while the
        -- rollback is in progress.
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.info.lsn, lsn)
        -- Let the ROLLBACK finish. Both transactions are rolled back, the
        -- second one - without ever getting into the journal.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok, err = f1:join(60)
        t.assert_not(ok)
        t.assert_equals(err.code, box.error.SYNC_QUORUM_TIMEOUT)
        ok, err = f2:join(60)
        t.assert_not(ok)
        t.assert_equals(err.code, box.error.SYNC_ROLLBACK)
        -- Only the ROLLBACK went into the WAL after the transactions.
        t.assert_equals(box.info.lsn, lsn + 1)
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.space.s:select(), {})
    end)
end
