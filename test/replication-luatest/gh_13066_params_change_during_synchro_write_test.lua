local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- gh-13066: there was the following bug.
--
-- A limbo request (PROMOTE/DEMOTE) is prepared under the limbo latch, and the
-- latch is held across its WAL write, which yields.
--
-- The PROMOTE/DEMOTE was speculatively bumping the volatile confirmed LSN
-- during prepare. This temporarily violated the invariant, that the volatile
-- confirmed lsn is always less than the LSN of the next entry to confirm.
--
-- It was assumed the violation wasn't observable, but it actually was. ACK
-- processing code works with the volatile confirmed LSN without a latch, and it
-- does actually check this invariant.
--
-- This led to a crash and was fixed earlier. But the state could still be in
-- theory observed from the code handling `box.cfg` parameters change affecting
-- the limbo. That patch wasn't covered with tests, and the test below fixes
-- that.
--
g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.replica_set.id),
            server.build_listen_uri('victim', cg.replica_set.id),
            server.build_listen_uri('laggard', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_timeout = 60,
        election_mode = 'off',
    }
    -- Master - owns the limbo and writes the synchronous transactions.
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = box_cfg,
    }
    -- Victim - applies master txns and later applies the DEMOTE - it is the
    -- node which gets the parameters change in the middle of the DEMOTE WAL
    -- write.
    cg.victim = cg.replica_set:build_and_add_server{
        alias = 'victim',
        box_cfg = box_cfg,
    }
    -- Laggard - ACKs the first transaction and gets stuck, so that the ACKs
    -- collected by the victim stay behind the DEMOTE's confirm boundary.
    cg.laggard = cg.replica_set:build_and_add_server{
        alias = 'laggard',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        rawset(_G, 'fiber', require('fiber'))
        box.ctl.promote()
        box.schema.space.create('s', {is_sync = true}):create_index('p')
    end)
    cg.victim:wait_for_vclock_of(cg.master)
    cg.laggard:wait_for_vclock_of(cg.master)
    cg.master_id = cg.master:exec(function() return box.info.id end)
    cg.laggard_id = cg.laggard:exec(function() return box.info.id end)
end)

g.after_each(function(cg)
    for _, instance in pairs({cg.master, cg.victim, cg.laggard}) do
        pcall(instance.exec, instance, function()
            box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', false)
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
    end
    cg.replica_set:drop()
end)

--
-- Write one synchronous transaction and return its LSN. Assuming it is not
-- confirmed, because the server's limbo worker is frozen.
--
local function make_txn(server_, value)
    return server_:exec(function(value)
        local lsn = box.info.lsn
        local f = _G.fiber.create(function() box.space.s:replace{value} end)
        f:set_joinable(true)
        local fibers = rawget(_G, 'fibers') or {}
        table.insert(fibers, f)
        rawset(_G, 'fibers', fibers)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_gt(box.info.lsn, lsn)
        end)
        return box.info.lsn
    end, {value})
end

g.test_params_change_during_synchro_request_write = function(cg)
    cg.master:exec(function()
        -- No CONFIRM must ever be written: the transactions have to stay in the
        -- limbo of all the nodes until the DEMOTE.
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', true)
        -- The master must be able to write the DEMOTE without waiting for
        -- anybody - the laggard is stuck by then.
        box.cfg{replication_synchro_quorum = 1}
    end)
    cg.victim:exec(function()
        --
        -- The victim can collect at most 3 ACKs: its own, the master's, and
        -- the laggard's. The quorum is unreachable until the parameters change
        -- lowers it.
        --
        box.cfg{replication_synchro_quorum = 4}
    end)

    -- Transaction 1 - everybody has it, and the victim knows the laggard's
    -- ACK for it.
    local lsn1 = make_txn(cg.master, 1)
    cg.laggard:wait_for_vclock_of(cg.master)
    cg.victim:exec(function(laggard_id, master_id, lsn1)
        t.helpers.retrying({timeout = 60}, function()
            local down = box.info.replication[laggard_id].downstream
            t.assert_not_equals(down, nil)
            t.assert_equals(down.vclock[master_id], lsn1)
        end)
    end, {cg.laggard_id, cg.master_id, lsn1})

    -- Transaction 2 - the laggard never gets it, so the ACKs known to the
    -- victim keep covering only transaction 1.
    cg.laggard:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
    end)
    make_txn(cg.master, 2)
    cg.victim:wait_for_vclock_of(cg.master)

    -- The DEMOTE confirms both transactions, i.e. its confirm boundary is
    -- ahead of what the ACKs known to the victim cover.
    cg.victim:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    cg.master:exec(function() box.ctl.demote() end)

    cg.victim:exec(function()
        --
        -- Let the WAL writes through one by one until the one made under the
        -- limbo latch is reached - that is the DEMOTE.
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
        -- The parameters change lands right in the middle of the DEMOTE WAL
        -- write. The bump it makes can only be based on the collected ACKs -
        -- the boundary is the laggard's ACK, behind the DEMOTE's one.
        --
        box.cfg{replication_synchro_quorum = 3}
        -- Release the DEMOTE.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)

    -- The victim must survive and apply the DEMOTE as usual.
    cg.laggard:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', false)
        for _, f in pairs(rawget(_G, 'fibers')) do
            f:join()
        end
    end)
    cg.victim:exec(function()
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_equals(box.space.s:select(), {{1}, {2}})
    end)
end
