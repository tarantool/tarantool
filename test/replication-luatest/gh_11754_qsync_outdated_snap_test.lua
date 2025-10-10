local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({box_cfg = {
        replication_synchro_timeout = 1000,
        election_mode = 'manual',
    }})
    cg.server:start()
    cg.server:exec(function()
        local xlog = require('xlog')
        local fio = require('fio')

        box.ctl.promote()
        box.ctl.wait_rw()

        local function get_confirmed_lsn_from_PROMOTE(body)
            local key_lsn = box.iproto.key.LSN
            for k, v in pairs(body) do
                if k == key_lsn then
                    return v
                end
            end
            t.fail("Not found the LSN key in the PROMOTE message")
        end

        rawset(_G, 'test_get_snap_confirmed_lsn', function()
            local glob = fio.glob(fio.pathjoin(box.cfg.memtx_dir, '*.snap'))
            t.assert_gt(#glob, 0)
            table.sort(glob)
            local snap_path = glob[#glob]

            for _, v in xlog.pairs(snap_path) do
                local type = v.HEADER.type
                if type == 'RAFT_PROMOTE' then
                    return get_confirmed_lsn_from_PROMOTE(v.BODY)
                end
            end
            t.fail("Not found a PROMOTE message in the snapshot")
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-11754: there was a bug that the snapshot creation code would do the
-- synchronous transaction control state collection too early. It was fully done
-- even before the engines would begin their checkpoints.
--
-- That led to a problem that the limbo and Raft checkpoints could have outdated
-- confirmation LSN, terms, and anything else that is supposed to be written
-- into the snapshot.
--
-- The test is reproducing the simplest previously failing scenario with just a
-- single pending synchronous transaction, when snapshot creation is started.
--
g.test_snapshot_wait_synchro_vclock = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local timeout = 60
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        --
        -- If the first snapshot is writing any special things, let it be done
        -- with it before the actual reproducer is about to happen.
        --
        s:replace{1}
        box.snapshot()
        --
        -- Check how a properly stored confirmed vclock is stored. This is
        -- validated on a trivial case when the snapshot is started with no
        -- pending transaction anywhere. Just a single synchro txn that is
        -- already confirmed.
        --
        local lsn = box.info.lsn
        s:replace{2}
        box.snapshot()
        -- +1 for the txn itself, +1 for CONFIRM.
        t.assert_equals(box.info.lsn, lsn + 2)
        -- CONFIRM entry is not accounted.
        t.assert_equals(_G.test_get_snap_confirmed_lsn(), box.info.lsn - 1)
        --
        -- Try the same with the transaction being a bit too slow. It is stuck
        -- on a slow WAL write. That shouldn't change anything.
        --
        lsn = box.info.lsn
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f_txn = fiber.create(function()
            fiber.self():set_joinable(true)
            s:replace{3}
        end)
        local f_snap = fiber.create(function()
            fiber.self():set_joinable(true)
            box.snapshot()
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f_txn:join(timeout)))
        t.assert((f_snap:join(timeout)))
        t.assert_equals(box.info.lsn, lsn + 2)
        -- The bug was that the confirmed LSN here would be too low. It wouldn't
        -- have progressed since the previous transaction.
        t.assert_equals(_G.test_get_snap_confirmed_lsn(), box.info.lsn - 1)
        --
        -- Cleanup.
        --
        s:drop()
    end)
end
