local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13469-memtx-mvcc-func-key-read-view')

g.before_all(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
    cg.server:exec(function()
        box.schema.func.create('f', {
            is_deterministic = true,
            is_sandboxed = true,
            body = 'function(t) return {t[2]} end',
        })
        local s = box.schema.space.create('s')
        s:create_index('pk')
        s:create_index('f', {func = 'f', parts = {{1, 'unsigned'}}})
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that the functional key of a snapshot cleaner record outlives the
-- story it is taken from: the story is garbage collected while the read view
-- is open, and the read view count compares the key with the count key.
g.test_read_view_outlives_story = function(cg)
    local stream = cg.server.net_box:new_stream()

    cg.server:exec(function()
        box.space.s:replace{0, 5}
        box.space.s:replace{1, 7}
        box.internal.memtx_tx_gc(100)
    end)

    -- Keep the story of the deleted tuple alive until the read view is open.
    stream:begin()
    stream.space.s:get{0}
    cg.server:exec(function()
        box.space.s:delete{0}
        box.internal.memtx_tx_gc(100)
        rawset(_G, 'rv', box.read_view.open())
    end)
    stream:rollback()

    cg.server:exec(function()
        -- Garbage collect the story and reuse the memory of its key.
        box.internal.memtx_tx_gc(100)
        for i = 10, 40 do
            box.space.s:replace{i, i}
        end
        box.internal.memtx_tx_gc(100)
        local rv = rawget(_G, 'rv')
        t.assert_equals(rv.space.s.index.f:count({5}, {iterator = 'GE'}), 1)
        t.assert_equals(rv.space.s.index.f:select{}, {{1, 7}})
        rv:close()
        rawset(_G, 'rv', nil)
    end)
end
