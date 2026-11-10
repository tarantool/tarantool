--
-- A fake replica: a node of the replica set with no instance behind it. It
-- joins a real node as a client, FETCH_SNAPSHOT and REGISTER, consuming what
-- the node sends, and then serves the node's applier as an upstream, feeding it
-- the rows the test builds, packet by packet. Any row can be sent, well-formed
-- or not.
--
--     local upstream = fake_upstream.new{
--         alias = 'fake_node2', replica_set_id = cg.replica_set.id,
--         timeout = TIMEOUT,
--     }
--     local snapshot = upstream:fetch_snapshot(node1.net_box_uri)
--     local catchup = upstream:register(node1.net_box_uri)
--     -- upstream.instance_id is known now.
--     upstream:start()
--     node1:update_box_cfg{replication = {upstream.uri}}
--     local session = upstream:wait_subscribe()
--     session:send(rows.promote{
--         origin = upstream.instance_id, lsn = upstream:next_lsn(), ...
--     })
--     session:close()
--     upstream:stop()
--
-- The join is two plain calls, each a connection, a request, and the stream
-- read to its end marker, returned as decoded packets: the snapshot's join
-- meta rows and data rows, the catch-up rows of the registration. The vclock
-- of the end markers is kept for the next call. The id is learned from the
-- _cluster row the registration sends.
--
-- Every connection accepted by the server side is a session, served the same
-- way: the greeting, IPROTO_ID answered with no features - so the ballot is
-- asked through a plain VOTE, which gets an empty ballot - and SUBSCRIBE with
-- an empty vclock, so the applier follows on the heartbeat sent right after. A
-- subscribed session is a subscriber, handed out by wait_subscribe() in the
-- order of subscription, with the SUBSCRIBE request kept in it. The acks are
-- read and the latest is kept. Nothing else is expected: an unknown request
-- fails the session, with a log line. No heartbeats are sent on their own
-- after the first one: the applier's replication_timeout has to cover the
-- silence.
--
-- A packet is {header = ..., body = ...}, both tables keyed by box.iproto.key
-- (the body of a RAFT row by box.iproto.raft_key), encoded by
-- box.iproto.encode_packet(). The packets read are decoded the same way, with
-- the keys numeric. A table with integer keys - a header, a body, a vclock, a
-- term map - has to be marked as a map with internal.utils.setmap(): a plain
-- table with small integer keys encodes as an array with nil holes. The
-- builders in fake_upstream.rows make the popular packets.
--
local fiber = require('fiber')
local log = require('log')
local msgpack = require('msgpack')
local server = require('luatest.server')
local socket = require('socket')
local t = require('luatest')
local uri = require('uri')
local uuid = require('uuid')
local version = require('version')
local setmap = require('internal.utils').setmap

local M = {}

local key = box.iproto.key
local types = box.iproto.type

-- An empty msgpack map: an empty table encodes as an array.
local empty_map = setmap{}

-- One packet from the socket: the 5-byte size prefix is msgpack, then the
-- payload - the header map and the body map, the latter optional. Nil on EOF
-- or timeout.
local function read_packet(sock, timeout)
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

local function write_packet(sock, packet)
    sock:write(box.iproto.encode_packet(packet.header, packet.body))
end

-- The error of an error packet, raised. The type of such a packet is the
-- error code with the error bit set.
local function raise_if_error(packet)
    if bit.band(packet.header[key.REQUEST_TYPE], types.TYPE_ERROR) ~= 0 then
        error(packet.body[key.ERROR_24])
    end
end

--
-- Builders of the popular packets.
--
M.rows = {}
local rows = M.rows

-- A heartbeat: an OK row with the time and no body.
function rows.heartbeat()
    return {header = setmap{
        [key.REQUEST_TYPE] = types.OK,
        [key.TIMESTAMP] = fiber.time(),
    }}
end

-- A DML row: opts.space_id, opts.tuple, from opts.origin at opts.lsn.
function rows.insert(opts)
    return {
        header = setmap{
            [key.REQUEST_TYPE] = types.INSERT,
            [key.REPLICA_ID] = opts.origin,
            [key.LSN] = opts.lsn,
        },
        body = setmap{
            [key.SPACE_ID] = opts.space_id,
            [key.TUPLE] = opts.tuple,
        },
    }
end

