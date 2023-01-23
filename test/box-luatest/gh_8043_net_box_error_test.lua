local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new{alias = 'default'}
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('primary')
        s:insert({1, 'with %d %f %n symbols'})
        rawset(_G, 'test1', function()
            error('error at %s:%d')
        end)
        rawset(_G, 'test2', function()
            box.error({code = box.error.UNKNOWN, reason = '%x%y%z'})
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_net_box_error = function(cg)
    local c = net.connect(cg.server.net_box_uri)
    t.assert_error_msg_content_equals(
        'Duplicate key exists in unique index "primary" in space "test" ' ..
        'with old tuple - [1, "with %d %f %n symbols"] ' ..
        'and new tuple - [1, "qwe"]',
        c.space.test.insert, c.space.test, {1, 'qwe'})
    t.assert_error_msg_content_equals('error at %s:%d',
                                      c.call, c, 'test1', {})
    t.assert_error_msg_content_equals('%x%y%z',
                                      c.call, c, 'test2', {})
    c:close()
end
