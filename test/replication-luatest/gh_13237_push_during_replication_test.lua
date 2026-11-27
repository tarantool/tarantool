local lsocket = require('socket')
local msgpack = require('msgpack')
local server = require('luatest.server')
local t = require('luatest')

local key = box.iproto.key
local type = box.iproto.type

local g = t.group()

local wait_timeout = 60

local function iproto_error_type(error_type)
    return bit.bor(box.iproto.type.TYPE_ERROR, error_type)
end

local function socket_connect(server_)
    local uri = require('uri')
    local u = uri.parse(server_.net_box_uri)
    local s = lsocket.tcp_connect(u.host, u.service)
    t.assert_not_equals(s, nil)
    -- Skip the greeting.
    s:read(box.iproto.GREETING_SIZE, wait_timeout)
    return s
end

local function socket_write(s, header, body)
    return s:write(box.iproto.encode_packet(header, body))
end

local function socket_write_watch(s, sync, watch_key)
    return socket_write(s, {
        [key.REQUEST_TYPE] = type.WATCH,
        [key.SYNC] = sync,
    }, {[key.EVENT_KEY] = watch_key})
end

local function socket_read(s)
    local size_mp = s:read(5, wait_timeout)
    t.assert_equals(#size_mp, 5)
    local size = msgpack.decode(size_mp)
    local response = s:read(size, wait_timeout)
    t.assert_equals(#response, size)
    return box.iproto.decode_packet(size_mp .. response)
end

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master = server:new({alias = 'master'})
    cg.master:start()
end)

g.after_all(function(cg)
    cg.master:drop()
end)

-- A session watcher notification (as well as a box.session.push() result)
-- is written into the connection output buffer in the TX thread, but it
-- reaches the socket in the IPROTO thread, which is notified with the
-- kharon message. On kharon arrival the IPROTO thread used to signal the
-- connection output watcher unconditionally. If the connection entered the
-- replication mode meanwhile, i.e. a JOIN/SUBSCRIBE-like request was
-- accepted on it, the signal tripped an assertion in the debug build and
-- made the IPROTO thread mess with the socket owned by the replication
-- code in the release build.
g.test_no_crash_on_push_during_replication = function(cg)
    local s = socket_connect(cg.master)
    local watch_key = 'test'
    -- Register a session watcher. It runs at once and delivers the initial
    -- notification with the current value of the key.
    socket_write_watch(s, 1, watch_key)
    local header = socket_read(s)
    t.assert_equals(header[key.REQUEST_TYPE], type.EVENT)
    t.assert_equals(header[key.SYNC], 1)
    -- Acknowledge the notification: the watcher becomes idle, so it will
    -- run again on the next key update. The repeated WATCH request is an
    -- acknowledgement, the server doesn't reply to it.
    socket_write_watch(s, 2, watch_key)
    require('fiber').sleep(0.2)
    -- Sync with the server: the PING roundtrip proves the WATCH request
    -- above is processed and there is nothing in flight on the connection.
    socket_write(s, {
        [key.REQUEST_TYPE] = type.PING,
        [key.SYNC] = 3,
    }, {})
    header = socket_read(s)
    t.assert_equals(header[key.REQUEST_TYPE], type.OK)
    t.assert_equals(header[key.SYNC], 3)
    -- Hold the subscribe request in the TX thread to keep the connection
    -- in the replication mode: a request is counted as in progress since
    -- it is accepted in the TX thread, which strictly follows the moment
    -- the request is parsed and the connection is switched to the
    -- replication mode in the IPROTO thread. There are two requests in
    -- progress: the held subscribe request and the request sampling the
    -- statistic below.
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_PROCESS_REPLICATION_DELAY',
                                true)
    end)
    -- The instance UUID is omitted, so the subscribe request is going to
    -- fail with ER_NIL_UUID as soon as the injection is disabled.
    socket_write(s, {
        [key.REQUEST_TYPE] = type.SUBSCRIBE,
        [key.SYNC] = 4,
    }, {
        [key.REPLICASET_UUID] = cg.master:exec(function()
            return box.info.replicaset.uuid
        end),
        [key.REPLICA_ANON] = true,
    })
    t.helpers.retrying({delay = 0.001, timeout = wait_timeout}, function()
        t.assert_equals(cg.master:exec(function()
            return box.stat.net().REQUESTS_IN_PROGRESS.current
        end), 2)
    end)
    -- Update the key: the idle session watcher of our connection runs in
    -- the TX thread and delivers a new notification to the connection in
    -- the replication mode. Its kharon message lands there long before the
    -- sleep ends: the trip is a couple of cbus hops, which take
    -- microseconds.
    cg.master:exec(function(key_)
        box.broadcast(key_, 'data')
    end, {watch_key})
    require('fiber').sleep(0.5)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_PROCESS_REPLICATION_DELAY',
                                false)
    end)
    -- The subscription error is written to the socket by the replication
    -- code itself, then the connection is closed. Skip the notification
    -- data, if the server managed to write it before the error.
    t.helpers.retrying({delay = 0.001, timeout = wait_timeout}, function()
        while true do
            header = socket_read(s)
            local request_type = header[key.REQUEST_TYPE]
            if request_type ~= type.EVENT and request_type ~= type.CHUNK then
                t.assert_equals(request_type,
                                iproto_error_type(box.error.NIL_UUID))
                t.assert_equals(header[key.SYNC], 4)
                return
            end
        end
    end)
    s:close()
    t.assert(cg.master.process:is_alive())
    t.assert_equals(cg.master:grep_log('Assertion failed'), nil)
end
