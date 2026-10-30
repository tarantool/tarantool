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

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        box.schema.func.drop('test', {if_exists = true})
    end)
end)

g.test_func_index_without_sandbox = function(cg)
    cg.server:exec(function()
        local msgpack = require('msgpack')
        local varbinary = require('varbinary')
        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                local msgpack = require('msgpack')
                local varbinary = require('varbinary')
                return {varbinary.new(msgpack.encode(tuple.value))}
            end]]
        })
        local s = box.schema.space.create('test', {
            format = {
                {'key', 'unsigned'},
                {'value', 'any'},
            },
        })
        s:create_index('primary')
        s:create_index('value', {
            func = 'test',
            parts = {{1, 'varbinary'}},
        })
        s:insert({1, {foo = 'bar'}})
        s:insert({2, {fuzz = 'buzz'}})
        local function get(value)
            local v = varbinary.new(msgpack.encode(value))
            return box.space.test.index.value:get(v)
        end
        t.assert_equals(get({foo = 'bar'}), {1, {foo = 'bar'}})
        t.assert_equals(get({fuzz = 'buzz'}), {2, {fuzz = 'buzz'}})
        t.assert_equals(get({1, 2, 3}), nil)
    end)
end
