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
    end)
end)

g.test_arg_check = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        t.assert_error_covers({
            type = 'IllegalParams',
            name = 'ILLEGAL_PARAMS',
            message = "options parameter 'aggregates' should be of type table",
        }, s.create_index, s, 'pk', {aggregates = 1234})

        t.assert_error_covers({
            type = 'ClientError',
            name = 'WRONG_INDEX_OPTIONS',
            message = "Wrong index options: 'aggregates' elements must be map",
        }, s.create_index, s, 'pk', {aggregates = {1234}})

        t.assert_error_covers({
            type = 'ClientError',
            name = 'WRONG_INDEX_OPTIONS',
            message = "Wrong index options: 'aggregates' must be array",
        }, box.space._index.insert, box.space._index, {
            s.id, 0, 'pk', 'tree', {aggregates = 1234}, {{[0] = 'unsigned'}}
        })

        t.assert_error_covers({
            type = 'ClientError',
            name = 'WRONG_INDEX_OPTIONS',
            message = "Wrong index options: 'aggregates' elements must be map",
        }, box.space._index.insert, box.space._index, {
            s.id, 0, 'pk', 'tree', {aggregates = {1234}}, {{[0] = 'unsigned'}}
        })
    end)
end

g.test_fields = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {
            format = {{'a', 'unsigned'}, {'b', 'string'}}
        })
        --
        -- Test space:create_index.
        --
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.aggregates[1]: field (name or number) is " ..
                      "expected",
        }, s.create_index, s, 'pk', {aggregates = {{field = {}}}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.aggregates[1]: field (number) must be one-based",
        }, s.create_index, s, 'pk', {aggregates = {{field = 0}}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.aggregates[1]: field was not found by name 'x'",
        }, s.create_index, s, 'pk', {aggregates = {{field = 'x'}}})
        --
        -- Test index:alter
        --
        local pk = s:create_index('pk')
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.aggregates[1]: field (name or number) is " ..
                      "expected",
        }, pk.alter, pk, {aggregates = {{field = {}}}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.aggregates[1]: field (number) must be one-based",
        }, pk.alter, pk, {aggregates = {{field = 0}}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.aggregates[1]: field was not found by name 'x'",
        }, pk.alter, pk, {aggregates = {{field = 'x'}}})
    end)
end

g.test_unsupported = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {engine = 'memtx'})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'UNSUPPORTED',
            message = "memtx does not support 'aggregates' option",
        }, s.create_index, s, 'pk', {aggregates = {{field = 1}}})
        s:drop()

        s = box.schema.space.create('test', {engine = 'vinyl'})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'UNSUPPORTED',
            message = "vinyl does not support 'aggregates' option",
        }, s.create_index, s, 'pk', {aggregates = {{field = 1}}})
    end)
end
