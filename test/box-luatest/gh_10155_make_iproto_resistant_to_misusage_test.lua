local lsocket = require('socket')
local msgpack = require('msgpack')
local server = require('luatest.server')
local t = require('luatest')
local uri = require('uri')

local key = box.iproto.key
local type = box.iproto.type

local g = t.group('make-iproto-resistant-to-misusage')
--
-- gh-10155: make iproto resistant to misusage.
--
local wait_timeout = 60

local function iproto_error_type(error_type)
    return bit.bor(box.iproto.type.TYPE_ERROR, error_type)
end

local function socket_connect(server)
    local u = uri.parse(server.net_box_uri)
    local s = lsocket.tcp_connect(u.host, u.service)
    t.assert_not_equals(s, nil)
    -- Skip the greeting
    s:read(box.iproto.GREETING_SIZE, wait_timeout)
    return s
end

local function encode_map(map)
    return msgpack.object(setmetatable(map, {__serialize = 'map'}))
end

local function setmap(map)
    return setmetatable(map, {__serialize = 'map'})
end

local function socket_write(s, header, body)
    return s:write(box.iproto.encode_packet(header, body))
end

local function write_ok(s, body)
    local header = {
        [key.REQUEST_TYPE] = box.iproto.type.OK,
        [key.SYNC] = 1,
    }
    if body == nil then
        body = {}
    end
    return socket_write(s, header, body)
end

local function write_eval(s, expr)
    local header = {
        [key.REQUEST_TYPE] = box.iproto.type.EVAL,
        [key.SYNC] = 1,
    }
    local body = {
        [key.EXPR] = expr,
        [key.TUPLE] = {},
    }
    return socket_write(s, header, body)
end

local function write_fetch_snapshot(s)
    local header = {
        [key.REQUEST_TYPE] = box.iproto.type.FETCH_SNAPSHOT,
        [key.SYNC] = 1,
    }
    local body = setmap({})
    return socket_write(s, header, body)
end

local function write_subscribe(s, uuid, replicaset_uuid, is_anon)
    local header = {
        [key.REQUEST_TYPE] = box.iproto.type.SUBSCRIBE,
        [key.SYNC] = 1,
    }
    local body = {
        [key.REPLICASET_UUID] = replicaset_uuid,
        [key.INSTANCE_UUID] = uuid,
        [key.VCLOCK] = encode_map({}),
        [key.REPLICA_ANON] = is_anon,
    }
    return socket_write(s, header, body)
end

local function socket_read(s)
    local size_mp = s:read(5, wait_timeout)
    t.assert_equals(#size_mp, 5)
    local size = msgpack.decode(size_mp)
    local response = s:read(size, wait_timeout)
    t.assert_equals(#response, size)
    return box.iproto.decode_packet(size_mp .. response)
end

-- Read all data sent in FETCH_SNAPSHOT response
local function read_snapshot(s)
    local h, _ = socket_read(s)
    local request_type = h[key.REQUEST_TYPE]
    while request_type == type.INSERT or request_type == type.RAFT_PROMOTE do
        h, _ = socket_read(s)
        request_type = h[key.REQUEST_TYPE]
    end
    t.assert_equals(request_type, type.OK)
end

g.before_each(function(g)
    g.server = server:new()
    g.server:start()
    g.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        for i = 1, 100 do
            box.space.test:replace{i}
        end
    end)
    g.s = socket_connect(g.server)
end)

g.after_each(function(g)
    g.s:close()
    g.server:stop()
end)

-- The case sends a dummy OK before subscribe
g.test_iproto_crash_on_subscribe = function(g)
    local uuid = require('uuid').str()
    local replicaset_uuid = g.server:eval('return box.info.replicaset.uuid')
    -- Write OK but do not read response before writing subscribe
    write_ok(g.s)
    write_subscribe(g.s, uuid, replicaset_uuid, true)
    local request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert_equals(request_type,
        iproto_error_type(box.error.UNKNOWN_REQUEST_TYPE))
    request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert(request_type == type.OK or request_type ==
        iproto_error_type(box.error.PROTOCOL))
end

-- The test is designed to check the behavior when, at the time of receiving a
-- replication request, the responses to all previous requests are written to
-- obuf but not flushed to the socket
g.test_iproto_crash_on_subscribe_flush_delay = function(g)
    t.tarantool.skip_if_not_debug()
    local uuid = require('uuid').str()
    local replicaset_uuid = g.server:eval('return box.info.replicaset.uuid')
    -- You can't use lustest.server:exec here because it will block
    write_eval(g.s, "box.error.injection.set(" ..
        "'ERRINJ_IPROTO_FLUSH_DELAY', true)")
    -- Several requests in a row to pretend to be a non-blocking client and
    -- provoke iproto_connection_feed_input, so that subscribe is immediately
    -- read
    for _ = 1, 3 do
        write_ok(g.s)
    end
    -- A short pause so that the packages do not end up in one batch and all
    -- responses have time to be written in obuf
    require('fiber').sleep(0.1)
    write_subscribe(g.s, uuid, replicaset_uuid, true)
    -- A short pause so that the tx thread has time to write the response to
    -- the subscribe request to the socket
    require('fiber').sleep(0.1)
    g.server:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_FLUSH_DELAY', false)
    end)
    -- Read IPROTO_EVAL response
    local request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert(request_type == type.OK)
    for _ = 1, 3 do
        request_type = socket_read(g.s)[key.REQUEST_TYPE]
        t.assert_equals(request_type,
            iproto_error_type(box.error.UNKNOWN_REQUEST_TYPE))
    end
    request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert(request_type == type.OK or request_type ==
        iproto_error_type(box.error.PROTOCOL))
