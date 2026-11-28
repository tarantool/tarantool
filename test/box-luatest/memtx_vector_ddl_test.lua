local server = require('luatest.server')
local t = require('luatest')

local g = t.group('memtx_vector_ddl')

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('vector_ddl')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.vector_ddl:drop()
    end)
end)

g.test_distance_and_catalog_options = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_ddl
        local options = {type = 'vector', dimension = 2,
                         distance = 'l2', algorithm = 'hnsw',
                         opts = {m = 8, ef_construction = 32,
                                 ef_search = 16},
                         unique = false, parts = {{2, 'embedding'}}}
        s:create_index('vec', options)
        local stored = box.space._index:get{s.id, 1}[5]
        t.assert_equals(stored.distance, 'l2')
        t.assert_equals(stored.algorithm, 'hnsw')
        t.assert_equals(stored.opts, options.opts)
        s:insert{1, embedding.new({10, 0})}
        s:insert{2, embedding.new({1, 1})}
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1],
                        2)
        s.index.vec:alter({distance = 'cosine'})
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1],
                        1)
    end)
end

g.test_invalid_algorithm_options = function(cg)
    cg.server:exec(function()
        local s = box.space.vector_ddl
        local function invalid(extra)
            local options = {type = 'vector', dimension = 2, unique = false,
                             parts = {{2, 'embedding'}}}
            for k, v in pairs(extra) do
                options[k] = v
            end
            t.assert_equals(pcall(function()
                s:create_index('vec', options)
            end), false)
        end
        invalid({distance = 'euclid'})
        invalid({distance = 'unknown'})
        invalid({algorithm = 'other'})
        invalid({opts = {m = 3}})
        invalid({opts = {m = 65}})
        invalid({opts = {m = 16, ef_construction = 15}})
        invalid({opts = {ef_search = 0}})
        invalid({opts = {ef_search = 8193}})
        invalid({opts = {unknown = 1}})
    end)
end

g.test_rtree_distance_still_works = function(cg)
    cg.server:exec(function()
        local s = box.space.vector_ddl
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'array'}})
        s:create_index('spatial', {type = 'rtree', dimension = 2,
                                   distance = 'manhattan', unique = false,
                                   parts = {{2, 'array'}}})
        s:insert{1, {0, 0}}
        t.assert_equals(s.index.spatial:select({0, 0},
                                               {iterator = 'NEIGHBOR'})[1][1],
                        1)
    end)
end

g.test_vector_options_require_vector_index = function(cg)
    cg.server:exec(function()
        for _, engine in ipairs({'memtx', 'vinyl'}) do
            local s = box.schema.space.create('vector_options',
                                             {engine = engine})
            for _, opts in ipairs({
                {distance = 'l2'},
                {algorithm = 'hnsw'},
                {opts = {ef_search = 10}},
            }) do
                t.assert_error_covers({code = box.error.MODIFY_INDEX},
                                      s.create_index, s, 'pk', opts)
            end
            s:drop()
        end
    end)
end

g.test_catalog_recovery = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_ddl
        s:create_index('vec', {type = 'vector', dimension = 2,
                               distance = 'ip', algorithm = 'hnsw',
                               opts = {m = 12, ef_construction = 48,
                                       ef_search = 24},
                               unique = false, parts = {{2, 'embedding'}}})
        s:insert{1, embedding.new({1, 0})}
        s:insert{2, embedding.new({10, 0})}
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.vector_ddl
        local opts = box.space._index:get{s.id, 1}[5]
        t.assert_equals(opts.distance, 'ip')
        t.assert_equals(opts.opts.m, 12)
        t.assert_equals(opts.opts.ef_construction, 48)
        t.assert_equals(opts.opts.ef_search, 24)
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1],
                        2)
    end)
end

g.test_path_and_nullable = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.vector_ddl
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'map'}})
        local parts = {{field = 2, type = 'embedding', path = 'embedding'}}
        s:create_index('vec', {type = 'vector', dimension = 2,
                               unique = false, parts = parts})
        s:insert{1, {embedding = embedding.new({1, 0})}}
        t.assert_equals(s.index.vec:select({{1, 0}},
                                           {iterator = 'neighbor',
                                            limit = 1})[1][1],
                        1)
        s.index.vec:drop()
        parts[1].is_nullable = true
        t.assert_equals(pcall(function()
            s:create_index('vec', {type = 'vector', dimension = 2,
                                   unique = false, parts = parts})
        end), false)
    end)
end

g.test_non_embedding_parts_rejected = function(cg)
    cg.server:exec(function()
        local s = box.space.vector_ddl
        for _, kind in ipairs({'array', 'varbinary'}) do
            s:format({{name = 'id', type = 'unsigned'},
                      {name = 'vec', type = kind}})
            t.assert_error(function()
                s:create_index('vec', {type = 'vector', dimension = 2,
                                      unique = false, parts = {{2, kind}}})
            end)
            t.assert_equals(s.index.vec, nil)
        end
    end)
end