-- A PROMOTE from opts.origin at opts.lsn: opts.term, replacing opts.owner at
-- opts.confirm_lsn, with opts.confirmed_vclock and opts.term_map (maps
-- already), waiting for acks when opts.wait_ack. The row LSN of a checkpoint
-- row is opts.row_lsn, absent otherwise.
function rows.promote(opts)
    local flags = 0
    if opts.wait_ack then
        flags = box.iproto.flag.WAIT_ACK
    end
    return {
        header = setmap{
            [key.REQUEST_TYPE] = types.RAFT_PROMOTE,
            [key.REPLICA_ID] = opts.origin,
            [key.LSN] = opts.lsn,
        },
        body = setmap{
            [key.TERM] = opts.term,
            [key.REPLICA_ID] = opts.owner,
            [key.LSN] = opts.confirm_lsn,
            [key.VCLOCK] = opts.confirmed_vclock,
            [key.TERM_MAP] = opts.term_map,
            [key.FLAGS] = flags,
            [key.ROW_LSN] = opts.row_lsn,
        },
    }
end

-- A CONFIRM from opts.origin at opts.lsn: opts.owner's rows up to
-- opts.confirm_lsn.
function rows.confirm(opts)
    return {
        header = setmap{
            [key.REQUEST_TYPE] = types.RAFT_CONFIRM,
            [key.REPLICA_ID] = opts.origin,
            [key.LSN] = opts.lsn,
        },
        body = setmap{
            [key.REPLICA_ID] = opts.owner,
            [key.LSN] = opts.confirm_lsn,
        },
    }
end

--
-- One accepted connection of an applier. Before the SUBSCRIBE it is the
-- handshake, the ballot watcher's session never gets past it. After the
-- SUBSCRIBE it is a subscriber: the test sends rows into it.
--
local Session = {}
Session.__index = Session

local function session_new(upstream, sock)
    return setmetatable({
        upstream = upstream,
        sock = sock,
        fiber = nil,
        -- The SUBSCRIBE packet, once it came.
        subscribe = nil,
        -- The vclock of the latest ack.
        ack_vclock = nil,
        is_closed = false,
    }, Session)
end

-- Serve the session: the greeting, then the requests until the socket closes.
function Session:serve()
    local sock = self.sock
    local upstream = self.upstream
    sock:write(box.iproto.encode_greeting{uuid = upstream.uuid})
    while true do
        local packet = read_packet(sock, upstream.timeout)
        if packet == nil then
            return
        end
        local header, body = packet.header, packet.body
        local request_type = header[key.REQUEST_TYPE]
        local reply = {header = {
            [key.REQUEST_TYPE] = types.OK,
            [key.SYNC] = header[key.SYNC],
        }}
        if request_type == types.ID then
            reply.body = empty_map
            write_packet(sock, reply)
        elseif request_type == types.VOTE then
            reply.body = {[key.BALLOT] = empty_map}
            write_packet(sock, reply)
        elseif request_type == types.SUBSCRIBE then
            assert(upstream.instance_id ~= nil, 'registered')
            reply.header[key.REPLICA_ID] = upstream.instance_id
            reply.body = {[key.VCLOCK] = empty_map}
            write_packet(sock, reply)
            self.subscribe = packet
            -- The applier checks its sync on a packet: the first heartbeat
            -- takes it from 'sync' to 'follow'.
            self:send(rows.heartbeat())
            table.insert(upstream.subscribers, self)
        elseif request_type == types.OK then
            self.ack_vclock = body[key.VCLOCK]
        else
            error(string.format('unexpected request type %d', request_type))
        end
    end
end

-- Send one packet.
function Session:send(packet)
    write_packet(self.sock, packet)
end

-- Close the session: the applier sees the upstream gone.
function Session:close()
    if self.is_closed then
        return
    end
    self.is_closed = true
    self.fiber:cancel()
    self.sock:close()
    self.fiber:join()
end

local Upstream = {}
Upstream.__index = Upstream

-- opts.alias names the socket, put next to the replica set's ones by
-- opts.replica_set_id. opts.uuid is the identity, random when not given, the
-- id comes with the registration. opts.timeout bounds the waits.
function M.new(opts)
    return setmetatable({
        uri = server.build_listen_uri(opts.alias, opts.replica_set_id),
        uuid = opts.uuid or uuid.str(),
        instance_id = nil,
        timeout = opts.timeout,
        -- The vclock of the latest end marker received: what the fake has.
        vclock = nil,
        -- The LSN of the latest row authored by the fake.
        lsn = 0,
        is_running = false,
        -- Every session accepted, served or done.
        sessions = {},
        -- The subscribed ones, in the order of subscription.
        subscribers = {},
        -- How many of them were handed out by wait_subscribe().
        subscriber_count_taken = 0,
    }, Upstream)
end

-- The LSN for the next row authored by the fake.
function Upstream:next_lsn()
    self.lsn = self.lsn + 1
    return self.lsn
end

