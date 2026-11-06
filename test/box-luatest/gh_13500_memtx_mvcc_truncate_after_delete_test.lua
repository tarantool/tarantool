local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13500-memtx-mvcc-truncate-after-delete')

g.before_all(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that truncate after a delete in the same transaction neither leaves
-- count trackers on the indexes of the new space nor fails.
g.test_truncate_after_delete = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('pk')
        s:create_index('sk', {parts = {{2, 'unsigned'}}, unique = false})
        s:insert{1, 1}

        box.begin()
        s:delete{1}
        s:truncate()
        box.commit()

        t.assert_equals(s:select(), {})
        t.assert_equals(s.index.sk:select(), {})
        s:insert{2, 2}
        t.assert_equals(s.index.sk:select(), {{2, 2}})
        s:drop()
    end)
end
