local t = require('luatest')
local server = require('luatest.server')

local function before_all(cg, box_cfg)
    cg.server = server:new({box_cfg = box_cfg})
    cg.server:start()
    cg.server:exec(function()
        -- A trigger that saves everything from `iterator' into `_G.result'.
        rawset(_G, 'on_rollback_trigger', function(iterator)
            local result = {}
            for num, old_tuple, new_tuple, space_id in iterator() do
                table.insert(result, {
                    num = num, space_id = space_id,
                    old_tuple = old_tuple, new_tuple = new_tuple
                })
            end
            rawset(_G, 'result', result)
        end)
    end)
end

local function after_all(cg)
    cg.server:drop()
end

local function after_each(cg)
    cg.server:exec(function()
        box.space.test1:drop()
        box.space.test2:drop()
    end)
end

-- Enable MVCC to abort transactions by a timeout or by a conflict.
local g_mvcc_on = t.group('gh-9340-mvcc-on', {{engine = 'memtx'},
                                              {engine = 'vinyl'}})
g_mvcc_on.before_all(function(cg)
    before_all(cg, {memtx_use_mvcc_engine = true})
end)
g_mvcc_on.after_all(after_all)
g_mvcc_on.after_each(after_each)

-- Check arguments of the `on_rollback' triggers.
g_mvcc_on.test_trigger_args = function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local s1 = box.schema.space.create('test1', {engine = engine})
        local s2 = box.schema.space.create('test2', {engine = engine})
        s1:create_index('pk')
        s2:create_index('pk')
        s1:on_replace(function() box.on_rollback(_G.on_rollback_trigger) end)

        -- Check rollback by `box.rollback()'.
        box.begin()
        s1:insert{1}
        s2:insert{2}
        box.rollback()
        t.assert_equals(_G.result, {
            {num = 1, space_id = 513, old_tuple = nil, new_tuple = {2}},
            {num = 2, space_id = 512, old_tuple = nil, new_tuple = {1}},
        })

        -- Check rollback by a conflict.
        box.begin()
        s1:insert{3}
        s2:insert{4}
        local f = fiber.new(s1.insert, s1, {3, 3})
        f:set_joinable(true)
        t.assert_equals({f:join()}, {true, {3, 3}})
        local errmsg = 'Transaction has been aborted by conflict'
        t.assert_error_msg_equals(errmsg, s1.insert, s1, {33})
        t.assert_error_msg_equals(errmsg, s2.insert, s2, {44})
        t.assert_error_msg_equals(errmsg, box.commit)
        t.assert_equals(_G.result, {
            {num = 1, space_id = 513, old_tuple = nil, new_tuple = {4}},
            {num = 2, space_id = 512, old_tuple = nil, new_tuple = {3}},
        })

        -- Check rollback by a timeout.
        box.begin({timeout = 0.01})
        s1:insert{5}
        s2:insert{6}
        fiber.sleep(0.1)
        local errmsg = 'Transaction has been aborted by timeout'
        t.assert_error_msg_equals(errmsg, s1.insert, s1, {55})
        t.assert_error_msg_equals(errmsg, s2.insert, s2, {66})
        t.assert_error_msg_equals(errmsg, box.commit)
        t.assert_equals(_G.result, {
            {num = 1, space_id = 513, old_tuple = nil, new_tuple = {6}},
            {num = 2, space_id = 512, old_tuple = nil, new_tuple = {5}},
        })

        -- Check that rollback of a single statement (without full transaction
        -- rollback) doesn't invoke `on_rollback' triggers.
        local function error_in_on_replace()
            error('err')
        end
        s2:on_replace(error_in_on_replace)
        _G.result = {}
        box.begin()
        s1:insert{7}
        t.assert_error_msg_content_equals('err', s2.insert, s2, {77})
        box.commit()
        s2:on_replace(nil, error_in_on_replace)
        t.assert_equals(_G.result, {})

        -- Check rollback to savepoint (with empty txn).
        box.begin()
        local svp = box.savepoint()
        box.rollback_to_savepoint(svp)
        t.assert_equals(_G.result, {})
        box.commit()
        t.assert_equals(_G.result, {})

        -- Check rollback to savepoint (with txn commit).
        box.begin()
        s1:insert{8}
        s2:insert{9}
        local svp1 = box.savepoint()
        s1:insert{10}
        local svp2 = box.savepoint()
        s2:insert{11}
        box.rollback_to_savepoint(svp2)
        t.assert_equals(_G.result, {
            {num = 1, space_id = 513, old_tuple = nil, new_tuple = {11}},
        })
        s1:insert{12}
        box.rollback_to_savepoint(svp1)
        t.assert_equals(_G.result, {
            {num = 1, space_id = 512, old_tuple = nil, new_tuple = {12}},
            {num = 2, space_id = 512, old_tuple = nil, new_tuple = {10}},
        })
        _G.result = {}
        box.commit()
        t.assert_equals(_G.result, {})

        -- Check rollback to savepoint (with txn rollback).
        box.begin()
        s1:insert{13}
        s2:insert{14}
        local svp = box.savepoint()
        s1:insert{15}
        s2:insert{16}
        box.rollback_to_savepoint(svp)
        t.assert_equals(_G.result, {
            {num = 1, space_id = 513, old_tuple = nil, new_tuple = {16}},
            {num = 2, space_id = 512, old_tuple = nil, new_tuple = {15}},
        })
        s1:insert{17}
        s2:insert{18}
        box.rollback()
        t.assert_equals(_G.result, {
            {num = 1, space_id = 513, old_tuple = nil, new_tuple = {18}},
            {num = 2, space_id = 512, old_tuple = nil, new_tuple = {17}},
            {num = 3, space_id = 513, old_tuple = nil, new_tuple = {14}},
            {num = 4, space_id = 512, old_tuple = nil, new_tuple = {13}},
        })
    end, {cg.params.engine})
