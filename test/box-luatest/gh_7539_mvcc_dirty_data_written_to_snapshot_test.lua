local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, {{index_type = 'tree'}, {index_type = 'hash'}})

g.before_all(function(cg)
    cg.server = server:new({
        alias = 'master',
        box_cfg = {memtx_use_mvcc_engine = true}
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function(index_type)
        box.schema.space.create('test')
        box.space.test:create_index('primary', {type = index_type})
    end, {cg.params.index_type})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

g.test_mvcc_dirty_data_written_to_snapshot = function(cg)
    cg.server:exec(function()
        box.space.test:replace({1, 1})
        box.begin()
        box.space.test:replace({1, 2})
        box.space.test:replace({2, 3})
        box.snapshot()
        box.rollback()
    end)
    cg.server:restart()
    cg.server:exec(function()
        t.assert_equals(box.space.test:select(), {{1, 1}})
    end)
end
