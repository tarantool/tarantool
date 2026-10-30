local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.before_test('test_func_index_iterator_stable', function()
    g.server:exec(function()
        box.schema.func.create('test', {
            body = [[function(t)
                local ret = {}
                for _, v in ipairs(string.split(t[2])) do
                    table.insert(ret, {v})
                end
                return ret
            end]],
            is_deterministic = true,
            is_sandboxed = true,
            is_multikey = true,
        })
        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:create_index('f', {
            func = 'test',
            parts = {{1, 'string'}},
        })
    end)
end)

g.after_test('test_func_index_iterator_stable', function()
    g.server:exec(function()
        box.space.test:drop()
        box.schema.func.drop('test')
    end)
end)

g.test_func_index_iterator_stable = function()
    g.server:exec(function()
        local s = box.space.test
        s:insert{1, 'abc'}
        s:insert{2, 'foo bar'}
        s:insert{3, string.format('%s %s %s',
                                  string.rep('x', 1000),
                                  string.rep('y', 3000),
                                  string.rep('z', 5000))}
        for _, t in s.index.f:pairs() do
            s:delete({t[1]})
        end
        t.assert_equals(s.index.f:select(), {})
    end)
end

g.before_test('test_func_index_allocator', function()
    g.server:exec(function()
        box.schema.func.create('test', {
            body = [[function(t)
                return {{string.sub(t[1], 1, t[2])}}
            end]],
            is_deterministic = true,
            is_sandboxed = true,
            opts = { is_multikey = true },
        })
        local s = box.schema.create_space('test')
        s:create_index('pk', {
            parts = {{1, 'string'}},
        })
        s:create_index('f', {
            func = 'test',
            parts = {{1, 'string'}},
        })
    end)
end)

g.after_test('test_func_index_allocator', function()
    g.server:exec(function()
        box.space.test:drop()
        box.schema.func.drop('test')
    end)
end)

-- Test different sizes of functional index.
g.test_func_index_allocator = function()
    g.server:exec(function()
        local s = box.space.test
        local MIN_STRING_LEN = 8
        local MAX_STRING_LEN = 256
        local STRING_LEN_STEP = 4
        local MAX_ALLOCS = 128
        local str = string.rep('a', MAX_STRING_LEN)

        for i = MIN_STRING_LEN, MAX_STRING_LEN, STRING_LEN_STEP do
            for j = 1, MAX_ALLOCS do
                s:insert{j .. str,  i}
            end
            t.assert_equals(s.index.f:count(), MAX_ALLOCS)
            for j = 1, MAX_ALLOCS do
                s:delete{j .. str}
            end
        end
    end)
end
