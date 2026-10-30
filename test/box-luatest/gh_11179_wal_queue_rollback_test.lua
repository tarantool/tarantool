local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_test('test_wal_queue_rollback_in_flight', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        box.space.test:drop()
    end)
end)

g.test_wal_queue_rollback_in_flight = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.create_space('test')
        s:create_index('pk')
        box.cfg{wal_queue_max_size = 100}
        s:insert({1})
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        -- In case txn in WAL queue (f2) is not rollbacked we get duplicate
        -- error on rollback of in-flight txn (f1) and as a result failed
        -- assertion or panic.
        local f1 = fiber.new(function()
            box.begin()
            s:delete({1})
            s:insert({100, string.rep('a', 1000)})
            box.commit()
        end)
        f1:set_joinable(true)
        fiber.yield()
        local f2 = fiber.new(function()
            s:insert({1})
        end)
        f2:set_joinable(true)
        fiber.yield()
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok, err = f1:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.WAL_IO,
            message = 'Failed to write to disk',
        })
        local ok, err = f2:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.CASCADE_ROLLBACK,
            message = 'WAL has a rollback in progress',
        })
        t.assert_equals(s:select(), {{1}})
    end)
end

g.after_test('test_wal_queue_rollback_cascade', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.error.injection.set('ERRINJ_WAL_IO', false)
        box.space.test:drop()
    end)
end)

--
-- Here we test a different situation. Part of in-flight requests can be
-- successfully written and part of requests are not. Imagine also in-flight
-- requests are split into 2 batches. The first has both successful and not
-- successful and the second has only unsuccessful requests. So when first
-- batch is returned to TX thread we don't proceed with rollback yet
-- waiting for the second batch. But we complete the successful part of
-- the batch and thus wakeup journal queue. Woken up fiber will try
-- to submit new request to WAL but fail as cascade rollback is in
-- progress. The failed request from journal will be rolled back and
-- this is not correct. First we should rollback newer requests form the
-- journal queue.
--
-- Why do we need two batches here? With only one batch we first rollback
-- failed requests from batch and thus rollback journal queue.
--
-- Note also that this situation it hard to reproduce directly. Thus it
-- modelled here by ERRINJ_WAL_IO injection.
--
g.test_wal_queue_rollback_cascade = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:insert({1})
        box.cfg{wal_queue_max_size = 100}
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f1 = fiber.new(function()
            box.begin()
            s:insert({100, string.rep('a', 1000)})
            box.commit()
        end)
        f1:set_joinable(true)
        fiber.yield()
        -- In case txn in WAL queue (f3) is not rollbacked we get duplicate
        -- error on rollback of in-flight txn (f2) and as a result failed
        -- assertion or panic.
        local f2 = fiber.new(function()
            s:delete({1})
        end)
        f2:set_joinable(true)
        fiber.yield()
        local f3 = fiber.new(function()
            s:insert({1})
        end)
        f3:set_joinable(true)
        fiber.yield()
        box.error.injection.set('ERRINJ_WAL_IO', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert_equals({f1:join()}, {true})
        local ok, err = f2:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.WAL_IO,
            message = 'Failed to write to disk',
        })
        local ok, err = f3:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.CASCADE_ROLLBACK,
            message = 'WAL has a rollback in progress',
        })
        t.assert_equals(s:select({100}, {iterator = 'lt'}), {{1}})
    end)
end
