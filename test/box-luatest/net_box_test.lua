local msgpack = require('msgpack')
local net = require('net.box')
local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all = function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end

g.after_all = function()
    g.server:stop()
end

g.before_test('test_msgpack_object_args', function()
    g.server:eval('function echo(...) return ... end')
end)

g.after_test('test_msgpack_object_args', function()
    g.server:eval('echo = nil')
end)

g.test_msgpack_object_args = function()
    local c = net.connect(g.server.net_box_uri)
    local ret

    -- eval
    t.assert_error_msg_content_equals(
        "Tuple/Key must be MsgPack array",
        function() c:eval('return ...', msgpack.object(123)) end)
    ret = {c:eval('return ...', {msgpack.object(123)})}
    t.assert_equals(ret, {123})
    ret = {c:eval('return ...', msgpack.object({}))}
    t.assert_equals(ret, {})
    ret = {c:eval('return ...', msgpack.object({1, 2, 3}))}
    t.assert_equals(ret, {1, 2, 3})

    -- call
    t.assert_error_msg_content_equals(
        "Tuple/Key must be MsgPack array",
        function() c:call('echo', msgpack.object(123)) end)
    ret = {c:call('echo', {msgpack.object(123)})}
    t.assert_equals(ret, {123})
    ret = {c:call('echo', msgpack.object({}))}
    t.assert_equals(ret, {})
    ret = {c:call('echo', msgpack.object({1, 2, 3}))}
    t.assert_equals(ret, {1, 2, 3})

    c:close()
end
