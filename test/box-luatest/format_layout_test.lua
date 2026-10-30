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

g.test_format_layout_validation = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_SPACE_FORMAT,
            message = "Wrong space format field 1: 'layout' must be string"
        }, s.format, s, {{name = 'a', type = 'unsigned', layout = 1}})
    end)
end

g.test_format_stubs = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test1', {engine = 'memtx'})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.UNSUPPORTED,
            message = "memtx does not support 'layout' option",
        }, s.format, s, {{name = 'a', type = 'unsigned', layout = 'null_rle'}})
        local s = box.schema.create_space('test2', {engine = 'vinyl'})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.UNSUPPORTED,
            message = "vinyl does not support 'layout' option",
        }, s.format, s, {{name = 'a', type = 'unsigned', layout = 'null_rle'}})
    end)
end