end

-- The case sends dummy OKs before subscribe
g.test_iproto_crash_on_subscribe_spam_ok = function(g)
    local uuid = require('uuid').str()
    local replicaset_uuid = g.server:eval('return box.info.replicaset.uuid')
    -- Write OKs but do not read responses before writing subscribe
    for _ = 1, 100 do
        write_ok(g.s)
    end
    write_subscribe(g.s, uuid, replicaset_uuid, true)
    local request_type
    for _ = 1, 100 do
        request_type = socket_read(g.s)[key.REQUEST_TYPE]
        t.assert_equals(request_type,
            iproto_error_type(box.error.UNKNOWN_REQUEST_TYPE))
    end
    request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert(request_type == type.OK or request_type ==
        iproto_error_type(box.error.PROTOCOL))
end

-- The case simulates a situation where the user of anonymous replication
-- simply sent IPROTO_OK back after FETCH_SNAPSHOT by mistake (did not know
-- that Tarantool doesn't expect a reply on FETCH_SNAPSHOT)
g.test_iproto_crash_fetch_snapshot_subscribe = function(g)
    local uuid = require('uuid').str()
    local replicaset_uuid = g.server:eval('return box.info.replicaset.uuid')
    write_fetch_snapshot(g.s)
    local request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert_equals(request_type, type.OK)
    read_snapshot(g.s)
    -- Write OK but do not read response before writing subscribe
    write_ok(g.s)
    write_subscribe(g.s, uuid, replicaset_uuid, true)
    request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert_equals(request_type,
        iproto_error_type(box.error.UNKNOWN_REQUEST_TYPE))
    request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert(request_type == type.OK or request_type ==
        iproto_error_type(box.error.PROTOCOL))
end

-- The same as above, but additionally forgot to pass is_anon option to
-- subscribe
g.test_iproto_crash_fetch_snapshot_subscribe_not_anon = function(g)
    local uuid = require('uuid').str()
    local replicaset_uuid = g.server:eval('return box.info.replicaset.uuid')
    write_fetch_snapshot(g.s)
    local request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert_equals(request_type, type.OK)
    read_snapshot(g.s)
    -- Write OK but do not read response before writing subscribe
    write_ok(g.s)
    -- Subscribe as not anon replica
    write_subscribe(g.s, uuid, replicaset_uuid)
    request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert_equals(request_type,
        iproto_error_type(box.error.UNKNOWN_REQUEST_TYPE))
    request_type = socket_read(g.s)[key.REQUEST_TYPE]
    t.assert(request_type ==
        iproto_error_type(box.error.TOO_EARLY_SUBSCRIBE) or request_type ==
        iproto_error_type(box.error.PROTOCOL))
end
