local t = require('luatest')

local server = require('luatest.server')

local g = t.group('cross_engine_tx', {
    {mvcc = true,  engine1 = 'memtx', engine2 = 'vinyl'},
    {mvcc = true,  engine1 = 'vinyl', engine2 = 'memtx'},
    {mvcc = false, engine1 = 'memtx', engine2 = 'vinyl'},
    {mvcc = false, engine1 = 'vinyl', engine2 = 'memtx'},
})

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = cg.params.mvcc},
    })
    cg.server:start()
    cg.server:exec(function(params)
        box.schema.space.create('test1', {engine = params.engine1})
        box.space.test1:create_index('primary')
        box.schema.space.create('test2', {engine = params.engine2})
        box.space.test2:create_index('primary')
    end, {cg.params})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.space.test1:truncate()
        box.space.test2:truncate()
    end)
end)

g.test_commit = function(cg)
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

g.test_rollback = function(cg)
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

g.test_abort_on_yield = function(cg)
    t.skip_if(cg.params.mvcc or
              cg.params.engine1 ~= 'memtx' or
              cg.params.engine2 ~= 'vinyl',
              'test space supports mvcc')
    cg.server:exec(function()
        local fiber = require('fiber')
        local memtx = box.space.test1
        local vinyl = box.space.test2
        local err = {name = 'TRANSACTION_YIELD'}

        box.begin()
        memtx:insert({1})
        vinyl:insert({1})
        fiber.yield()
        t.assert_error_covers(err, memtx.get, memtx, 1)
        t.assert_error_covers(err, memtx.replace, memtx, {1, 1})
        t.assert_error_covers(err, vinyl.get, vinyl, 1)
        t.assert_error_covers(err, vinyl.replace, vinyl, {1, 1})
        t.assert_error_covers(err, box.commit)

        t.assert_equals(memtx:select(), {})
        t.assert_equals(vinyl:select(), {})

        memtx:replace({1})
        vinyl:replace({1})
        box.snapshot()

        box.begin()
        t.assert_equals(memtx:get(1), {1})
        t.assert_equals(vinyl:get(1), {1}) -- implicit yield
        t.assert_error_covers(err, box.commit)
    end)
end

g.test_abort_on_dirty_read = function(cg)
    t.skip_if(cg.params.mvcc or
              cg.params.engine1 ~= 'memtx' or
              cg.params.engine2 ~= 'vinyl',
              'test space supports mvcc')
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local memtx = box.space.test1
        local vinyl = box.space.test2
        local err = {name = 'TRANSACTION_CONFLICT'}

        memtx:insert({1, 'a'})
        vinyl:insert({1, 'a'})
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        fiber.create(vinyl.replace, vinyl, {1, 'b'})

        box.begin({txn_isolation = 'read-confirmed'})
        t.assert_equals(vinyl:get(1), {1, 'a'})
        -- The transaction was sent to a read view so it's impossible to use
        -- memtx without MVCC.
        t.assert_error_covers(err, memtx.get, memtx, 1)
        t.assert_error_covers(err, box.commit)

        box.begin({txn_isolation = 'read-confirmed'})
        t.assert_equals(memtx:get(1), {1, 'a'})
        -- It's impossible to send the transaction to a read view because
        -- it uses memtx without MVCC.
        t.assert_error_covers(err, vinyl.get, vinyl, 1)
        t.assert_error_covers(err, box.commit)
    end)
end

g.test_send_to_read_view_on_dirty_read = function(cg)
    t.skip_if(not cg.params.mvcc, 'mvcc is off')
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

g.test_send_to_read_view_on_conflict = function(cg)
    t.skip_if(not cg.params.mvcc, 'mvcc is off')
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

g.test_abort_on_conflict = function(cg)
    t.skip_if(not cg.params.mvcc, 'mvcc is off')
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
