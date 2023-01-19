local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{
        alias   = 'default',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.test_mvcc_txm_story_gc_does_not_break_memtx_tree_iter = function()
    g.server:exec(function()
        local t = require('luatest')

        local s = box.schema.space.create('test')
        local idx_parts = {
            {field = 1, type = 'unsigned'},
            {field = 2, type = 'unsigned'}
        }
        local idx = s:create_index('primary', {parts = idx_parts})

        local space_sz = 16
        for i = 1, space_sz do
            s:insert({i, i})
        end

        local offs = 2
        local err_msg = 'MVCC transaction manager story garbage collection ' ..
                        'breaks memtx TREE index iterator'
        for i = 1, space_sz - offs do
            box.begin()
            s:delete{i, i}
            for _, tuple in idx:pairs{i + offs} do
                if i + offs ~= tuple[1] then
                    box.rollback()
                    t.assert_equals(i + offs, tuple[1], err_msg)
                end
            end
            box.commit()
        end
    end)
end
