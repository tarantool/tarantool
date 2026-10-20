local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13477-memtx-mvcc-disabled-func-index', t.helpers.matrix{
    memtx_use_mvcc_engine = {false, true},
})

g.before_all(function(cg)
    cg.server = server:new{
        box_cfg = {memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine},
    }
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that a space with a disabled functional index, whose function is
-- detached by the deletion of its _func_index row, accepts writes: the index
-- has no function to build the keys with and holds no entries. The index is
-- rebuilt once its row is inserted back.
g.test_disabled_func_index_write = function(cg)
    cg.server:exec(function()
        box.schema.func.create('f', {
            body = 'function(tuple) return {tuple[2]} end',
            is_deterministic = true,
            is_sandboxed = true,
        })
        local s = box.schema.space.create('s')
        s:create_index('pk')
        local sk = s:create_index('sk', {
            parts = {{1, 'unsigned'}},
            func = box.func.f.id,
        })
        s:insert{1, 10}
        local binding = box.space._func_index:delete{s.id, sk.id}
        t.assert_not_equals(binding, nil)

        t.assert_equals(s:insert{2, 20}, {2, 20})
        t.assert_equals(s:replace{1, 30}, {1, 30})
        t.assert_equals(s:update(2, {{'=', 2, 40}}), {2, 40})
        t.assert_equals(s:delete{1}, {1, 30})
        t.assert_equals(s:select{}, {{2, 40}})
        -- The disabled index serves no reads.
        t.assert_error_msg_content_equals(
            "Index 'sk' (TREE) of space 's' (memtx) does not support " ..
            "iterator", function() s.index.sk:select{} end)

        box.space._func_index:insert(binding)
        t.assert_equals(s.index.sk:select{}, {{2, 40}})
        s:insert{3, 50}
        t.assert_equals(s.index.sk:select{}, {{2, 40}, {3, 50}})
    end)
end