end

-- Tests with error injection to force rollback due to WAL failure.
g_mvcc_on.test_trigger_args_debug = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(engine)
        local s1 = box.schema.space.create('test1', {engine = engine})
        local s2 = box.schema.space.create('test2', {engine = engine})
        s1:create_index('pk')
        s2:create_index('pk')
        s1:on_replace(function() box.on_rollback(_G.on_rollback_trigger) end)

        -- Check rollback by a WAL I/O error.
        box.begin()
        s1:insert{1}
        s2:insert{2}
        box.error.injection.set("ERRINJ_WAL_IO", true)
        t.assert_error_msg_equals('Failed to write to disk', box.commit)
        box.error.injection.set("ERRINJ_WAL_IO", false)
        t.assert_equals(_G.result, {
            {num = 1, space_id = 513, old_tuple = nil, new_tuple = {2}},
            {num = 2, space_id = 512, old_tuple = nil, new_tuple = {1}},
        })

        -- Check that `on_rollback' triggers are called with correct arguments
        -- for single-statement transactions as well.
        box.error.injection.set("ERRINJ_WAL_IO", true)
        t.assert_error_msg_equals('Failed to write to disk', s1.insert, s1, {3})
        box.error.injection.set("ERRINJ_WAL_IO", false)
        t.assert_equals(_G.result, {
            {num = 1, space_id = 512, old_tuple = nil, new_tuple = {3}},
        })
    end, {cg.params.engine})
end

-- Force disable MVCC to abort the transaction by fiber yield.
local g_mvcc_off = t.group('gh-9340-mvcc-off-memtx')
g_mvcc_off.before_all(function(cg)
    before_all(cg, {memtx_use_mvcc_engine = false})
end)
g_mvcc_off.after_all(after_all)
g_mvcc_off.after_each(after_each)

-- Check arguments of the `on_rollback' triggers.
g_mvcc_off.test_trigger_args = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s1 = box.schema.space.create('test1', {engine = 'memtx'})
        local s2 = box.schema.space.create('test2', {engine = 'memtx'})
        s1:create_index('pk')
        s2:create_index('pk')

        -- Check rollback by `fiber.yield()'.
        box.begin()
        box.on_rollback(_G.on_rollback_trigger)
        s1:insert{1}
        s2:insert{2}
        fiber.yield()
        local errmsg = 'Transaction has been aborted by a fiber yield'
        t.assert_error_msg_equals(errmsg, s1.insert, s1, {11})
        t.assert_error_msg_equals(errmsg, s2.insert, s2, {22})
        t.assert_error_msg_equals(errmsg, box.commit)
        t.assert_equals(_G.result, {
            {num = 1, space_id = 513, old_tuple = nil, new_tuple = {2}},
            {num = 2, space_id = 512, old_tuple = nil, new_tuple = {1}},
        })
    end)
end
