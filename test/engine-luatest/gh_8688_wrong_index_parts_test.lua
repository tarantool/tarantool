local t = require('luatest')
local g = t.group('gh-8688', {{engine = 'memtx'}, {engine = 'vinyl'}})
local server = require('luatest.server')

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that it is not possible to insert an index definition with bad list
-- of the parts directly into `box.space._index`.
g.test_wrong_index_parts = function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})

        local parts = {}
        t.assert_error_msg_equals(
            "Can't create or modify index 'pk' in space 'test': " ..
            "part count must be positive",
            function()
                box.space._index:insert{s.id, 0, 'pk', 'tree',
                                        {unique = true}, parts}
            end
        )

        for k = 1, box.schema.INDEX_PART_MAX + 1, 1 do
            table.insert(parts, k)
            table.insert(parts, 'unsigned')
        end
        t.assert_error_msg_equals(
            "Can't create or modify index 'pk' in space 'test': " ..
            "too many key parts",
            function()
                box.space._index:insert{s.id, 0, 'pk', 'tree',
                                        {unique = true}, parts}
            end
        )
    end, {cg.params.engine})
end
