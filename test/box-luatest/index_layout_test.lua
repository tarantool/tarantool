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
            message = "options parameter 'layout' should be of type string",
        }, s.create_index, s, 'pk', {layout = 1234})

        t.assert_error_covers({
            type = 'ClientError',
            name = 'WRONG_INDEX_OPTIONS',
            message = "Wrong index options: 'layout' must be string",
        }, box.space._index.insert, box.space._index, {
            s.id, 0, 'pk', 'tree', {layout = 1234}, {{[0] = 'unsigned'}}
        })
    end)
end

g.test_unsupported = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {engine = 'memtx'})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'UNSUPPORTED',
            message = "memtx does not support 'layout' option",
        }, s.create_index, s, 'pk', {layout = 'test'})
        s:drop()

        s = box.schema.space.create('test', {engine = 'vinyl'})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'UNSUPPORTED',
            message = "vinyl does not support 'layout' option",
        }, s.create_index, s, 'pk', {layout = 'test'})
    end)
end
