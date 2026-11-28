local server = require('luatest.server')
local t = require('luatest')

local g = t.group('vector_replication')

g.before_all(function(cg)
    cg.master = server:new({alias = 'vector_master'})
    cg.replica = server:new({
        alias = 'vector_replica',
        box_cfg = {
            read_only = true,
            replication = cg.master.net_box_uri,
        },
    })
    cg.master:start()
end)

g.after_all(function(cg)
    cg.replica:drop()
    cg.master:drop()
end)

g.test_snapshot_join_and_async_apply = function(cg)
    cg.master:exec(function()
        local embedding = require('embedding')
        local s = box.schema.space.create('vector_repl')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2,
                               unique = false, parts = {{2, 'embedding'}}})
        s:insert{1, embedding.new({1, 0})}
        s:insert{2, embedding.new({0, 1})}
        box.snapshot()
    end)
    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        local s = box.space.vector_repl
        t.assert_equals(s.index.vec:len(), 2)
        local rows, _, distances = s.index.vec:search({1, 0}, {
            limit = 2, with_distance = true,
        })
        t.assert_equals({rows[1][1], distances[1]}, {1, 0})
    end)
    cg.master:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_repl
        s:replace{2, embedding.new({0.9, 0.1})}
        s:delete{1}
        s:insert{3, embedding.new({1, 0})}
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        local s = box.space.vector_repl
        local rows, _, distances = s.index.vec:search({1, 0}, {
            limit = 3, with_distance = true,
        })
        t.assert_equals(#rows, 2)
        t.assert_equals({rows[1][1], distances[1]}, {3, 0})
        t.assert_equals(rows[2][1], 2)
    end)
end
