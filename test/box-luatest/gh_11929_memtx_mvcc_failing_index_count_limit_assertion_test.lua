local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-11929-memtx-mvcc-failing-index-count-limit-assertion')
--
-- gh-11929: memtx mvcc failing index count limit assertion
--

g.before_all(function()
    g.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    g.server:start()

    g.server:exec(function()
        box.schema.space.create("test")
    end)
end)

g.after_each(function()
    g.server:exec(function() box.space.test:truncate() end)
end)

g.after_all(function()
    g.server:drop()
end)

g.test_index_id = function()
    g.server:exec(function()
        for i = 1, box.schema.INDEX_MAX do
            box.space.test:create_index(
                ('idx_%d'):format(i),
                {parts={{i, is_nullable=(i > 1)}}, unique=true}
            )
        end

        box.space.test:replace{1}
        t.assert_equals(box.space.test:get(1), {1})
    end)
end
