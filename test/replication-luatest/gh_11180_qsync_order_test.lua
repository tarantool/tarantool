local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {
            replication_synchro_queue_max_size = 1000,
            wal_queue_max_size = 1000,
            replication_synchro_timeout = 1000,
            election_mode = 'manual',
        }
    })
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'fiber', require('fiber'))
        rawset(_G, 'test_timeout', 60)
        rawset(_G, 'test_data', string.rep('a', 1000))
        box.ctl.promote()
        box.ctl.wait_rw()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        s = box.schema.create_space('test_async')
        s:create_index('pk')

        rawset(_G, 'join_with_error', function(f, expected_err)
            local ok, err = f:join()
            t.assert_not(ok)
            t.assert_covers(err:unpack(), expected_err)
        end)
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:truncate()
        box.space.test_async:truncate()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-11180: rollback of a txn waiting for space in the limbo wouldn't cascading
-- rollback the newer txns.
--
g.test_cascading_rollback_while_waiting_for_limbo_space = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        local results = {}
        local function make_txn_fiber(id)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.begin()
                box.on_rollback(function()
                    table.insert(results, ('rollback %s'):format(id))
                end)
                box.on_commit(function()
                    table.insert(results, ('commit %s'):format(id))
                end)
                s:insert{id, _G.test_data}
                box.commit()
            end)
        end
        --
        -- txn1 is stuck in WAL, txn2-5 are waiting for limbo space.
        --
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f1 = make_txn_fiber(1)
        local f2 = make_txn_fiber(2)
        local f3 = make_txn_fiber(3)
        local f4 = make_txn_fiber(4)
        local f5 = make_txn_fiber(5)
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        --
        -- txn3 is cancelled and rolled back. txn4 and txn5 are cascading rolled
        -- back. The cancel is done for 2 txns to see what happens when one of
        -- them is both cancelled AND rolled back cascading.
        --
        f3:cancel()
        f4:cancel()
        -- Limbo doesn't set the cascading rollback error. No reason why.
        _G.join_with_error(f3, {name = 'SYNC_ROLLBACK'})
        _G.join_with_error(f4, {name = 'SYNC_ROLLBACK'})
        _G.join_with_error(f5, {name = 'SYNC_ROLLBACK'})
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        --
        -- The older txns were not affected by the rollback of newer txns.
        --
        t.assert((f1:join(_G.test_timeout)))
        t.assert((f2:join(_G.test_timeout)))
        t.assert_equals(results, {'rollback 5', 'rollback 4', 'rollback 3',
                                  'commit 1', 'commit 2'})
        t.assert_equals(s:select(), {{1, _G.test_data}, {2, _G.test_data}})
    end)
end

--
-- One txn is submitted to the limbo and is stuck in WAL. The other txn is
-- volatile and waits for limbo space. Suddenly there becomes enough space. The
-- second then should see that it is not the first one, but it is the first
-- volatile one and hence can proceed to WAL.
--
g.test_unblock_nonfirst_volatile_entry = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f1 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            s:insert{1, _G.test_data}
        end)
        local f2 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            s:insert{2, _G.test_data}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        local old_size = box.cfg.replication_synchro_queue_max_size
        box.cfg{replication_synchro_queue_max_size = 1000000}
        f2:wakeup()
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert_equals(box.info.synchro.queue.len, 2)
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join()))
        t.assert((f2:join()))
        t.assert_equals(s:select(), {{1, _G.test_data}, {2, _G.test_data}})
        box.cfg{replication_synchro_queue_max_size = old_size}
    end)
end

--
-- One volatile async transaction in the limbo queue after a synchro txn. It is
-- going to try being added to the limbo, but it won't be.
--
g.test_volatile_async_one = function(cg)
    cg.server:exec(function()
        local sync = box.space.test
        local async = box.space.test_async
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f1 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            sync:insert{1, _G.test_data}
        end)
        local f2 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            async:insert{1}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join()))
        t.assert((f2:join()))
        t.assert_equals(sync:select(), {{1, _G.test_data}})
        t.assert_equals(async:select(), {{1}})
    end)
end

--
-- Same, but multiple async txns in the tail.
--
g.test_volatile_async_many = function(cg)
    cg.server:exec(function()
        local sync = box.space.test
        local async = box.space.test_async
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f1 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            sync:insert{1, _G.test_data}
        end)
        local f2 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            async:insert{1}
        end)
        local f3 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            async:insert{2}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join()))
        t.assert((f2:join()))
        t.assert((f3:join()))
        t.assert_equals(sync:select(), {{1, _G.test_data}})
        t.assert_equals(async:select(), {{1}, {2}})
    end)
end

