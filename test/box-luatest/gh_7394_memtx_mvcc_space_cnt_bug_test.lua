local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new {
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk')
        box.internal.memtx_tx_gc(100)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that memtx transaction manager MVCC invariant violation does not lead
-- to incorrect space count (see also gh-7490 test).
g.test_memtx_mvcc_space_cnt_with_invariant_violation = function(cg)
    cg.server:exec(function()
        for _ = 1, 5 do
            box.begin()
            box.space.s:replace{1}
            box.space.s:replace{2}
            box.commit()
        end

        t.assert_equals(box.space.s:count(), #box.space.s:select{})
    end)
end
