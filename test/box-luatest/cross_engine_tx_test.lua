local t = require('luatest')

local server = require('luatest.server')

local g_mvcc_off = t.group('cross_engine_tx_mvcc_off', {
    {engine1 = 'memtx', engine2 = 'vinyl'},
    {engine1 = 'vinyl', engine2 = 'memtx'},
})

g_mvcc_off.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = false},
    })
    cg.server:start()
    cg.server:exec(function(params)
        box.schema.space.create('test1', {engine = params.engine1})
        box.space.test1:create_index('primary')
        box.schema.space.create('test2', {engine = params.engine2})
        box.space.test2:create_index('primary')
    end, {cg.params})
end)

g_mvcc_off.after_all(function(cg)
    cg.server:drop()
end)

g_mvcc_off.after_each(function(cg)
    cg.server:exec(function()
        box.space.test1:truncate()
        box.space.test2:truncate()
    end)
end)

g_mvcc_off.test_error = function(cg)
    cg.server:exec(function()
        local s1 = box.space.test1
        local s2 = box.space.test2

        box.begin()
        s1:replace({1})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'MVCC_UNAVAILABLE',
            message = "MVCC is unavailable for storage engine 'memtx' so " ..
                      "it cannot be used in the same transaction with " ..
                      "'vinyl', which supports MVCC",
            engine_with_mvcc = 'vinyl',
            engine_without_mvcc = 'memtx',
        }, s2.replace, s2, {1})
        box.commit()

        t.assert_equals(s1:select(), {{1}})
        t.assert_equals(s2:select(), {})
    end)
end

local g_mvcc_on = t.group('cross_engine_tx_mvcc_on', {
    {engine1 = 'memtx', engine2 = 'vinyl'},
    {engine1 = 'vinyl', engine2 = 'memtx'},
})

g_mvcc_on.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = true},
    })
    cg.server:start()
    cg.server:exec(function(params)
        box.schema.space.create('test1', {engine = params.engine1})
        box.space.test1:create_index('primary')
        box.schema.space.create('test2', {engine = params.engine2})
        box.space.test2:create_index('primary')
    end, {cg.params})
end)

g_mvcc_on.after_all(function(cg)
    cg.server:drop()
end)

g_mvcc_on.after_each(function(cg)
    cg.server:exec(function()
        box.space.test1:truncate()
        box.space.test2:truncate()
    end)
end)

g_mvcc_on.test_commit = function(cg)
    cg.server:exec(function()
        local s1 = box.space.test1
        local s2 = box.space.test2

        box.begin()
        s1:insert({1})
        s2:insert({1})
        box.commit()

        t.assert_equals(s1:select(), {{1}})
        t.assert_equals(s2:select(), {{1}})

        box.begin()
        s1:insert({2})
        s2:insert({2})
        s1:replace({1, 1})
        s2:replace({1, 1})
        box.commit()

        t.assert_equals(s1:select(), {{1, 1}, {2}})
        t.assert_equals(s2:select(), {{1, 1}, {2}})

        box.begin()
        t.assert_equals(s1:get(1), {1, 1})
        t.assert_equals(s2:get(1), {1, 1})
        box.commit()

        box.begin()
        t.assert_equals(s1:get(2), {2})
        t.assert_equals(s2:get(2), {2})
        s1:replace({2, 2})
        s2:replace({2, 2})
        box.commit()

        t.assert_equals(s1:select(), {{1, 1}, {2, 2}})
        t.assert_equals(s2:select(), {{1, 1}, {2, 2}})
    end)
end

g_mvcc_on.test_rollback = function(cg)
    cg.server:exec(function()
        local s1 = box.space.test1
        local s2 = box.space.test2

        box.begin()
        s1:insert({1})
        s2:insert({1})
        box.rollback()

        t.assert_equals(s1:select(), {})
        t.assert_equals(s2:select(), {})

        s1:insert({1})
        s2:insert({1})

        box.begin()
        t.assert_equals(s1:get(1), {1})
        t.assert_equals(s2:get(1), {1})
        s1:replace({2})
        s2:replace({2})
        box.rollback()

        t.assert_equals(s1:select(), {{1}})
        t.assert_equals(s2:select(), {{1}})
    end)
end

g_mvcc_on.after_test('test_send_to_read_view_on_dirty_read', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end)

