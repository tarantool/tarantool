--
-- The serving side of a replica, done by hand: an upstream for the appliers of
-- real nodes, with no instance behind it. It feeds them the rows the test
-- builds, packet by packet. Any row can be sent, well-formed or not.
--
--     local server = replica_server.new{
--         alias = 'server2', replica_set_id = cg.replica_set.id,
--         uuid = registration.uuid, instance_id = registration.instance_id,
--         timeout = TIMEOUT,
--     }
--     server:start()
--     node1:update_box_cfg{replication = {server.uri}}
--     local session = server:wait_subscribe()
--     session:send(replica_server.rows.insert{
--         origin = server.instance_id, lsn = 1, ...
--     })
--     session:close()
--     server:stop()
--
-- The server never connects anywhere itself. Its identity - the UUID and the
-- id - is given: a real node replicates only from a registered replica, see
-- replica_client for the registration.
--
-- Every connection accepted is a session, served the same way: the greeting,
-- IPROTO_ID answered with no features - so the ballot is asked through a plain
-- VOTE, which gets an empty ballot - and SUBSCRIBE with an empty vclock, so
-- the applier follows on the heartbeat sent right after. A subscribed session
-- is a subscriber, handed out by wait_subscribe() in the order of
-- subscription, with the SUBSCRIBE request kept in it. The acks are read and
-- the latest is kept. Nothing else is expected: an unknown request fails the
-- session, with a log line. No heartbeats are sent on their own after the
-- first one: the applier's replication_timeout has to cover the silence.
--
-- The packets are described in replica_io. The builders in
-- replica_server.rows make the popular ones.
--
local fiber = require('fiber')
local log = require('log')
local luatest_server = require('luatest.server')
local replica_io = require('test.replication-luatest.lib.replica_io')
local socket = require('socket')
local t = require('luatest')
local setmap = require('internal.utils').setmap

local M = {}

local key = box.iproto.key
local types = box.iproto.type

-- An empty msgpack map: an empty table encodes as an array.
local empty_map = setmap{}

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

local function session_new(server, sock)
    return setmetatable({
        server = server,
        sock = sock,
        fiber = nil,
        -- The SUBSCRIBE packet, once it came.
        subscribe = nil,
        -- The vclock of the latest ack.
        ack_vclock = nil,
        is_sending = false,
        is_closed = false,
    }, Session)
end

-- Serve the session: the greeting, then the requests until the socket closes.
function Session:serve()
    local sock = self.sock
    local server = self.server
    sock:write(box.iproto.encode_greeting{uuid = server.uuid})
    while true do
        local packet = replica_io.read_packet(sock, server.timeout)
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
            self:send(reply)
        elseif request_type == types.VOTE then
            reply.body = {[key.BALLOT] = empty_map}
            self:send(reply)
        elseif request_type == types.SUBSCRIBE then
            reply.header[key.REPLICA_ID] = server.instance_id
            reply.body = {[key.VCLOCK] = empty_map}
            self:send(reply)
            self.subscribe = packet
            -- The applier checks its sync on a packet: the first heartbeat
            -- takes it from 'sync' to 'follow'.
            self:send(rows.heartbeat())
            -- The session is handed out to the test here, and the test
            -- writes into it. This fiber must not write from now on: two
            -- writers would mix their bytes when a write yields.
            table.insert(server.subscribers, self)
        elseif request_type == types.OK then
            self.ack_vclock = body[key.VCLOCK]
        else
            error(string.format('unexpected request type %d', request_type))
        end
    end
end

-- Send one packet. One writer at a time: a write can yield, and another one
-- started meanwhile would land in the middle of the packet.
function Session:send(packet)
    assert(not self.is_sending, 'a concurrent send into one session')
    self.is_sending = true
    local ok, err = pcall(replica_io.write_packet, self.sock, packet)
    self.is_sending = false
    if not ok then
        error(err)
    end
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

local Server = {}
Server.__index = Server

-- opts.alias names the socket, put next to the replica set's ones by
-- opts.replica_set_id. opts.uuid and opts.instance_id are the identity.
-- opts.timeout bounds the waits.
function M.new(opts)
    assert(opts.uuid ~= nil)
    assert(opts.instance_id ~= nil)
    assert(opts.timeout ~= nil)
    return setmetatable({
        uri = luatest_server.build_listen_uri(opts.alias, opts.replica_set_id),
        uuid = opts.uuid,
        instance_id = opts.instance_id,
        timeout = opts.timeout,
        is_running = false,
        -- Every session accepted, served or done.
        sessions = {},
        -- The subscribed ones, in the order of subscription.
        subscribers = {},
        -- How many of them were handed out by wait_subscribe().
        subscriber_count_taken = 0,
    }, Server)
end

function Server:accept(sock)
    sock:nonblock(true)
    local session = session_new(self, sock)
    session.fiber = fiber.new(function()
        local ok, err = pcall(session.serve, session)
        if not ok then
            log.error('replica_server %s: %s', self.uri, tostring(err))
        end
    end)
    session.fiber:set_joinable(true)
    session.fiber:name('ReplicaServerIO')
    table.insert(self.sessions, session)
end

function Server:start()
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
    self.worker:name('ReplicaServerAccept')
end

function Server:stop()
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
function Server:wait_subscribe()
    local idx = self.subscriber_count_taken + 1
    t.helpers.retrying({timeout = self.timeout}, function()
        t.assert_not_equals(self.subscribers[idx], nil,
                            'an applier subscribed')
    end)
    self.subscriber_count_taken = idx
    return self.subscribers[idx]
end

return M
