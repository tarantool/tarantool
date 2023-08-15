local fiber = require('fiber')
local net = require('net.box')
local server = require('luatest.server')
local socket = require('socket')
local yaml = require('yaml')
local t = require('luatest')

local g = t.group()

local function serialize(x)
    return yaml.decode(yaml.encode(x))
end

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        box.session.su('admin', function()
            box.schema.user.create('alice', {password = 'secret'})
            box.schema.user.grant('alice', 'super')
        end)
    end)
    cg.connect = function()
        return socket.tcp_connect('unix/', cg.server.net_box_uri)
    end
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks errors raised on invalid arguments.
g.test_invalid_args = function()
    t.assert_error_msg_equals(
        "Illegal parameters, fd should be a number",
        net.from_fd)
    t.assert_error_msg_equals(
        "Illegal parameters, options should be a table",
        net.from_fd, 0, 0)
    t.assert_error_msg_equals(
        "Illegal parameters, unexpected option 'foo'",
        net.from_fd, 0, {foo = 'bar'})
    t.assert_error_msg_equals(
        "Illegal parameters, " ..
        "options parameter 'user' should be of type string",
        net.from_fd, 0, {user = 123})
    t.assert_error_msg_equals(
        "Illegal parameters, " ..
        "options parameter 'fetch_schema' should be of type boolean",
        net.from_fd, 0, {fetch_schema = 'foo'})
    t.assert_error_msg_equals(
        "Invalid fd: expected nonnegative integer",
        net.from_fd, -1)
    t.assert_error_msg_equals(
        "Invalid fd: expected nonnegative integer",
        net.from_fd, 2 ^ 31)
    t.assert_error_msg_equals(
        "Invalid fd: expected nonnegative integer",
        net.from_fd, 1.5)
end

-- Checks basic functionality.
g.test_basic = function(cg)
    local s = cg.connect()
    local fd = s:fd()
    local c = net.from_fd(fd)
    s:detach()
    t.assert_equals(c.state, 'active')

    t.assert_equals(c.fd, fd)
    t.assert_is(c.host, nil)
    t.assert_is(c.port, nil)

    local v = serialize(c)
    t.assert_equals(v.fd, fd)
    t.assert_is(v.host, nil)
    t.assert_is(v.port, nil)

    t.assert_equals(c:call('box.session.type'), 'binary')
    t.assert_equals(c:call('box.session.user'), 'guest')
    t.assert_equals(c:call('box.session.peer'), 'unix/:(socket)')

    local c2 = net.connect(cg.server.net_box_uri)
    local v2 = serialize(c2)

    v.fd = nil
    v2.host = nil
    v2.port = nil
    t.assert_equals(v, v2)

    c2:close()
    c:close()
end

-- Checks that fd is closed with connection.
g.test_fd_closed = function(cg)
    local s = cg.connect()
    t.assert_covers(s:name(), {type = 'SOCK_STREAM'})
    local c = net.from_fd(s:fd())
    t.assert_equals(c.state, 'active')
    c:close()
    t.helpers.retrying({}, function()
        t.assert_is(s:name(), nil)
    end)
    t.assert_not(s:close())
end

-- Checks that authentication works.
g.test_auth = function(cg)
    local s = cg.connect()
    local c = net.from_fd(s:fd(), {user = 'alice', password = 'secret'})
    s:detach()
    t.assert_equals(c.state, 'active')
    t.assert_equals(c:call('box.session.user'), 'alice')
    c:close()
end

g.after_test('test_reconnect', function()
    box.error.injection.set('ERRINJ_NETBOX_IO_ERROR', false)
end)

-- Checks that reconnect is disabled.
g.test_reconnect = function(cg)
    t.tarantool.skip_if_not_debug()
    local s = cg.connect()
    local c = net.from_fd(s:fd(), {reconnect_after = 0.1})
    s:detach()
    t.assert(c:ping())
    box.error.injection.set('ERRINJ_NETBOX_IO_ERROR', true)
    t.assert_not(c:ping())
    t.assert_equals(c.state, 'error')
    box.error.injection.set('ERRINJ_NETBOX_IO_ERROR', false)
    fiber.sleep(0.1)
    t.assert_equals(c.state, 'error')
    c:close()
end

-- Passing an invalid fd.
g.test_invalid_fd = function()
    local c = net.from_fd(9000)
    t.assert_equals(c.state, 'error')
    t.assert_str_contains(c.error, 'Bad file descriptor')
    c:close()
end
