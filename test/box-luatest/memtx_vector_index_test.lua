local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('t')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        s:create_index('v', {type = 'vector', dimension = 3,
                             unique = false, parts = {{2, 'embedding'}}})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.t ~= nil then
            box.space.t:drop()
        end
    end)
end)

g.test_search_returns_nearest_first = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local function neighbours(key, opts)
            local ids = {}
            for _, tuple in ipairs(box.space.t.index.v:select(key, opts)) do
                table.insert(ids, tuple[1])
            end
            return ids
        end
        local s = box.space.t
        s:insert{1, embedding.new({1, 0, 0})}
        s:insert{2, embedding.new({0.9, 0.1, 0})}
        s:insert{3, embedding.new({0, 1, 0})}
        s:insert{4, embedding.new({0, 0, 1})}
        local ids = neighbours({{1, 0, 0}}, {iterator = 'neighbor', limit = 4})
        t.assert_equals({ids[1], ids[2], #ids}, {1, 2, 4})
        t.assert_equals(#neighbours({{1, 0, 0}}, {
            iterator = 'neighbor', limit = 2,
        }), 2)
        t.assert_equals({s.index.v:len(), s.index.v:bsize() > 0}, {4, true})
    end)
end

g.test_delete_and_update_are_visible = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local function neighbours(key, opts)
            local ids = {}
            for _, tuple in ipairs(box.space.t.index.v:select(key, opts)) do
                table.insert(ids, tuple[1])
            end
            return ids
        end
        local s = box.space.t
        s:insert{1, embedding.new({1, 0, 0})}
        s:insert{2, embedding.new({0.9, 0.1, 0})}
        s:insert{3, embedding.new({0, 1, 0})}

        s:delete{2}
        t.assert_equals({neighbours({{1, 0, 0}},
                                    {iterator = 'neighbor', limit = 2}),
                         s.index.v:len()}, {{1, 3}, 2})

        s:replace{3, embedding.new({0.99, 0.01, 0})}
        t.assert_equals(neighbours({{1, 0, 0}},
                                   {iterator = 'neighbor', limit = 2}), {1, 3})
    end)
end

g.test_invalid_replace_preserves_old_vector = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.t
        s:insert{1, embedding.new({1, 0, 0})}
        local ok = pcall(function() s:replace{1, embedding.new({1, 0})} end)
        t.assert_equals(ok, false)
        t.assert_equals(s:get{1}:totable(),
                        {1, embedding.new({1, 0, 0})})
        local found = s.index.v:select({{1, 0, 0}},
                                       {iterator = 'neighbor', limit = 1})
        t.assert_equals({s.index.v:len(), #found, found[1][1]}, {1, 1, 1})
    end)
end

g.test_search_observes_fiber_slice = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local fiber = require('fiber')
        local s = box.space.t
        s:insert{1, embedding.new({1, 0, 0})}
        fiber.set_slice(0)
        local ok, err = pcall(function()
            return s.index.v:select({{1, 0, 0}},
                                    {iterator = 'neighbor', limit = 1})
        end)
        fiber.set_slice(1)
        t.assert_equals(ok, false)
        t.assert_str_contains(tostring(err), 'fiber slice is exceeded')
        local found = s.index.v:select({{1, 0, 0}},
                                       {iterator = 'neighbor', limit = 1})
        t.assert_equals({#found, found[1][1]}, {1, 1})
    end)
end

g.test_unsupported_iterators_are_reported = function(cg)
    cg.server:exec(function()
        local v = box.space.t.index.v
        t.assert_error_covers({code = box.error.VECTOR_INVALID},
                              function() v:select() end)
        for _, iterator in ipairs({'GT', 'EQ'}) do
            t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                function()
                    v:select({{1, 0, 0}}, {iterator = iterator, limit = 1})
                end)
        end
    end)
end

g.test_index_grows_past_its_initial_reserve = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local function neighbours(key, opts)
            local ids = {}
            for _, tuple in ipairs(box.space.t.index.v:select(key, opts)) do
                table.insert(ids, tuple[1])
            end
            return ids
        end
        local s = box.space.t
        for i = 1, 500 do
            s:insert{i, embedding.new({i / 500, 1 - i / 500, 0})}
        end
        t.assert_equals({s.index.v:len(), #neighbours({{1, 0, 0}},
                                                      {iterator = 'neighbor',
                                                       limit = 500})},
                        {500, 500})
    end)
end

g.test_index_is_rebuilt_on_recovery = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.space.t
        s:insert{1, embedding.new({1, 0, 0})}
        s:insert{2, embedding.new({0, 1, 0})}
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(function()
        local function neighbours(key, opts)
            local ids = {}
            for _, tuple in ipairs(box.space.t.index.v:select(key, opts)) do
                table.insert(ids, tuple[1])
            end
            return ids
        end
        t.assert_equals({box.space.t.index.v:len(),
                         neighbours({{1, 0, 0}}, {
                             iterator = 'neighbor', limit = 1,
                         })},
                        {2, {1}})
    end)
end

g.test_dimension_is_validated = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('dim')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        local message = 'Vector index dimension must be between 1 and 4096'
        for _, dimension in ipairs({0, 5000}) do
            t.assert_error_msg_contains(message, function()
                s:create_index('v', {type = 'vector', dimension = dimension,
                                     unique = false,
                                     parts = {{2, 'embedding'}}})
            end)
        end
        s:drop()
    end)
end

g.test_vectors_of_an_embedding_size = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.schema.space.create('emb')
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        s:create_index('v', {type = 'vector', dimension = 1536,
                             unique = false, parts = {{2, 'embedding'}}})
        local function vector(shift)
            local vec = {}
            for i = 1, 1536 do
                vec[i] = ((i + shift) % 11) / 11
            end
            return vec
        end
        s:insert{1, embedding.new(vector(0))}
        s:insert{2, embedding.new(vector(5))}
        local found = s.index.v:select({vector(0)}, {
            iterator = 'neighbor', limit = 1,
        })
        t.assert_equals({#found, found[1][1]}, {1, 1})
        s:drop()
    end)
end