--
-- The limbo might be not full and yet have volatile entries in the end. The
-- newly coming entries then should still respect the order.
--
g.test_non_full_but_volatile = function(cg)
    cg.server:exec(function()
        local sync = box.space.test
        local async = box.space.test_async
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local results = {}
        local function make_txn_fiber(space, id, on_commit, opts)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.begin()
                box.on_commit(function()
                    table.insert(results, id)
                    if on_commit then
                        on_commit()
                    end
                end)
                space:insert{id, _G.test_data}
                box.commit(opts)
            end)
        end
        local f3, f4
        --
        -- When txn1 gets committed, it is already removed from the limbo, so
        -- the limbo becomes "not full". But in this case txn2 is volatile and
        -- is waiting for its space in the limbo. So new entries should line up
        -- after txn2. Despite the limbo being not full.
        --
        local f1 = make_txn_fiber(sync, 1, function()
            -- Blocking is required, so wait_mode=none is not possible.
            f3 = make_txn_fiber(async, 3, nil, {wait = 'none'})
            -- Try the old blocking way.
            f4 = make_txn_fiber(async, 4)
        end)
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        local f2 = make_txn_fiber(sync, 2)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join()))
        t.assert((f2:join()))
        _G.join_with_error(f3, {name = 'SYNC_QUEUE_FULL'})
        t.assert((f4:join()))
        t.assert_equals(results, {1, 2, 4})
    end)
end

--
-- gh-11180: volatile txns waiting for the limbo space didn't form any queue
-- and their spurious wakeups could lead to them finishing the commits not in
-- the same order as the commits were started.
--
g.test_spurious_wakeup = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        local f1, f2, f3
        --
        -- This fiber gets executed on each event loop iteration, right after
        -- the scheduler-fiber. It is able then to mess up any plans that the
        -- next fibers would have about waking each other up in any special
        -- order.
        --
        local wakeuper = _G.fiber.create(function()
            while true do
                _G.fiber.testcancel()
                --
                -- f2 is supposed to finish before f3, but lets wake them up
                -- always in the reversed order.
                --
                if f3 then
                    pcall(f3.wakeup, f3)
                end
                if f2 then
                    pcall(f2.wakeup, f2)
                end
                _G.fiber.yield()
            end
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local results = {}
        local function make_txn_fiber(id)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.begin()
                box.on_rollback(function()
                    table.insert(results, ('rollback %s'):format(id))
                end)
                box.on_commit(function()
                    table.insert(results, ('commit %s'):format(id))
                end)
                s:insert{id, _G.test_data}
                box.commit()
            end)
        end
        f1 = make_txn_fiber(1)
        f2 = make_txn_fiber(2)
        f3 = make_txn_fiber(3)
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join(_G.test_timeout)))
        t.assert((f2:join(_G.test_timeout)))
        t.assert((f3:join(_G.test_timeout)))
        wakeuper:cancel()
        t.assert_equals(results, {'commit 1', 'commit 2', 'commit 3'})
        t.assert_equals(s:select(), {
            {1, _G.test_data}, {2, _G.test_data}, {3, _G.test_data}
        })
    end)
end

--
-- gh-11180: rollback of a txn due to a WAL error wouldn't cascading rollback
-- the newer txns which were prepared but didn't reach the journal yet (at the
-- moment of writing such txns could only be the ones waiting for space in the
-- limbo).
--
g.test_cascading_rollback_from_journal = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        local old_max_size = box.cfg.replication_synchro_queue_max_size
        box.cfg{replication_synchro_queue_max_size = #_G.test_data * 2}
        local results = {}
        local function make_txn_fiber(id)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.begin()
                box.on_rollback(function()
                    table.insert(results, ('rollback %s'):format(id))
                end)
                box.on_commit(function()
                    table.insert(results, ('commit %s'):format(id))
                end)
                s:replace{id, _G.test_data}
                box.commit()
            end)
        end
        --
        -- txn1 is stuck in WAL.
        -- txn2 is in the limbo, but waits for WAL space.
        -- txn3-5 are volatile, wait for limbo space.
        --
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f1 = make_txn_fiber(1)
        local f2 = make_txn_fiber(2)
        local f3 = make_txn_fiber(3)
        local f4 = make_txn_fiber(4)
        local f5 = make_txn_fiber(5)
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        --
        -- txn1 fails due to a WAL error. But then WAL has no error. So the
        -- next txns must be rolled back only due to cascade. Not due to WAL
        -- errors.
        --
        box.error.injection.set('ERRINJ_WAL_ROTATE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        _G.join_with_error(f1, {name = 'WAL_IO'})
        _G.join_with_error(f2, {name = 'CASCADE_ROLLBACK'})
        _G.join_with_error(f3, {name = 'SYNC_ROLLBACK'})
        _G.join_with_error(f4, {name = 'SYNC_ROLLBACK'})
        _G.join_with_error(f5, {name = 'SYNC_ROLLBACK'})
        t.assert_equals(results, {'rollback 5', 'rollback 4', 'rollback 3',
                                  'rollback 2', 'rollback 1'})
        --
        -- None of the txns 2-5 even tried going to WAL. The delay wasn't
        -- re-activated.
        --
        t.assert_not(box.error.injection.get('ERRINJ_WAL_DELAY'))
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_ROTATE', false)
        --
        -- Cleanup.
        --
        box.cfg{replication_synchro_queue_max_size = old_max_size}
        box.ctl.promote()
        box.ctl.wait_rw()
    end)
end