g_mvcc_on.test_send_to_read_view_on_dirty_read = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local s1 = box.space.test1
        local s2 = box.space.test2

        s1:insert({1, 'a'})
        s2:insert({1, 'a'})
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f1 = fiber.new(s1.replace, s1, {1, 'b'})
        f1:set_joinable(true)
        fiber.yield()

        -- The transaction is sent to a read view because it skips
        -- an unconfirmed statement.
        box.begin({txn_isolation = 'read-confirmed'})
        t.assert_equals(s1:get(1), {1, 'a'})

        local f2 = fiber.new(s2.replace, s2, {1, 'b'})
        f2:set_joinable(true)
        fiber.yield()

        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert_equals({f1:join(5)}, {true, {1, 'b'}})
        t.assert_equals({f2:join(5)}, {true, {1, 'b'}})

        -- The transaction must continue reading from the read view
        -- in both engines.
        t.assert_equals(s1:get(1), {1, 'a'})
        t.assert_equals(s2:get(1), {1, 'a'})
        box.commit()

        t.assert_equals(s1:select(), {{1, 'b'}})
        t.assert_equals(s2:select(), {{1, 'b'}})
    end)
end

g_mvcc_on.test_send_to_read_view_on_conflict = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s1 = box.space.test1
        local s2 = box.space.test2

        s1:insert({1, 'a'})
        s2:insert({1, 'a'})

        box.begin()
        t.assert_equals(s1:get(1), {1, 'a'})

        local f = fiber.new(function()
            s1:replace({1, 'b'})
            s2:replace({1, 'b'})
        end)
        f:set_joinable(true)
        t.assert_equals({f:join(5)}, {true})

        -- The transaction must be sent to a read view in both engines.
        t.assert_equals(s1:get(1), {1, 'a'})
        t.assert_equals(s2:get(1), {1, 'a'})
        box.commit()

        t.assert_equals(s1:select(), {{1, 'b'}})
        t.assert_equals(s2:select(), {{1, 'b'}})
    end)
end

g_mvcc_on.test_abort_on_conflict = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s1 = box.space.test1
        local s2 = box.space.test2
        local err = {name = 'TRANSACTION_CONFLICT'}

        s1:insert({1, 'a'})
        s2:insert({1, 'a'})

        box.begin()
        t.assert_equals(s1:get(1), {1, 'a'})
        s2:replace({1, 'b'})

        local f = fiber.new(function()
            s1:replace({1, 'b'})
        end)
        f:set_joinable(true)
        t.assert_equals({f:join(5)}, {true})

        -- The transaction must be aborted in both engines.
        t.assert_error_covers(err, box.commit)

        t.assert_equals(s1:select(), {{1, 'b'}})
        t.assert_equals(s2:select(), {{1, 'a'}})
    end)
end

local g_misc = t.group('cross_engine_tx_misc')

g_misc.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = true},
    })
    cg.server:start()
end)

g_misc.after_all(function(cg)
    cg.server:drop()
end)

g_misc.after_test('test_vinyl_stmt_in_ddl_tx', function(cg)
    cg.server:exec(function()
        if box.space.vinyl ~= nil then
            box.space.vinyl:drop()
        end
        if box.space.memtx ~= nil then
            box.space.memtx:drop()
        end
    end)
end)

g_misc.test_vinyl_stmt_in_ddl_tx = function(cg)
    cg.server:exec(function()
        local err = {
            type = 'ClientError',
            name = 'UNSUPPORTED',
            message = 'Vinyl does not support executing a statement in ' ..
                      'a transaction that is not allowed to yield',
        }

        local vinyl = box.schema.space.create('vinyl', {engine = 'vinyl'})
        vinyl:create_index('primary')
        vinyl:create_index('secondary', {parts = {2, 'unsigned'}})

        box.begin()
        local memtx = box.schema.space.create('memtx')
        memtx:create_index('primary')
        t.assert_error_covers(err, vinyl.insert, vinyl, {1, 10})
        t.assert_error_covers(err, vinyl.replace, vinyl, {1, 10})
        t.assert_error_covers(err, vinyl.update, vinyl, {1}, {{'!', 2, 10}})
        t.assert_error_covers(err, vinyl.delete, vinyl, {1})
        t.assert_error_covers(err, vinyl.get, vinyl, {1})
        t.assert_error_covers(err, vinyl.select, vinyl)
        t.assert_error_covers(err, vinyl.index.secondary.get,
                              vinyl.index.secondary, {10})
        t.assert_error_covers(err, vinyl.index.secondary.select,
                              vinyl.index.secondary)
        box.commit()

        t.assert_equals(vinyl:select(), {})
        t.assert_equals(memtx:select(), {})
    end)
end
