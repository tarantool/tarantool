local server = require('luatest.server')
local t = require('luatest')

local g = t.group('vector_embedding', {{mvcc = false}, {mvcc = true}})

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = cg.params.mvcc}})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('vector_embedding')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'},
                  {name = 'payload', type = 'string'}})
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2,
                              distance = 'l2', unique = false,
                              parts = {{2, 'embedding'}}})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_VECTOR_ALLOC', -1)
        box.rollback()
        box.space.vector_embedding:drop()
    end)
end)

g.test_query_lifetime_rollback_and_recovery = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_embedding
        s:insert{1, embedding.new({1, 0}), string.rep('x', 8192)}
        s:insert{2, embedding.new({0, 1}), 'other'}
        local idx = s.index.vec
        local query = embedding.new({1, 0})
        t.assert_equals(idx:select({query},
            {iterator = 'NEIGHBOR', limit = 1})[1][1], 1)
        local rows, _, distances = idx:search(query,
            {limit = 2, with_distance = true})
        t.assert_equals({rows[1][1], distances[1]}, {1, 0})
        t.assert_equals(idx:stat().memory.vectors, 0)
        t.assert_equals(idx:stat().memory.borrowed_vectors, 16)
        for i = 1, 100 do s:update({1}, {{'=', 3, tostring(i)}}) end
        t.assert_equals(idx:stat().slots.embeddings, 2)
        box.begin()
        s:replace{1, embedding.new({5, 5}), 'rollback'}
        t.assert_equals(idx:search(query, {limit = 1})[1][1], 2)
        box.rollback()
        t.assert_equals(idx:search(query, {limit = 1})[1][1], 1)
        for nth = 0, 40 do
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', nth)
            local ok = pcall(s.insert, s,
                             {3, embedding.new({2, 2}), 'injected'})
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', -1)
            if ok then s:delete{3} end
            t.assert_equals(s:get{3}, nil)
            t.assert_equals(idx:search(query, {limit = 1})[1][1], 1)
        end
        idx:rebuild()
        t.assert_equals(idx:search(query, {limit = 1})[1][1], 1)
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_embedding
        t.assert(embedding.is(s:get{1}[2]))
        t.assert_equals(embedding.totable(s:get{1}[2]), {1, 0})
        t.assert_equals(s.index.vec:search(embedding.new({1, 0}),
            {limit = 1})[1][1], 1)
    end)
end

g.test_reuse_under_constant_changes = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_embedding
        for i = 1, 8 do s:insert{i, embedding.new({i, 0}), 'original'} end
        for round = 1, 20 do
            for i = 1, 8 do
                s:replace{i, embedding.new({i, round}), 'changed'}
            end
            fiber.sleep(0)
        end
        t.helpers.retrying({}, function()
            s:update({1}, {{'=', 3, 'gc'}})
            t.assert_equals(s.index.vec:stat().slots.embeddings, 8)
        end)
        local idx = s.index.vec
        local nodes = idx:stat().slots.nodes
        t.assert(idx:stat().slots.reusable > 0)
        for i = 1, 8 do
            s:replace{i, embedding.new({i, 99}), 'reused'}
        end
        t.assert_equals(idx:stat().slots.nodes, nodes)
        t.assert_equals(idx:search(embedding.new({1, 99}),
            {limit = 1})[1][1], 1)
    end)
end
