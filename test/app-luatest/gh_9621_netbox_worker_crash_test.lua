local fiber = require('fiber')
local net = require('net.box')
local socket = require('socket')

local t = require('luatest')
local g = t.group()

local cond_send_leftover_packet = fiber.cond()
local handler = function(fd)
    -- Greeting.
    fd:write(box.iproto.encode_greeting())
    -- Wait for identification request to be sent from the client, so its
    -- send buffer gets empty.
    fd:read(1)
    -- Identification response.
    local error_type = bit.bor(box.iproto.type.TYPE_ERROR,
                               box.error.UNKNOWN_REQUEST_TYPE)
    local id_response = box.iproto.encode_packet({request_type = error_type})
    -- ce 00 00 00 03 81 00 00
    local packet = box.iproto.encode_packet({request_type = box.iproto.type.OK})
    -- ce 00 00 00 03
    local partial_packet = packet:sub(1, 5)
    -- 81 00 00 ce 00 00 00 03 81 00 00
    local leftover_packet = packet:sub(6) .. packet
    fd:write(id_response .. partial_packet)
    cond_send_leftover_packet:wait()
    fd:write(leftover_packet)
end

-- Test that the connection's worker fiber correctly handles scenario when
-- connection is closed concurrently to the `coio_wait` call.
g.test_worker_yield_from_coio_wait = function()
    local srv = socket.tcp_server('localhost', 0, handler)
    local c = net.new(srv:name().port, {fetch_schema = false})
    c:on_disconnect(function()
        fiber.yield()
    end)

    fiber.create(function()
        c:close()
    end)

    cond_send_leftover_packet:signal()

    fiber.yield()

    srv:close()
end

-- Test that the connection's worker fiber correctly handles scenario when
-- connection is closed concurrently to the `on_connect` trigger call.
g.test_worker_yield_from_on_connect_trigger = function()
    local cond_entered_on_connect_trigger = fiber.cond()
    local cond_return_from_on_connect_trigger = fiber.cond()

    local srv = socket.tcp_server('localhost', 0, handler)
    local c = net.new(srv:name().port, {wait_connected = false,
                                       fetch_schema = false})
    c:on_connect(function()
        cond_entered_on_connect_trigger:signal()
        cond_return_from_on_connect_trigger:wait()
    end)
    c:on_disconnect(function()
        fiber.yield()
    end)

    cond_entered_on_connect_trigger:wait()

    fiber.create(function()
        c:close()
    end)

    cond_send_leftover_packet:signal()
    cond_return_from_on_connect_trigger:signal()

    fiber.yield()

    srv:close()
end