-- Connect to a node as a client, past its greeting.
function Upstream:connect(node_uri)
    local u = uri.parse(node_uri)
    local sock = socket.tcp_connect(u.host, u.service, self.timeout)
    t.assert_not_equals(sock, nil, 'connected to ' .. node_uri)
    local greeting = sock:read(box.iproto.GREETING_SIZE, self.timeout)
    t.assert_equals(#greeting, box.iproto.GREETING_SIZE, 'greeting read')
    return sock
end

-- Read a stream of rows up to and including the OK marker carrying a vclock:
-- the join stages end so. The marker's vclock is returned with the rows, by
-- stage when the stream has JOIN_META and JOIN_SNAPSHOT markers.
function Upstream:read_stream(sock)
    local result = {rows = {}}
    local rows_dst = result.rows
    while true do
        local packet = read_packet(sock, self.timeout)
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

-- Fetch the node's snapshot: what a joining replica does first. Returned are
-- the node's vclock when the read view was taken, the join meta rows (the
-- Raft state and the limbo's PROMOTEs), the data rows, and the node's vclock
-- at the end of the stream. The fake is at that vclock now.
function Upstream:fetch_snapshot(node_uri)
    local sock = self:connect(node_uri)
    write_packet(sock, {
        header = {
            [key.REQUEST_TYPE] = types.FETCH_SNAPSHOT,
            [key.SYNC] = 1,
        },
        body = {
            [key.SERVER_VERSION] = box.internal.version_to_id(
                version(require('tarantool').version)),
            [key.INSTANCE_UUID] = self.uuid,
        },
    })
    local packet = read_packet(sock, self.timeout)
    t.assert_not_equals(packet, nil, 'the snapshot header read')
    raise_if_error(packet)
    t.assert_equals(packet.header[key.REQUEST_TYPE], types.OK)
    local snapshot = self:read_stream(sock)
    sock:close()
    snapshot.start_vclock = packet.body[key.VCLOCK]
    self.vclock = snapshot.vclock
    return snapshot
end

-- Register at the node: the second step of the join. The node writes the
-- fake into _cluster and sends the rows the fake lacks, the registration
-- included - the id is taken from it. Returned are the rows and the node's
-- vclock at the end of the stream. The fake is at that vclock now.
function Upstream:register(node_uri)
    assert(self.vclock ~= nil, 'the snapshot fetched')
    local sock = self:connect(node_uri)
    write_packet(sock, {
        header = {
            [key.REQUEST_TYPE] = types.REGISTER,
            [key.SYNC] = 1,
        },
        body = {
            [key.INSTANCE_UUID] = self.uuid,
            [key.VCLOCK] = setmap(self.vclock),
        },
    })
    local catchup = self:read_stream(sock)
    sock:close()
    for _, packet in ipairs(catchup.rows) do
        local body = packet.body
        if packet.header[key.REQUEST_TYPE] == types.INSERT and
           body[key.SPACE_ID] == box.schema.CLUSTER_ID and
           body[key.TUPLE][2] == self.uuid then
            self.instance_id = body[key.TUPLE][1]
        end
    end
    t.assert_not_equals(self.instance_id, nil, 'the _cluster row received')
    self.vclock = catchup.vclock
    return catchup
end

function Upstream:accept(sock)
    sock:nonblock(true)
    local session = session_new(self, sock)
    session.fiber = fiber.new(function()
        local ok, err = pcall(session.serve, session)
        if not ok then
            log.error('fake_upstream %s: %s', self.uri, tostring(err))
        end
    end)
    session.fiber:set_joinable(true)
    session.fiber:name('FakeUpstreamIO')
    table.insert(self.sessions, session)
end

function Upstream:start()
    local listen = socket('PF_UNIX', 'SOCK_STREAM', 0)
    t.assert(listen:bind('unix/', self.uri), listen:error())
    listen:nonblock(true)
    t.assert(listen:listen(16), listen:error())
    self.listen = listen
    self.is_running = true
    self.worker = fiber.new(function()
        while self.is_running do
            if listen:readable(0.001) then
                local sock = listen:accept()
                if sock ~= nil then
                    self:accept(sock)
                end
            end
        end
    end)
    self.worker:set_joinable(true)
    self.worker:name('FakeUpstreamAccept')
end

function Upstream:stop()
    if not self.is_running then
        return
    end
    self.is_running = false
    self.worker:join()
    for _, session in pairs(self.sessions) do
        session:close()
    end
    self.sessions = {}
    self.subscribers = {}
    self.subscriber_count_taken = 0
    self.listen:close()
    os.remove(self.uri)
end

-- Wait for the next applier to subscribe and return its session.
function Upstream:wait_subscribe()
    local idx = self.subscriber_count_taken + 1
    t.helpers.retrying({timeout = self.timeout}, function()
        t.assert_not_equals(self.subscribers[idx], nil,
                            'an applier subscribed')
    end)
    self.subscriber_count_taken = idx
    return self.subscribers[idx]
end

return M
