local t = require('luatest')
local g = t.group('ghs-121')

g.test_too_large_response_size = function()
    local net = require('net.box')
    local socket = require('socket')
    local msgpack = require('msgpack')
    local greeting =
        'Tarantool 3.0.0 (Binary) b1a9c3e1-2ecd-4939-b114-3965986738ca  \n'..
        'GuAaXfJM0doawXKUqNCptueVDEQoEY1ZQ3vQkvj6/o4=                   \n'
    local data = msgpack.encode(18446744073709551614ULL)..msgpack.encode(1)
    local handler = function(fd) fd:write(greeting) fd:write(data) end
    local srv = socket.tcp_server('localhost', 0, {handler = handler})
    local res = net.new('localhost:' .. srv:name().port)
    t.assert_equals(res.error, "Response size too large")
end
