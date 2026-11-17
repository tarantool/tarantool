--
-- The client side of a replica's join, done by hand: the requests a replica
-- sends to a node to become a member of its replica set, and the streams the
-- node answers with, returned as decoded packets.
--
--     local snapshot = replica_client.fetch_snapshot(node1.net_box_uri, {
--         timeout = TIMEOUT,
--     })
--     local registration = replica_client.register(node1.net_box_uri, {
--         uuid = snapshot.uuid, vclock = snapshot.vclock, timeout = TIMEOUT,
--     })
--     -- registration.instance_id is the id the node gave to the UUID.
--
-- Each call is a connection, a request, and the stream read to its end marker.
-- Nothing is kept between the calls: what the next one needs is taken from
-- the result of the previous one, or made up by the test.
--
-- The packets are described in replica_io.
--
local replica_io = require('test.replication-luatest.lib.replica_io')
local socket = require('socket')
local t = require('luatest')
local uri = require('uri')
local uuid = require('uuid')
local version = require('version')
local setmap = require('internal.utils').setmap

local M = {}

local key = box.iproto.key
local types = box.iproto.type

-- The error of an error packet, raised. The type of such a packet is the
-- error code with the error bit set.
local function raise_if_error(packet)
    if bit.band(packet.header[key.REQUEST_TYPE], types.TYPE_ERROR) ~= 0 then
        error(packet.body[key.ERROR_24])
    end
end

-- Connect to a node, past its greeting.
local function connect(node_uri, timeout)
    local u = uri.parse(node_uri)
    local sock = socket.tcp_connect(u.host, u.service, timeout)
    t.assert_not_equals(sock, nil, 'connected to ' .. node_uri)
    local greeting = sock:read(box.iproto.GREETING_SIZE, timeout)
    t.assert_equals(#greeting, box.iproto.GREETING_SIZE, 'greeting read')
    return sock
end

-- Read a stream of rows up to and including the OK marker carrying a vclock:
-- the join stages end so. The marker's vclock is returned with the rows, by
-- stage when the stream has JOIN_META and JOIN_SNAPSHOT markers.
local function read_stream(sock, timeout)
    local result = {rows = {}}
    local rows_dst = result.rows
    while true do
        local packet = replica_io.read_packet(sock, timeout)
        t.assert_not_equals(packet, nil, 'a packet read')
        raise_if_error(packet)
        local request_type = packet.header[key.REQUEST_TYPE]
        if request_type == types.OK then
            result.vclock = packet.body[key.VCLOCK]
            return result
        elseif request_type == types.JOIN_META then
            result.meta = {}
            rows_dst = result.meta
        elseif request_type == types.JOIN_SNAPSHOT then
            rows_dst = result.rows
        else
            table.insert(rows_dst, packet)
        end
    end
end

-- Fetch the node's snapshot: what a joining replica does first. The replica
-- is opts.uuid, random when not given. Returned are:
--
-- - uuid: the replica's UUID;
-- - start_vclock: the node's vclock when the read view was taken;
-- - meta: the join meta rows (the Raft state and the limbo state);
-- - rows: the data rows;
-- - vclock: the node's vclock at the end of the stream.
function M.fetch_snapshot(node_uri, opts)
    assert(opts.timeout ~= nil)
    local replica_uuid = opts.uuid or uuid.str()
    local sock = connect(node_uri, opts.timeout)
    replica_io.write_packet(sock, {
        header = {
            [key.REQUEST_TYPE] = types.FETCH_SNAPSHOT,
            [key.SYNC] = 1,
        },
        body = {
            [key.SERVER_VERSION] = box.internal.version_to_id(
                version(require('tarantool').version)),
            [key.INSTANCE_UUID] = replica_uuid,
        },
    })
    local packet = replica_io.read_packet(sock, opts.timeout)
    t.assert_not_equals(packet, nil, 'the snapshot header read')
    raise_if_error(packet)
    t.assert_equals(packet.header[key.REQUEST_TYPE], types.OK)
    local snapshot = read_stream(sock, opts.timeout)
    sock:close()
    snapshot.uuid = replica_uuid
    snapshot.start_vclock = packet.body[key.VCLOCK]
    return snapshot
end

-- Register at the node: the second step of the join. The replica is
-- opts.uuid, having the node's data up to opts.vclock. The node writes the
-- replica into _cluster and sends the rows it lacks, the registration
-- included. Returned are:
--
-- - uuid: the replica's UUID;
-- - instance_id: the id given to the replica, taken from its _cluster row;
-- - rows: the rows sent;
-- - vclock: the node's vclock at the end of the stream.
function M.register(node_uri, opts)
    assert(opts.timeout ~= nil)
    assert(opts.uuid ~= nil)
    assert(opts.vclock ~= nil)
    local sock = connect(node_uri, opts.timeout)
    replica_io.write_packet(sock, {
        header = {
            [key.REQUEST_TYPE] = types.REGISTER,
            [key.SYNC] = 1,
        },
        body = {
            [key.INSTANCE_UUID] = opts.uuid,
            [key.VCLOCK] = setmap(table.copy(opts.vclock)),
        },
    })
    local registration = read_stream(sock, opts.timeout)
    sock:close()
    registration.uuid = opts.uuid
    for _, packet in ipairs(registration.rows) do
        local body = packet.body
        if packet.header[key.REQUEST_TYPE] == types.INSERT and
           body[key.SPACE_ID] == box.schema.CLUSTER_ID and
           body[key.TUPLE][2] == opts.uuid then
            registration.instance_id = body[key.TUPLE][1]
        end
    end
    t.assert_not_equals(registration.instance_id, nil,
                        'the _cluster row received')
    return registration
end

return M
