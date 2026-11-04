local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that the story GC does not crash when an index extent allocation
-- fails while a checkpoint read view makes the index trees copy-on-write.
g.test_story_gc_index_oom_under_read_view = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.space.create('s')
        s:create_index('pk')
        s:create_index('sk', {parts = {{2, 'unsigned'}}, unique = false})
        for i = 1, 50 do s:replace{i, i} end
        for i = 1, 50 do s:delete{i} end

        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)
        local f = fiber.new(box.snapshot)
        f:set_joinable(true)
        t.helpers.retrying({}, function()
            t.assert_equals(#box.read_view.list(), 1)
        end)
        box.error.injection.set('ERRINJ_INDEX_ALLOC', true)
        box.internal.memtx_tx_gc(1000)
        box.error.injection.set('ERRINJ_INDEX_ALLOC', false)
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', false)
        t.assert_equals({f:join()}, {true, 'ok'})

        t.assert_equals(s:select(), {})
        t.assert_equals(s.index.sk:select(), {})
        for i = 1, 50 do s:insert{i, i} end
        t.assert_equals(s:count(), 50)
        t.assert_equals(s.index.sk:count(), 50)
    end)
end
