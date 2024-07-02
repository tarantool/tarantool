local server = require('luatest.server')
local t = require('luatest')

local g = t.group('commit{wait=...}', {
    {engine = 'memtx'},
    {engine = 'memtx', mvcc = true},
    {engine = 'vinyl'},
})

--
-- gh-67: wal_mode='async' which was turned into a commit{wait=...} feature.
--
g.before_all(function(cg)
    cg.server = server:new({
        alias = 'master',
        box_cfg = {
            memtx_use_mvcc_engine = cg.params.mvcc,
        },
    })
    cg.server:start()
    cg.server:exec(function(engine)
        box.schema.create_space('test', {engine = engine}):create_index('pk')

        rawset(_G, 'fiber', require('fiber'))
        rawset(_G, 'test_result', 0)
        rawset(_G, 'test_cond', _G.fiber.cond())
        rawset(_G, 'set_triggers', function()
            box.on_commit(function()
                _G.test_result = 1
                _G.test_cond:signal()
            end)
            box.on_rollback(function()
                _G.test_result = -1
                _G.test_cond:signal()
            end)
        end)
        rawset(_G, 'wait_complete', function()
            while _G.test_result == 0 do
                _G.test_cond:wait()
            end
        end)
        rawset(_G, 'manual_atomic', function(options, func)
            box.begin()
            local ok = pcall(func)
            t.assert(ok)
            box.commit(options)
        end)
    end, {cg.params.engine})
    cg.are_txns_isolated = cg.params.mvcc or cg.params.engine == 'vinyl'
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:truncate()
    end)
end)

g.after_all(function(cg)
    cg.server:stop()
end)

local test_basic_impl = function(cg, atomic_func_name)
    cg.server:exec(function(are_txns_isolated, atomic_func_name)
        local atomic_func = _G[atomic_func_name]
        --
        -- Trivial complete, default behaviour.
        --
        _G.test_result = 0
        local csw = _G.fiber.self():csw()
        atomic_func({wait = 'complete'}, function()
            box.space.test:replace{1}
            _G.set_triggers()
        end)
        t.assert_gt(_G.fiber.self():csw(), csw)
        t.assert_equals(_G.test_result, 1)
        --
        -- Submit returns right away (when the journal queue isn't full).
        --
        _G.test_result = 0
        csw = _G.fiber.self():csw()
        atomic_func({wait = 'submit'}, function()
            box.space.test:replace{2}
            _G.set_triggers()
        end)
        t.assert_equals(_G.fiber.self():csw(), csw)
        t.assert_equals(_G.test_result, 0)
        if are_txns_isolated then
            t.assert_equals(box.space.test:select{}, {{1}})
        end
        _G.wait_complete()
        t.assert_equals(_G.test_result, 1)
        t.assert_equals(box.space.test:select{}, {{1}, {2}})

        box.space.test:truncate()
    end, {cg.are_txns_isolated, atomic_func_name})
end

g.test_basic_commit = function(cg)
    test_basic_impl(cg, 'manual_atomic')
    cg.server:exec(function()
        --
        -- Unknown mode in commit.
        --
        _G.test_result = 0
        t.assert_equals(box.space.test:select{}, {})
        box.begin()
        box.space.test:replace{1}
        _G.set_triggers()
        local csw = _G.fiber.self():csw()
        t.assert_error_msg_contains("unknown 'wait' mode",
                                    box.commit, {wait = 'trash'})
        t.assert_equals(_G.fiber.self():csw(), csw)
        t.assert_equals(_G.test_result, 0)
        t.assert(box.is_in_txn())
        box.commit({wait = 'complete'})
        t.assert_gt(_G.fiber.self():csw(), csw)
        t.assert_equals(_G.test_result, 1)
        t.assert_equals(box.space.test:select{}, {{1}})
    end)
end

--
-- Submit with the full journal queue is blocked until the queue gets free space
-- and the txn is sent to the journal.
--
local function test_full_journal_submit_impl(cg, atomic_func_name)
    cg.server:exec(function(are_txns_isolated, atomic_func_name)
        local atomic_func = _G[atomic_func_name]
        local old_size = box.cfg.wal_queue_max_size
        box.cfg{wal_queue_max_size = 1}
        local csw = _G.fiber.self():csw()
        atomic_func({wait = 'submit'}, function()
            box.space.test:replace{1}
        end)
        t.assert_equals(_G.fiber.self():csw(), csw)
        if are_txns_isolated then
            t.assert_equals(box.space.test:select{}, {})
        end

        _G.test_result = 0
        csw = _G.fiber.self():csw()
        atomic_func({wait = 'submit'}, function()
            box.space.test:replace{2}
            _G.set_triggers()
        end)
        t.assert_gt(_G.fiber.self():csw(), csw)
        -- Still not ready. Submit was blocked until journal queue had space,
        -- but then the txn is sent to journal and the fiber moves on without
        -- waiting for the journal write result.
        t.assert_equals(_G.test_result, 0)
        if are_txns_isolated then
            t.assert_equals(box.space.test:select{}, {{1}})
        end
        _G.wait_complete()
        t.assert_equals(box.space.test:select{}, {{1}, {2}})
        box.cfg{wal_queue_max_size = old_size}
    end, {cg.are_txns_isolated, atomic_func_name})
end
g.test_full_journal_submit_commit = function(cg)
    test_full_journal_submit_impl(cg, 'manual_atomic')
end

--
-- Submit returns ok even if later the txn fails.
--
local function test_rollback_impl(cg, atomic_func_name)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(atomic_func_name)
        local atomic_func = _G[atomic_func_name]
        _G.test_result = 0
        local csw = _G.fiber.self():csw()
        atomic_func({wait = 'submit'}, function()
            box.space.test:replace{1}
            _G.set_triggers()
            box.error.injection.set("ERRINJ_WAL_FALLOCATE", 1)
        end)
        t.assert_equals(_G.fiber.self():csw(), csw)

        t.assert_equals(_G.test_result, 0)
        _G.wait_complete()
        t.assert_equals(_G.test_result, -1)
        t.assert_equals(box.error.injection.get("ERRINJ_WAL_FALLOCATE"), 0)
    end, {atomic_func_name})
end
g.test_rollback_commit = function(cg)
    test_rollback_impl(cg, 'manual_atomic')
end

--
-- Submit is actually not finishing the txn instantly after commit is started.
--
local function test_delay_impl(cg, atomic_func_name)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(atomic_func_name)
        local atomic_func = _G[atomic_func_name]
        _G.test_result = 0
        local csw = _G.fiber.self():csw()
        atomic_func({wait = 'submit'}, function()
            box.space.test:replace{1}
            _G.set_triggers()
            box.error.injection.set("ERRINJ_WAL_DELAY", true)
        end)
        t.assert_equals(_G.fiber.self():csw(), csw)

        t.assert_equals(_G.test_result, 0)
        _G.fiber.sleep(0.01)
        t.assert_equals(_G.test_result, 0)
        box.error.injection.set("ERRINJ_WAL_DELAY", false)
        _G.wait_complete()
        t.assert_equals(_G.test_result, 1)
    end, {atomic_func_name})
end
g.test_delay_commit = function(cg)
    test_delay_impl(cg, 'manual_atomic')
end
