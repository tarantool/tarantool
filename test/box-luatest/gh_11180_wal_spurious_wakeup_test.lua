local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({box_cfg = {wal_queue_max_size = 1000}})
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'fiber', require('fiber'))
        rawset(_G, 'test_data', string.rep('a', 1000))
        rawset(_G, 'test_timeout', 60)
        rawset(_G, 'join_with_error', function(f, err_expected)
            local ok, err = f:join()
            t.assert_not(ok)
            t.assert_covers(err:unpack(), err_expected)
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-11180: the queued but not yet submitted WAL entries were not handling
-- spurious wakeups and could break in all sorts of ways due to that.
--
g.test_wal_queue_rollback_in_flight = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        local results = {}
        local function make_txn_fiber(id)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.begin()
                box.on_commit(function()
                    table.insert(results, id)
                end)
                s:replace{id, _G.test_data}
                box.commit()
            end)
        end
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f2, f3 = nil
        --
        -- The first txn goes to WAL. The other 2 txns wait for free space in
        -- the journal queue. When the first txn gets committed, it wakes the
        -- next txn-fibers up in a wrong order. They should still be able to
        -- re-sort themselves.
        --
        local f1 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            box.begin()
            box.on_commit(function()
                table.insert(results, 1)
                if f3 then
                    pcall(f3.wakeup, f3)
                end
                if f2 then
                    pcall(f2.wakeup, f2)
                end
            end)
            s:replace{1, _G.test_data}
            box.commit()
        end)
        -- Make sure all 3 txns are waiting and aren't committed just one by one
        -- somehow.
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        f2 = make_txn_fiber(2)
        f3 = make_txn_fiber(3)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join(_G.test_timeout)))
        t.assert((f2:join(_G.test_timeout)))
        t.assert((f3:join(_G.test_timeout)))
        t.assert_equals(results, {1, 2, 3})
        s:drop()
    end)
end

g.test_wal_cancel_first_waiting = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        -- Goes to WAL thread.
        local f2
        local f1 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            box.begin()
            box.on_commit(function() f2:cancel() end)
            s:replace{1, _G.test_data}
            box.commit()
        end)
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        -- Gets stuck in the WAL queue and cancelled right when the first txn
        -- frees the WAL queue.
        f2 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            s:replace{2, _G.test_data}
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join()))
        _G.join_with_error(f2, {type = 'FiberIsCancelled'})
        t.assert_equals(s:select(), {{1, _G.test_data}})
        s:drop()
    end)
end

--
-- gh-11712: txns standing in the journal queue for the sake of cascading
-- rollback must know the position of the previous entry in the queue, so they
-- could cut the entire tail of the list (including themselves) and roll it all
-- back on an error. The problem is that the previous entry might be already
-- gone from the queue by the time when the next entry wants to make a rollback.
-- The entries, before cutting themselves from the prev entry, must make sure
-- there is still a previous entry.
--
g.test_wal_cancel_waiting_non_first_becoming_first = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local function make_txn_fiber(id)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                s:replace{id, _G.test_data}
            end)
        end
        -- Txn 1 is in WAL.
        local f1 = make_txn_fiber(0)
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        -- Txn 2-4 wait for space in the WAL queue.
        local f2 = make_txn_fiber(1)
        local f3 = make_txn_fiber(2)
        local f4 = make_txn_fiber(3)
        -- Txn-1 is committed, txn-2 goes to WAL, 3-4 wait for
        -- space. The txn-3 becomes the first one in the
        -- space-wait-queue.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        t.assert((f1:join()))
        -- The txn3 should cascading-roll the txn4 back.
        f3:cancel()
        _G.join_with_error(f3, {type = 'FiberIsCancelled'})
        _G.join_with_error(f4, {name = 'CASCADE_ROLLBACK'})
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f2:join()))
        s:drop()
    end)
end

g.test_wal_sync_cancel = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local function make_txn_fiber(id)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                s:replace{id, _G.test_data}
            end)
        end
        -- Goes to WAL thread.
        local f1 = make_txn_fiber(1)
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        -- Gets stuck in the WAL queue.
        local f2 = make_txn_fiber(2)
        -- A special entry in the WAL queue.
        local f3 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            box.ctl.wal_sync()
        end)
        -- A normal txn after the special entry.
        local f4 = make_txn_fiber(4)
        f3:cancel()
        _G.join_with_error(f3, {type = 'FiberIsCancelled'})
        _G.join_with_error(f4, {name = 'CASCADE_ROLLBACK'})
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join()))
        t.assert((f2:join()))
        t.assert_equals(s:select(), {{1, _G.test_data}, {2, _G.test_data}})
        s:drop()
    end)
end
