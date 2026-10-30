local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-11802-memtx-mvcc-rollback-may-cause-dirty-gap-read')
--
-- gh-11802: memtx mvcc rollback may cause dirty gap read
--

g.before_all(function()
    t.tarantool.skip_if_not_debug()

    g.server = server:new{
        box_cfg = {
            memtx_use_mvcc_engine = true,
            txn_isolation = 'read-committed',
        }
    }
    g.server:start()

    g.server:exec(function()
        box.schema.space.create("test")
        box.space.test:format{{'a', type='unsigned'}, {'b', type='unsigned'}}
        box.space.test:create_index("pk", {parts={{'a'}}})
        box.space.test:create_index("sk", {parts={{'b'}}, unique=true})
    end)
end)

g.after_each(function()
    g.server:exec(function() box.space.test:truncate() end)
end)

g.after_all(function()
    g.server:drop()
end)

g.test_abort_dirty_gap_read_after_rollback_test = function()
    g.server:exec(function()
        box.space.test:insert{1, 1}

        -- We want `replace{1, 2}` to hang after preparation but before it
        -- enters the WAL, for example, in the WAL queue. However, no matter
        -- how small wal_queue_max_size is, it always allows at least one
        -- transaction to pass into the WAL. Therefore, we need to stop the
        -- WAL and send a dummy `replace{10000, 10000}` there first, so that
        -- `replace{1, 2}` gets stuck in the WAL queue.
        -- We can't just stop the WAL and send `replace{1, 2}` there,
        -- because we won't be able to roll it back with an error afterward
        -- — the 'ERRINJ_WAL_IO' check happens before 'ERRINJ_WAL_DELAY'.
        box.cfg{wal_queue_max_size=1}
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        box.begin()
            box.space.test:insert{10000, 10000}
        box.commit({wait='none'})

        local fiber = require('fiber')

        -- Will be rolled back due to WAL error.
        local f1 = fiber.create(function()
            box.space.test:replace{1, 2}
        end)
        f1:set_joinable(true)

        local cond = fiber.cond()
        -- Must be aborted with conflict.
        local f2 = fiber.create(function()
            box.begin()
                t.assert_equals(box.space.test.index.sk:get{1}, nil)
                cond:wait()
            box.commit()
        end)
        f2:set_joinable(true)

        box.error.injection.set('ERRINJ_WAL_IO', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        local _, err = f1:join()
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.WAL_IO,
            message = 'Failed to write to disk',
        })

        box.error.injection.set('ERRINJ_WAL_IO', false)

        cond:signal()
        local _, err = f2:join()
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.TRANSACTION_CONFLICT,
            message = 'Transaction has been aborted by conflict',
        })
    end)
end
