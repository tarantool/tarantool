--
-- A replication link under the test's control: the connection of one
-- downstream to one upstream, going through luatest's replica proxy and cut
-- into IPROTO packets. Each direction can hold its packets in a queue and
-- release them later, so the test decides what the other side sees, and when.
--
--     local link = replica_link.new{upstream = uri, alias = 'link_1_from_2'}
--     box_cfg.replication = {link.uri}  -- on the downstream
--     link:start()
--     link.down:hold()         -- relay -> applier: rows, heartbeats, Raft
--     link.down:wait_held(function(queue) ... end)
--     link.down:release()
--     link.up                  -- applier -> relay: the subscribe, the acks
--     link:stop()
--
-- The queue is a plain array of packets {type, header, body, raw}, in the
-- arrival order. A release forwards it from the front, so what the test does
-- to the array before the release is what the other side gets.
--
-- A hold keeps the heartbeats too. The downstream's applier gives up on a
-- silent upstream after 4 * replication_timeout, so a hold longer than that
-- needs the timeout raised first.
--
-- The link carries more than one connection at a time: beside the applier's
-- socket its ballot watcher keeps one of its own. The packets are cut per
-- connection, and each one goes back to the connection it came from. A
-- packet whose connection is gone by the release is dropped, with a log
-- line.
--
local log = require('log')
local proxy = require('luatest.replica_proxy')
local server = require('luatest.server')
local t = require('luatest')

local M = {}

-- A predicate on a packet: is it of the given IPROTO type.
function M.is_type(type)
    return function(packet)
        return packet.type == type
    end
end

function M.has_type(queue, type)
    for _, packet in ipairs(queue) do
        if packet.type == type then
            return true
        end
    end
    return false
end

-- Move the packets matching the predicate out of the queue. Both keep their
-- order.
function M.take_out(queue, pred)
    local taken = {}
    local i = 1
    while i <= #queue do
        if pred(queue[i]) then
            table.insert(taken, table.remove(queue, i))
        else
            i = i + 1
        end
    end
    return taken
end

local Direction = {}
Direction.__index = Direction

-- One direction of the link. The forward function is the proxy connection's
-- method sending raw bytes on. The greeting opens the stream from the
-- upstream and isn't a packet: it is let through as is.
local function direction_new(opts)
    return setmetatable({
        name = opts.name,
        forward = opts.forward,
        has_greeting = opts.has_greeting,
        timeout = opts.timeout,
        is_holding = false,
        queue = {},
        -- The stream state of each live connection: the unparsed tail and
        -- the greeting bytes still to pass.
        streams = setmetatable({}, {__mode = 'k'}),
    }, Direction)
end

-- The proxy's hook: a chunk of the socket stream, cut on packet boundaries.
-- The proxy's fiber dies silently on an error, hence the log line.
function Direction:process(conn, chunk)
    local ok, err = pcall(self.process_chunk, self, conn, chunk)
    if not ok then
        log.error('replica_link %s: %s', self.name, tostring(err))
        error(err)
    end
end

function Direction:process_chunk(conn, chunk)
    local stream = self.streams[conn]
    if stream == nil then
        stream = {
            buf = '',
            greeting_left = self.has_greeting and box.iproto.GREETING_SIZE or 0,
        }
        self.streams[conn] = stream
    end
    if stream.greeting_left > 0 then
        local n = math.min(stream.greeting_left, #chunk)
        self.forward(conn, chunk:sub(1, n))
        stream.greeting_left = stream.greeting_left - n
        chunk = chunk:sub(n + 1)
    end
    local buf = stream.buf .. chunk
    local pos = 1
    while true do
        local header, body, next_pos = box.iproto.decode_packet(buf, pos)
        if header == nil then
            break
        end
        local packet = {
            type = header.request_type,
            header = header,
            body = body,
            raw = buf:sub(pos, next_pos - 1),
            conn = conn,
        }
        pos = next_pos
        if self.is_holding then
            table.insert(self.queue, packet)
        else
            self.forward(conn, packet.raw)
        end
    end
    stream.buf = buf:sub(pos)
end

-- Send a held packet back to its connection, if the connection is still
-- there.
function Direction:forward_held(packet)
    local conn = packet.conn
    if not conn.client_connected or not conn.server_connected then
        log.info('replica_link %s: a held packet of a closed connection ' ..
                 'dropped', self.name)
        return
    end
    self.forward(conn, packet.raw)
end

-- Queue every packet from now on.
function Direction:hold()
    self.is_holding = true
end

-- Forward the first n queued packets, keep holding the rest.
function Direction:release_first(n)
    assert(self.is_holding)
    t.assert_le(n, #self.queue)
    for _ = 1, n do
        self:forward_held(table.remove(self.queue, 1))
    end
end

-- Forward the whole queue in order and stop holding. The packets arriving
-- while the queue is being written go after it.
function Direction:release()
    assert(self.is_holding)
    while #self.queue > 0 do
        self:forward_held(table.remove(self.queue, 1))
    end
    self.is_holding = false
end

-- Wait until the queue holds at least cond packets, when cond is a number,
-- or until cond(queue) is true.
function Direction:wait_held(cond, timeout)
    assert(self.is_holding)
    local check
    if type(cond) == 'number' then
        check = function()
            t.assert_ge(#self.queue, cond, self:queue_str())
        end
    else
        check = function()
            t.assert(cond(self.queue), self:queue_str())
        end
    end
    t.helpers.retrying({timeout = timeout or self.timeout}, check)
end

-- The queue as the packet type names, for the failure messages.
function Direction:queue_str()
    local names = {}
    for name, type in pairs(box.iproto.type) do
        names[type] = name
    end
    local types = {}
    for _, packet in ipairs(self.queue) do
        table.insert(types, names[packet.type] or tostring(packet.type))
    end
    return string.format('%s queue: [%s]', self.name,
                         table.concat(types, ', '))
end

local Link = {}
Link.__index = Link

-- opts.upstream is the upstream's listen URI, opts.alias names the link's own
-- socket, put next to the replica set's ones by opts.replica_set_id.
-- opts.timeout is the default of the waits.
function M.new(opts)
    local link = setmetatable({
        uri = server.build_listen_uri(opts.alias, opts.replica_set_id),
        down = direction_new({
            name = opts.alias .. ' down',
            forward = function(conn, data)
                conn:forward_to_client(data)
            end,
            has_greeting = true,
            timeout = opts.timeout,
        }),
        up = direction_new({
            name = opts.alias .. ' up',
            forward = function(conn, data)
                conn:forward_to_server(data)
            end,
            has_greeting = false,
            timeout = opts.timeout,
        }),
    }, Link)
    link.proxy = proxy:new({
        client_socket_path = link.uri,
        server_socket_path = opts.upstream,
        process_server = {
            func = function(conn, chunk)
                link.down:process(conn, chunk)
            end,
            post = function(conn)
                conn:stop()
            end,
        },
        process_client = {
            func = function(conn, chunk)
                link.up:process(conn, chunk)
            end,
            post = function(conn)
                conn:stop()
            end,
        },
    })
    return link
end

function Link:start()
    t.assert(self.proxy:start())
end

function Link:stop()
    self.proxy:stop()
end

-- The proxy's own pause and resume: no socket reads at all.
function Link:pause()
    self.proxy:pause()
end

function Link:resume()
    self.proxy:resume()
end

return M
