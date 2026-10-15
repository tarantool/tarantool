local msgpack = require('msgpack')
local server = require('luatest.server')
local socket = require('socket')
local t = require('luatest')
local uri = require('uri')

local g = t.group('iproto_packet_size', {
    {length = 0x80000001},
    {length = 0x100000000},
})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    if cg.socket ~= nil then
        cg.socket:close()
        cg.socket = nil
    end
end)

local GREETING_SIZE = 128

local function decode_packet(data)
    local response_length, start_offset = msgpack.decode(data)
    local header, end_offset = msgpack.decode(data, start_offset)
    local body
    body, end_offset = msgpack.decode(data, end_offset)
    t.assert_equals(end_offset - start_offset, response_length)
    return header, body
end

g.test_oversized_length_without_payload = function(cg)
    local u = uri.parse(cg.server.net_box_uri)
    local s = socket.tcp_connect(u.host, u.service)
    t.assert(s)
    cg.socket = s
    local timeout = 5
    t.assert_equals(#s:read(GREETING_SIZE, timeout), GREETING_SIZE)

    -- A complete length prefix must be checked before reserving space for
    -- the payload, even when no payload bytes have arrived yet.
    local length = msgpack.encode(cg.params.length)
    t.assert_equals(s:write(length, timeout), #length)
    local response_length = s:read(5, timeout)
    t.assert_equals(#response_length, 5)
    local size = msgpack.decode(response_length)
    local response = s:read(size, timeout)
    t.assert_equals(#response, size)
    local header, body = decode_packet(response_length .. response)
    local error_type = bit.bor(box.iproto.type.TYPE_ERROR,
                               box.error.INVALID_MSGPACK)
    local request_type = header[box.iproto.key.REQUEST_TYPE]
    local err_content = body[box.iproto.key.ERROR_24]
    t.assert_equals(request_type, error_type)
    t.assert_equals(err_content, 'Invalid MsgPack - ' ..
                    'too big packet size in the header: ' .. cg.params.length)
    t.assert_equals(s:read(1, timeout), '')
end
