local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- gh-13066: there was the following bug, which indirectly has motivated this
-- test.
--
-- A limbo request (PROMOTE/DEMOTE) is prepared under the limbo latch, and the
-- latch is held across its WAL write, which yields.
--
-- The PROMOTE/DEMOTE was speculatively bumping the volatile confirmed LSN
-- during prepare.
--
-- This was used by the synchronous transaction local rollback
-- (IPROTO_RAFT_ROLLBACK) on timeout. If a fiber sees the synchro transaction
-- has timed out, but its LSN is < the volatile confirmed LSN, then it wouldn't
-- write a rollback as it is too late for that.
--
-- This speculative bump was removed, and so the rollback-on-timeout code had to
-- be updated.
--
-- The behaviour above was working before the speculation removal, but it wasn't
-- tested anywhere. So this test is added in scope of the bigger rework to just
-- cover a related and previously untested path.
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
        election_mode = 'off',
    }
    -- Master - owns the limbo, writes the transaction and can't gather the
    -- quorum for it.
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = box_cfg,
    }
    -- Replica - promotes itself with a confirm boundary covering the
    -- transaction, right when the master's waiter times out.
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
    cg.replica_id = cg.replica:exec(function() return box.info.id end)
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

g.test_txn_timeout_during_synchro_request_write = function(cg)
    cg.master:exec(function()
        -- Unreachable quorum - the transaction can only be confirmed by a
        -- PROMOTE.
        box.cfg{replication_synchro_quorum = 3}
    end)
    cg.replica:exec(function()
        -- The replica's own view of the transaction is enough for its
        -- promotion to cover it.
        box.cfg{replication_synchro_quorum = 1}
    end)

    -- The transaction whose waiter is going to time out.
    cg.master:exec(function()
        local lsn = box.info.lsn
        local f = _G.fiber.create(function() box.space.s:replace{1} end)
        f:set_joinable(true)
        rawset(_G, 'txn_fiber', f)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_gt(box.info.lsn, lsn)
        end)
    end)
    cg.replica:wait_for_vclock_of(cg.master)

    -- The promotion covering the transaction, parked in its WAL write on the
    -- master.
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    cg.replica:exec(function() box.ctl.promote() end)
    cg.master:exec(function()
        --
        -- Let the WAL writes through one by one until the one made under the
        -- limbo latch is reached - that is the PROMOTE.
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
        --
        -- Time the waiter out right in the middle of the PROMOTE WAL write. The
        -- waiter decides to start a rollback-by-timeout and blocks on the limbo
        -- latch held by the PROMOTE.
        --
        box.cfg{replication_synchro_timeout = 0.001}
        --
        -- The decision to roll back is only made by the waiter fiber itself.
        -- Give it time to actually reach the latch before letting the PROMOTE
        -- finish.
        --
        require('fiber').sleep(0.1)
        --
        -- Release the PROMOTE. It confirms the transaction and transfers the
        -- queue ownership while the timed out waiter is on the latch.
        --
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    -- The waiter must wake up and find its transaction committed. No
    -- rollback, no hang.
    cg.master:exec(function(replica_id)
        local joined, err = _G.txn_fiber:join(60)
        t.assert(joined)
        t.assert_equals(err, nil)
        t.assert_equals(box.space.s:select(), {{1}})
        t.assert_equals(box.info.synchro.queue.owner, replica_id)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
    end, {cg.replica_id})
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.space.s:select(), {{1}})
    end)
end
