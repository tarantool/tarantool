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

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_iterator_restore_after_unconfirmed_tuple = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk')
        s:insert({1})
        s:insert({10})
        box.snapshot()

        -- Add chain [-inf, 1] to the cache.
        t.assert_equals(s:select({}, {limit = 1}), {{1}})

        -- Make index.select() block on disk read after fetching [1]
        -- from the cache.
        box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', true)

        local f1 = fiber.new(box.atomic, {txn_isolation = 'read-committed'},
                             s.select, s, {}, {fullscan = true})
        f1:set_joinable(true)
        fiber.yield()

        local f2 = fiber.new(box.atomic, {txn_isolation = 'read-confirmed'},
                             s.select, s, {}, {fullscan = true})
        f2:set_joinable(true)
        fiber.yield()

        -- Insert a new unconfirmed tuple into the memory level to make
        -- index.select() restore the memory iterator after disk read.
        -- The firber reading committed tuples should restore at [5].
        -- The fiber reading confirmed tuples should skip [5] and not
        -- add chain [1, 10] to the cache because of it.
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f3 = fiber.new(s.replace, s, {5})
        f3:set_joinable(true)
        fiber.yield()

        -- Resume iteration and check the results.
        box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', false)
        local committed = {{1}, {5}, {10}}
        local confirmed = {{1}, {10}}
        t.assert_equals({f1:join(10)}, {true, committed})
        t.assert_equals({f2:join(10)}, {true, confirmed})

        t.assert_equals(box.atomic({txn_isolation = 'read-committed'},
                                   s.select, s, {}, {fullscan = true}),
                        committed)
        t.assert_equals(box.atomic({txn_isolation = 'read-confirmed'},
                                   s.select, s, {}, {fullscan = true}),
                        confirmed)

        -- Confirm the pending tuple and check the results.
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        f3:join()

        t.assert_equals(box.atomic({txn_isolation = 'read-committed'},
                                   s.select, s, {}, {fullscan = true}),
                        committed)
        t.assert_equals(box.atomic({txn_isolation = 'read-confirmed'},
                                   s.select, s, {}, {fullscan = true}),
                        committed)
    end)
end
