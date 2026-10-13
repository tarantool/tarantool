local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13411-memtx-mvcc-count-tracker-bound')

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that a count tracker bounded by an index entry survives the rollback
-- and garbage collection of the story of the bound: the bound is an invisible
-- replacement of a tuple, so the counting transaction does not read it.
g.test_count_tracker_bound_rolled_back = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.space.create('s')
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
        s:replace{1, 0}
        s:replace{2, 0}
        box.internal.memtx_tx_gc(100)

        -- tx1 replaces {2, 0}: the index holds the uncommitted {2, 0, 'tx1'}.
        local tx1 = fiber.channel()
        local f1 = fiber.create(function()
            box.begin()
            s:replace{2, 0, 'tx1'}
            tx1:get()
            box.rollback()
        end)
        f1:set_joinable(true)

        -- tx2 skips {1, 0} and reaches the index entry {2, 0, 'tx1'}: its
        -- count tracker is bounded by the raw entry, but the transaction reads
        -- the visible {2, 0} and does not keep the bound alive.
        local tx2 = fiber.channel()
        local f2 = fiber.create(function()
            box.begin()
            t.assert_equals(s:select({}, {offset = 1}), {{2, 0}})
            tx2:get()
            t.assert_equals(s:select({}, {offset = 1}), {{2, 0}})
            box.commit()
        end)
        f2:set_joinable(true)

        -- tx1 rolls back: the story of the bound is unreferenced and
        -- collected once the Lua garbage collection drops the tuple.
        tx1:put(true)
        t.assert(f1:join())
        collectgarbage('collect')
        box.internal.memtx_tx_gc(100)

        -- A write matching the count tracker is compared with the bound: the
        -- first key parts are equal, so the comparison reads the tuple data.
        s:replace{2, 1}

        tx2:put(true)
        t.assert(f2:join())
    end)
end
