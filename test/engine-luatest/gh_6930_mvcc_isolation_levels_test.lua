local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new{
        alias   = 'default',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_mvcc_isolation_level_errors = function(cg)
    cg.server:exec(function()
        t.assert_error_msg_content_equals(
            "Illegal parameters, txn_isolation must be one of " ..
            "box.txn_isolation_level (keys or values)",
            function() box.begin{txn_isolation = 'avadakedavra'} end)
        t.assert_error_msg_content_equals(
            "Incorrect value for option 'txn_isolation': must " ..
            "be one of box.txn_isolation_level (keys or values)",
            function() box.cfg{txn_isolation = 'avadakedavra'} end)
        t.assert_error_msg_content_equals(
            "Illegal parameters, txn_isolation must be one of " ..
            "box.txn_isolation_level (keys or values)",
            function() box.begin{txn_isolation = false} end)
        t.assert_error_msg_content_equals(
            "Incorrect value for option 'txn_isolation': " ..
            "should be one of types string, number",
            function() box.cfg{txn_isolation = false} end)
        t.assert_error_msg_content_equals(
            "Illegal parameters, txn_isolation must be one of " ..
            "box.txn_isolation_level (keys or values)",
            function() box.begin{txn_isolation = 8} end)
        t.assert_error_msg_content_equals(
            "Incorrect value for option 'txn_isolation': must " ..
            "be one of box.txn_isolation_level (keys or values)",
            function() box.cfg{txn_isolation = 8} end)
        t.assert_error_msg_content_equals(
            "Incorrect value for option 'txn_isolation': " ..
            "cannot set default transaction isolation to 'default'",
            function() box.cfg{txn_isolation = 'default'} end)
    end)
end

g.before_test('test_mvcc_isolation_level_basics', function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('primary')
    end, {cg.params.engine})
end)

g.test_mvcc_isolation_level_basics = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test

        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            s:insert{1}
        end)

        t.assert_equals(s:select(), {})
        t.assert_equals(s:count(), 0)

        box.begin()
        local res1 = s:select()
        local res2 = s:count()
        box.commit()
        t.assert_equals(res1, {})
        t.assert_equals(res2, 0)

        local expect0 = {'default', 'read-confirmed', 'best-effort',
                         box.txn_isolation_level.DEFAULT,
                         box.txn_isolation_level.READ_CONFIRMED,
                         box.txn_isolation_level.BEST_EFFORT,
                         box.txn_isolation_level['default'],
                         box.txn_isolation_level['read-confirmed'],
                         box.txn_isolation_level['best-effort']}

        for _,level in pairs(expect0) do
            box.begin{txn_isolation = level}
            res1 = s:select()
            res2 = s:count()
            box.commit()
            t.assert_equals(res1, {})
            t.assert_equals(res2, 0)
        end

        local expect0 = {'read-confirmed', 'best-effort',
                         box.txn_isolation_level.READ_CONFIRMED,
                         box.txn_isolation_level.BEST_EFFORT,
                         box.txn_isolation_level['read-confirmed'],
                         box.txn_isolation_level['best-effort']}

        for _,level in pairs(expect0) do
            box.cfg{txn_isolation = level}
            box.begin{}
            res1 = s:select()
            res2 = s:count()
            box.commit()
            t.assert_equals(res1, {})
            t.assert_equals(res2, 0)
            box.begin{txn_isolation = 'default'}
            res1 = s:select()
            res2 = s:count()
            box.commit()
            t.assert_equals(res1, {})
            t.assert_equals(res2, 0)
            box.cfg{txn_isolation = 'best-effort'}
        end

        local expect1 = {'read-committed',
                         box.txn_isolation_level.READ_COMMITTED,
                         box.txn_isolation_level['read-committed']}

        for _,level in pairs(expect1) do
            box.begin{txn_isolation = level}
            res1 = s:select()
            res2 = s:count()
            box.commit()
            t.assert_equals(res1, {{1}})
            t.assert_equals(res2, 1)
        end

        for _,level in pairs(expect1) do
            box.cfg{txn_isolation = level}
            box.begin{txn_isolation = level}
            res1 = s:select()
            res2 = s:count()
            box.commit()
            t.assert_equals(res1, {{1}})
            t.assert_equals(res2, 1)
            -- txn_isolation does not affect autocommit select,
            -- which is always run as read-confirmed
            t.assert_equals(s:select(), {})
            t.assert_equals(s:count(), 0)
            box.cfg{txn_isolation = 'best-effort'}
        end

        -- With default best-effort isolation RO->RW transaction can be aborted:
        box.begin()
        res1 = s:select(1) -- read confirmed {}
        t.assert_error_msg_content_equals(
            "Transaction has been aborted by conflict",
            function() s:replace{2} end)
        t.assert_error_msg_content_equals(
            "Transaction has been aborted by conflict",
            function() box.commit() end)
        t.assert_equals(res1, {})

        -- But using 'read-committed' allows to avoid conflict:
        box.begin{txn_isolation = 'read-committed'}
        res1 = s:select(1) -- read confirmed {{1}}
        s:replace{2}
        box.commit()
        t.assert_equals(res1, {{1}})
        t.assert_equals(s:select{}, {{1}, {2}})

        f:join()
    end)
end

g.after_test('test_mvcc_isolation_level_basics', function(cg)
    cg.server:exec(function()
        local s = box.space.test
        if s then
            s:drop()
        end
    end)
end)

g.before_test('test_mvcc_best_effort_read_view', function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('primary')
    end, {cg.params.engine})
end)

g.test_mvcc_best_effort_read_view = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        local ch = fiber.channel(1)
        fiber.create(function() s:replace{1} end)
        fiber.create(function() s:replace{2} ch:put(true) end)
        box.begin({txn_isolation = 'best-effort'})
        t.assert_equals(s:select(2), {})
        t.assert(ch:get(5))
        t.assert_equals(s:select(1), {{1}})
        box.commit()
    end)
end

g.after_test('test_mvcc_best_effort_read_view', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local s = box.space.test
        if s then
            s:drop()
        end
    end)
end)
