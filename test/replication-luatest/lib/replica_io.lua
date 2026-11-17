--
-- IPROTO packets on a plain socket, for the helpers speaking the replication
-- protocol by hand.
--
-- A packet is {header = ..., body = ...}, both tables keyed by box.iproto.key
-- (the body of a RAFT row by box.iproto.raft_key), encoded by
-- box.iproto.encode_packet(). The packets read are decoded the same way, with
-- the keys numeric. A table with integer keys - a header, a body, a vclock -
-- has to be marked as a map with internal.utils.setmap(): a plain table with
-- small integer keys encodes as an array with nil holes.
--
local msgpack = require('msgpack')

local M = {}

-- One packet from the socket: the 5-byte size prefix is msgpack, then the
-- payload - the header map and the body map, the latter optional. Nil on EOF
-- or timeout.
function M.read_packet(sock, timeout)
    local size_mp = sock:read(5, timeout)
    if size_mp == nil or #size_mp < 5 then
        return nil
    end
    local size = msgpack.decode(size_mp)
    local payload = sock:read(size, timeout)
    if payload == nil or #payload < size then
        return nil
    end
    local header, pos = msgpack.decode(payload)
    local body = nil
    if pos <= #payload then
        body = msgpack.decode(payload, pos)
    end
    return {header = header, body = body}
end

function M.write_packet(sock, packet)
    sock:write(box.iproto.encode_packet(packet.header, packet.body))
end

return M
