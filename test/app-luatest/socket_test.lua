local socket = require('socket')
local t = require('luatest')

local g = t.group()

g.test_from_fd = function()
    local errmsg = 'fd must be a number'
    t.assert_error_msg_content_equals(errmsg, socket.from_fd)
    t.assert_error_msg_content_equals(errmsg, socket.from_fd, 'foo')

    local s1 = socket('AF_INET', 'SOCK_STREAM', 'tcp')
    t.assert(s1)
    local s2 = socket.from_fd(s1:fd())
    t.assert(s2)
    t.assert_equals(s2:fd(), s1:fd())
    t.assert_equals(s2.itype, socket.internal.SO_TYPE.SOCK_STREAM)

    t.assert(s1:close())
    t.assert_not(s2:close())

    s1 = socket('AF_INET', 'SOCK_DGRAM', 'udp')
    t.assert(s1)
    s2 = socket.from_fd(s1:fd())
    t.assert(s2)
    t.assert_equals(s2:fd(), s1:fd())
    t.assert_equals(s2.itype, socket.internal.SO_TYPE.SOCK_DGRAM)

    t.assert(s1:close())
    t.assert_not(s2:close())

    local invalid_fd = 100500
    local s = socket.from_fd(invalid_fd)
    t.assert(s)
    t.assert(s:fd(), invalid_fd)
    t.assert(s.itype, 0)
    t.assert_not(s:close())
end
