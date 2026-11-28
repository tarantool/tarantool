local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('vector_review')
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
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', false)
        box.rollback()
        for _, rv in ipairs(box.read_view.list()) do
            box.read_view.open({id = rv.id}):close()
        end
        if box.space.vector_review then box.space.vector_review:drop() end
    end)
end)

g.test_return_positions = function(cg)
    cg.server:exec(function()
        local idx = box.space.vector_review.index.vec
        local rows, _, distances = idx:search({0, 0}, {
            limit = 0, with_distance = true,
        })
        t.assert_equals({rows, distances}, {{}, {}})
        t.assert_equals(select('#', idx:search({0, 0}, {
            limit = 0, with_distance = true,
        })), 3)
    end)
end

g.test_search_own_changes = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_review
        for i = 1, 40 do s:insert{i, embedding.new({i, 0}), 'old'} end
        box.begin()
        s:replace{1, embedding.new({100, 0}), 'new'}
        local rows = s.index.vec:search({0, 0}, {limit = 40})
        t.assert_equals(rows[1][1], 2)
        t.assert_equals(rows[#rows][1], 1)
        box.rollback()
        t.assert_equals(s.index.vec:search({0, 0}, {limit = 1})[1][1], 1)
    end)
end

g.test_read_view_survives_dml_rebuild_and_drop = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_review
        for i = 1, 70 do s:insert{i, embedding.new({i, 0}), 'original'} end
        local rv = box.read_view.open()
        s:replace{1, embedding.new({100, 0}), 'changed'}
        s:delete{2}
        s.index.vec:rebuild()
        s:drop()
        rv:with(function()
            local idx = rv.space.vector_review.index.vec
            local rows = idx:select({{0, 0}},
                                    {iterator = 'neighbor', limit = 70})
            t.assert_equals(#rows, 70)
            for i, row in ipairs(rows) do
                t.assert_equals(row, {i, embedding.new({i, 0}), 'original'})
            end
            local count = 0
            for _ in idx:pairs() do count = count + 1 end
            t.assert_equals(count, 70)
        end)
        rv:close()
    end)
end

g.test_read_view_with_hash_primary = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_review
        s.index.vec:drop()
        s.index.pk:drop()
        s:create_index('pk', {type = 'hash'})
        s:create_index('vec', {type = 'vector', dimension = 2,
                              distance = 'l2', unique = false,
                              parts = {{2, 'embedding'}}})
        s:insert{1, embedding.new({1, 0}), 'committed'}
        box.begin()
        s:replace{1, embedding.new({2, 0}), 'pending'}
        local rv = box.read_view.open()
        box.rollback()
        rv:with(function()
            local rows = rv.space.vector_review.index.vec:select({{0, 0}},
                {iterator = 'neighbor', limit = 1})
            t.assert_equals(rows, {{1, embedding.new({1, 0}), 'committed'}})
        end)
        rv:close()
    end)
end

g.test_cow_generations_and_limits = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local debug_build = t.tarantool.is_debug_build()
        local s = box.space.vector_review
        local idx = s.index.vec
        for i = 1, 100 do s:insert{i, embedding.new({i, 0}), 'v1'} end
        local first = box.read_view.open()
        s:update({1}, {{'=', 3, 'v2'}})
        s:replace{2, embedding.new({200, 0}), 'v2'}
        local second = box.read_view.open()
        s:delete{3}
        idx:rebuild()
        s:replace{4, embedding.new({400, 0}), 'v3'}
        idx:rebuild()
        local stat = idx:stat().memory
        t.assert_gt(stat.read_views, 0)
        if debug_build then
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', 0)
            local opened = pcall(box.read_view.open)
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', -1)
            t.assert_not(opened)
            t.assert_equals(idx:stat().memory.read_views, stat.read_views)
        end
        t.assert_equals(stat.total, stat.generations.current +
            stat.transient + stat.read_views)
        t.assert_equals(idx:bsize(), stat.total)
        first:with(function()
            local rv_idx = first.space.vector_review.index.vec
            local rows = rv_idx:select({{0, 0}},
                {iterator = 'neighbor', limit = 4})
            t.assert_equals({rows[1][1], rows[2][1], rows[3][1], rows[4][1]},
                            {1, 2, 3, 4})
            t.assert_equals(rows[1][3], 'v1')
            t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                rv_idx.select, rv_idx, {{0, 0}}, {iterator = 'neighbor'})
            for _, opts in ipairs({
                {iterator = 'neighbor', limit = 1025},
                {iterator = 'neighbor', limit = 1, offset = 1},
                {iterator = 'neighbor', limit = 1, fetch_pos = true},
            }) do
                t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                    rv_idx.select, rv_idx, {{0, 0}}, opts)
            end
            t.assert_equals(rv_idx:select({{0, 0}},
                {iterator = 'neighbor', limit = 0}), {})
            if debug_build then
                box.error.injection.set('ERRINJ_VECTOR_WORK_LIMIT', 1)
                local ok, err = pcall(rv_idx.select, rv_idx, {{0, 0}},
                    {iterator = 'neighbor', limit = 1})
                box.error.injection.set('ERRINJ_VECTOR_WORK_LIMIT', -1)
                t.assert_not(ok)
                t.assert_equals(err.code, box.error.VECTOR_WORK_LIMIT)
            end
        end)
        first:close()
        second:with(function()
            local rows = second.space.vector_review.index.vec:select({{0, 0}},
                {iterator = 'neighbor', limit = 4})
            t.assert_equals({rows[1][1], rows[2][1], rows[3][1], rows[4][1]},
                            {1, 3, 4, 5})
            t.assert_equals(rows[1][3], 'v2')
        end)
        second:close()
        t.assert_equals(idx:stat().memory.read_views, 0)
        idx:rebuild()
        t.assert_equals(idx:search({0, 0}, {limit = 1})[1][3], 'v2')
    end)
