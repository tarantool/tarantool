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

g.test_memtx_read_view_stats = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        -- Helper function that frees tuples referenced from Lua.
        local function gc()
            box.tuple.new() -- drop blessed tuple ref
            collectgarbage('collect') -- drop Lua refs
        end

        local s = box.schema.create_space('test')
        s:create_index('pk')

        -- Insert one tuple.
        gc()
        local stat1 = box.stat.memtx()
        s:insert({1})
        gc()
        local stat2 = box.stat.memtx()
        local data_size = stat2.data.total - stat1.data.total
        local index_size = stat2.index.total - stat1.index.total
        t.assert_gt(data_size, 0)
        t.assert_gt(index_size, 0)
        t.assert_equals(stat2.index.read_view, 0)
        t.assert_equals(stat2.data.read_view, 0)
        t.assert_equals(stat2.data.garbage, 0)

        -- Start a snapshot to create a system read view.
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)
        local f = fiber.new(box.snapshot)
        f:set_joinable(true)
        fiber.yield()
        local stat3 = box.stat.memtx()
        t.assert_equals(stat3, stat2)

        -- Replace a tuple to do CoW.
        s:replace({1})
        gc()
        local stat4 = box.stat.memtx()
        t.assert_equals(stat4.index.total - stat3.index.total, index_size)
        t.assert_equals(stat4.index.read_view, index_size)
        t.assert_equals(stat4.data.total - stat3.data.total, data_size)
        t.assert_equals(stat4.data.read_view, data_size)
        t.assert_equals(stat4.data.garbage, 0)

        -- Complete the snapshot.
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', false)
        t.assert(f:join())
        local stat5 = box.stat.memtx()
        t.assert_equals(stat5.index.total - stat3.index.total, 0)
        t.assert_equals(stat5.index.read_view, 0)
        t.assert_equals(stat5.data.total - stat3.data.total, data_size)
        t.assert_equals(stat5.data.read_view, 0)
        t.assert_equals(stat5.data.garbage, data_size)

        -- Replace a tuple to collect garbage.
        s:replace({1})
        gc()
        local stat6 = box.stat.memtx()
        t.assert_equals(stat6, stat2)

        s:drop()
    end)
end
