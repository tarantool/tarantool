local server = require('luatest.server')
local buffer = require('buffer')
local net = require('net.box')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{alias = 'default'}
    g.server:start()
    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:format({
                  {name = 'id',   type = 'unsigned'},
                  {name = 'data', type = 'string'},
                 })
        s:create_index('primary', {parts = {'id'}})
        box.schema.user.grant('guest', 'read,write', 'space', 'test')
    end)

    g.conn = net.connect(g.server.net_box_uri)
end)

g.after_all(function()
    g.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)

    g.conn:close()
    g.server:drop()
end)

g.test_netbox_connect_opts = function()
    t.assert_error_msg_contains("unexpected option 'some_opt'", function()
        net.connect(g.server.net_box_uri, {some_opt = 1})
    end)
    t.assert_error_msg_contains("parameter 'user' should be of type string", function()
        net.connect(g.server.net_box_uri, {user = true})
    end)
    t.assert_error_msg_contains("parameter 'password' should be of type string", function()
        net.connect(g.server.net_box_uri, {password = true})
    end)
    t.assert_error_msg_contains("parameter 'wait_connected' should be one of types: number, boolean", function()
        net.connect(g.server.net_box_uri, {wait_connected = 'string'})
    end)
    t.assert_error_msg_contains("parameter 'reconnect_after' should be of type number", function()
        net.connect(g.server.net_box_uri, {reconnect_after = true})
    end)
    t.assert_error_msg_contains("parameter 'call_16' should be of type boolean", function()
        net.connect(g.server.net_box_uri, {call_16 = 1})
    end)
    t.assert_error_msg_contains("parameter 'console' should be of type boolean", function()
        net.connect(g.server.net_box_uri, {console = 1})
    end)
    t.assert_error_msg_contains("parameter 'connect_timeout' should be of type number", function()
        net.connect(g.server.net_box_uri, {connect_timeout = 'string'})
    end)
    t.assert_error_msg_contains("parameter 'required_protocol_version' should be of type number", function()
        net.connect(g.server.net_box_uri, {required_protocol_version = 'string'})
    end)
    t.assert_error_msg_contains("parameter 'required_protocol_features' should be of type table", function()
        net.connect(g.server.net_box_uri, {required_protocol_features = 1})
    end)
end

g.test_netbox_select_opts = function()
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test:select({1}, 123)
    end)
    t.assert_error_msg_contains("parameter 'timeout' should be of type number", function()
        g.conn.space.test:select({1}, {timeout = true})
    end)
    t.assert_error_msg_contains("parameter 'buffer' should be of type struct ibuf", function()
        g.conn.space.test:select({1}, {buffer = true})
    end)
    t.assert_error_msg_contains("parameter 'is_async' should be of type boolean", function()
        g.conn.space.test:select({1}, {is_async = 1})
    end)
    t.assert_error_msg_contains("parameter 'on_push' should be of type function", function()
        g.conn.space.test:select({1}, {on_push = 1})
    end)
    t.assert_error_msg_contains("parameter 'return_raw' should be of type boolean", function()
        g.conn.space.test:select({1}, {return_raw = 1})
    end)
    t.assert_error_msg_contains("in an async request use future:pairs()", function()
        g.conn.space.test:select({1}, {is_async = true, on_push_ctx = true })
    end)
end

g.test_netbox_all_methods_opts = function()
    t.assert_error_msg_contains("options should be a table", function()
        g.conn:ping(123)
    end)

    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test:get({1}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test:insert({1, 'A'}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test:replace({1, 'A'}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test:update(1, {'!', 1, 'A'}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test:upsert(1, {'!', 1, 'A'}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test:delete({1}, 123)
    end)

    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test.index.primary:select({1}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test.index.primary:get({1}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test.index.primary:min({1}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test.index.primary:max({1}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test.index.primary:count({1}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test.index.primary:delete({1}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn.space.test.index.primary:update(1, {'!', 1, 'A'}, 123)
    end)

    t.assert_error_msg_contains("options should be a table", function()
        g.conn:eval('return {nil,5}', {}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn:call('f2',{1,'B'}, 123)
    end)

    local stream = g.conn:new_stream()
    t.assert_error_msg_contains("options should be a table", function()
        stream:begin({}, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        stream:commit(123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        stream:rollback(123)
    end)

    t.assert_error_msg_contains("options should be a table", function()
        g.conn:execute('', {}, nil, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn:prepare('', {}, nil, 123)
    end)
    t.assert_error_msg_contains("options should be a table", function()
        g.conn:unprepare(1, {}, nil, 123)
    end)
end

g.test_netbox_not_supported_opts = function()
    t.assert_error_msg_contains("doesn't support", function()
        g.conn:ping({is_async = true})
    end)

    local ibuf = buffer.ibuf()
    t.assert_error_msg_contains("doesn't support", function()
        g.conn.space.test.index.primary:get({1}, {buffer = ibuf})
    end)
    t.assert_error_msg_contains("doesn't support", function()
        g.conn.space.test.index.primary:min({1}, {buffer = ibuf})
    end)
    t.assert_error_msg_contains("doesn't support", function()
        g.conn.space.test.index.primary:max({1}, {buffer = ibuf})
    end)
    t.assert_error_msg_contains("doesn't support", function()
        g.conn.space.test.index.primary:count({1}, {buffer = ibuf})
    end)
end
