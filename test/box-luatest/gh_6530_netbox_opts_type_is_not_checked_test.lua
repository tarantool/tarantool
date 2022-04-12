local server = require('test.luatest_helpers.server')
local t = require('luatest')

local g = t.group()

g.before_all = function()
    g.server = server:new{alias   = 'default'}
    g.server:start()
end

g.after_all = function()
    g.server:drop()
end

g.before_test('test_netbox_opts_type_is_not_checked', function()
    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:format({
                  {name = 'id',   type = 'unsigned'},
                  {name = 'data', type = 'string'},
                 })
        s:create_index('primary', {parts = {'id'}})
        box.schema.user.grant('guest', 'read,write', 'space', 'test')
    end)
end)

g.test_netbox_opts_type_is_not_checked = function()
    local t = require('luatest')
    local netbox = require('net.box')
    local conn = netbox.connect(g.server.net_box_uri)

    t.assert_equals(conn.space.test:select(), {})
    t.assert_error_msg_contains("should be of C type struct ibuf", function()
        conn:call('echo', {1}, {buffer = 'abcd'})
    end)
    t.assert_error_msg_contains("parameter 'timeout' should be of type number", function()
        conn:ping({timeout = 'abcd'})
    end)
    t.assert_error_msg_contains("unexpected option 'some_opt'", function()
        conn:ping({some_opt = true})
    end)
    t.assert_error_msg_contains("parameter 'is_async' should be of type boolean", function()
        conn:ping({is_async = 'abc'})
    end)
    t.assert_error_msg_contains("parameter 'return_raw' should be of type boolean", function()
        conn:ping({return_raw = 5})
    end)
    t.assert_error_msg_contains("parameter 'limit' should be of type number", function()
        conn.space.test:select({}, {limit = 'abc'})
    end)
    t.assert_error_msg_contains("parameter 'offset' should be of type number", function()
        conn.space.test:select({}, {offset = 'abc'})
    end)
    t.assert_error_msg_contains("Unknown iterator type 'abc'", function()
        conn.space.test:select({}, {iterator = 'abc'})
    end)
    t.assert_error_msg_contains("parameter 'timeout' should be of type number", function()
        conn.space.test:replace({5, 'data'}, {timeout = 'ldk'})
    end)
    conn.space.test:insert({1, 'some info'})
    t.assert_error_msg_contains("parameter 'timeout' should be of type number", function()
        conn.space.test:insert({6, 'data'}, {timeout = 'abc'})
    end)
    t.assert_error_msg_contains("'buffer' should be of C type struct ibuf", function()
        conn.space.test:delete({1}, {buffer = 5})
    end)
    t.assert_error_msg_contains("parameter 'timeout' should be of type number", function()
        conn.space.test:get({6}, {timeout = 'abc'})
    end)
end

g.after_test('test_netbox_opts_type_is_not_checked', function()
    g.server:exec(function()
        local s = box.space.test
        if s then
            s:drop()
        end
    end)
end)