end

g.test_reuse_with_delayed_maintenance = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.vector_review
        s:insert{1, embedding.new({1, 0}), 'row'}
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', true)
        for i = 2, 300 do
            s:replace{1, embedding.new({i, 0}), 'row'}
            if i % 8 == 0 then fiber.sleep(0) end
        end
        local stat = s.index.vec:stat()
        t.assert_le(stat.slots.nodes, stat.slots.node_limit)
        t.assert_lt(stat.slots.nodes, 64)
        box.error.injection.set('ERRINJ_VECTOR_REBUILD_DELAY', false)
        s:replace{1, embedding.new({999, 0}), 'resumed'}
        t.assert_equals(s.index.vec:search({999, 0}, {limit = 1})[1][3],
                        'resumed')
    end)
end

g.test_embedding_coordinates = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_review
        s:drop()
        s = box.schema.space.create('vector_review')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'},
                  {name = 'payload', type = 'string'}})
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2,
                              distance = 'cosine', unique = false,
                              parts = {{2, 'embedding'}}})
        local packed = embedding.new({1, 0})
        t.assert_equals(embedding.totable(packed), {1, 0})
        s:insert{1, packed, 'first'}
        s:insert{2, embedding.new({0, 1}), 'other'}
        local rows, _, distances = s.index.vec:search({1, 0}, {
            limit = 2, with_distance = true,
        })
        t.assert_equals({rows[1][1], distances[1]}, {1, 0})
        t.assert_equals(distances[2], 1)
        t.assert_equals(s.index.vec:stat().memory.vectors, 0)
        t.assert_equals(s.index.vec:stat().memory.borrowed_vectors, 16)
        for i = 1, 100 do s:update({1}, {{'=', 3, tostring(i)}}) end
        t.assert_equals(s.index.vec:stat().slots.embeddings, 2)
        local rv = box.read_view.open()
        s:delete{1}
        s.index.vec:rebuild()
        rv:with(function()
            local got = rv.space.vector_review.index.vec:select({{1, 0}},
                {iterator = 'neighbor', limit = 2})
            t.assert_equals(got[1][1], 1)
            t.assert_equals(got[1][2], packed)
        end)
        rv:close()
        t.assert_equals(s.index.vec:search({1, 0}, {limit = 1})[1][1], 2)
        t.assert_error(function() embedding.new({0/0, 1}) end)
        t.assert_error(function() embedding.new({x = 1}) end)
        t.assert_error(function() embedding.new({1, extra = 2}) end)
        local valid = embedding.new({1, 1})
        for nth = 0, 30 do
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', nth)
            local ok = pcall(s.insert, s, {3, valid, 'allocation failure'})
            box.error.injection.set('ERRINJ_VECTOR_ALLOC', -1)
            if ok then s:delete{3} end
            t.assert_equals(s:get{3}, nil)
            t.assert_equals(s:get{2}[3], 'other')
        end
        t.helpers.retrying({}, function()
            s:update({2}, {{'=', 3, 'other'}})
            t.assert_equals(s.index.vec:stat().slots.embeddings, 1)
        end)
        s.index.vec:rebuild()
        t.assert_equals(s.index.vec:stat().memory.vectors, 0)
        t.assert_equals(s.index.vec:stat().memory.borrowed_vectors, 8)
    end)
end
