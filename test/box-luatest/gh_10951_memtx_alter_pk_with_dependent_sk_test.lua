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
        local s = box.schema.create_space('test')
        s:create_index('primary')
        box.begin()
        for i = 1, 10000 do
            s:replace{i, i}
        end
        box.commit()
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

-- Unique non-nullable index doesn't depend on PK, so alter should work fine.
g.test_unique_index = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        s:create_index('sk', {unique = true})
        s.index.primary:alter({parts = {2, 'unsigned'}})
    end)
end

g.test_non_unique_index = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        s:create_index('sk', {unique = false})
        t.assert_error_covers({
            type = "ClientError",
            name = "UNSUPPORTED",
            message = "Tarantool does not support alter of primary index " ..
                      "along with non-unique or nullable secondary indexes",
        }, s.index.primary.alter, s.index.primary, {parts = {2, 'unsigned'}})
    end)
end

g.test_nullable_index = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        s:create_index('sk', {parts = {2, 'unsigned', is_nullable = true}})
        t.assert_error_covers({
            type = "ClientError",
            name = "UNSUPPORTED",
            message = "Tarantool does not support alter of primary index " ..
                      "along with non-unique or nullable secondary indexes",
        }, s.index.primary.alter, s.index.primary, {parts = {2, 'unsigned'}})
    end)
end
