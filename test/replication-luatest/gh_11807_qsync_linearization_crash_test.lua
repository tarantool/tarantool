local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {
            replication_synchro_queue_max_size = 1000,
            replication_synchro_timeout = 1000,
            election_mode = 'manual',
            memtx_use_mvcc_engine = true,
        }
    })
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'fiber', require('fiber'))
        rawset(_G, 'test_data', string.rep('a', 1000))
        rawset(_G, 'make_txn_fiber', function(space, id, on_commit)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.begin()
                box.on_commit(function()
                    if on_commit then
                        on_commit()
                    end
                end)
                space:insert{id, _G.test_data}
                box.commit()
            end)
        end)

        box.ctl.promote()
        box.ctl.wait_rw()

        local s = box.schema.create_space('test', {is_sync= true})
        s:create_index('pk')
        local a = box.schema.create_space('test2')
        a:create_index('pk')
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:truncate()
        box.space.test2:truncate()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-11807: the DB state synchronization for a linearizable transaction is
-- trying to wait for all the currently known synchro txns to get confirmed so
-- it is guaranteed that any transactions previously committed on the master
-- definitely reach this replica and also get committed here (+ some other steps
-- to guarantee that).
--
-- Waiting for the last synchro txns was done in a way that if the limbo isn't
-- empty, then it 100% must contain a synchro txn in it. But it is not always
-- so. Sometimes it might contain a volatile async txn, which isn't written to
-- WAL yet. Or it might even contain dummy entries created by the limbo flush
-- operation (for a snapshot, for a new replica join). About these things the
-- linearization sync must not care and should treat them like if the limbo is
-- empty.
--
g.test_linearization_point_on_non_empty_limbo_with_no_synchro_txns_snap =
    function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local s = box.space.test
        local f3 = nil
        local f1 = _G.make_txn_fiber(s, 1)
        local f2 = _G.make_txn_fiber(s, 2, function()
            -- Try to make the linearization point **exactly** after the synchro
            -- txns f1 and f2 are committed and removed from the limbo,  but the
            -- third limbo entry from f_snap is still here.
            f3 = _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.begin({txn_isolation = 'linearizable'})
                box.commit()
            end)
        end)
        local f_snap = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            box.snapshot()
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        t.assert((f1:join()))
        t.assert((f2:join()))
        t.assert((f3:join()))
        t.assert((f_snap:join()))

        t.assert_equals(s:select{}, {{1, _G.test_data}, {2, _G.test_data}})
    end)
end

--
-- Same test, but it reproduces the same bug using an async txn instead of the
-- limbo flush operation (the test above does that via a snapshot).
--
g.test_linearization_point_on_non_empty_limbo_with_no_synchro_txns_async =
    function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local s = box.space.test
        local a = box.space.test2
        local f3 = nil
        local f1 = _G.make_txn_fiber(s, 1)
        local f2 = _G.make_txn_fiber(s, 2, function()
            -- Try to make the linearization point **exactly** after the synchro
            -- txns f1 and f2 are committed and removed from the limbo,  but the
            -- async transaction f3 is still here.
            f3 = _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.begin({txn_isolation = 'linearizable'})
                box.commit()
            end)
        end)
        local f_async = _G.make_txn_fiber(a, 3)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)

        t.assert((f1:join()))
        t.assert((f2:join()))
        t.assert((f3:join()))
        t.assert((f_async:join()))

        t.assert_equals(s:select{}, {{1, _G.test_data}, {2, _G.test_data}})
        t.assert_equals(a:select{}, {{3, _G.test_data}})
    end)
end
