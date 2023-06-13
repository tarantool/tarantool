local msgpack = require('msgpack')
local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

--
-- The test checks that unknown IPROTO keys are silently ignored by both
-- net.box client and IPROTO server.
--
local g = t.group()

local iproto_key = box.iproto.key
local iproto_type = box.iproto.type

local function test_request(conn, header, body)
    local packet = msgpack.encode(header)
    if body ~= nil then
        packet = packet .. msgpack.encode(body)
    end
    packet = msgpack.encode(#packet) .. packet
    return conn:_inject(packet)
end

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_select = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.SELECT,
        [iproto_key.SYNC] = conn:_next_sync(),
        [9999] = 'foobar',
    }, {
        [iproto_key.SPACE_ID] = 272, -- _schema
        [iproto_key.KEY] = {'some_key'},
        [iproto_key.LIMIT] = 0,
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_replace = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.REPLACE,
        [iproto_key.SYNC] = conn:_next_sync(),
        [9999] = 'foobar',
    }, {
        [iproto_key.SPACE_ID] = 272, -- _schema
        [iproto_key.TUPLE] = {'some_key'},
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_delete = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.DELETE,
        [iproto_key.SYNC] = conn:_next_sync(),
        [9999] = 'foobar',
    }, {
        [iproto_key.SPACE_ID] = 272, -- _schema
        [iproto_key.KEY] = {'some_key'},
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_begin = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.BEGIN,
        [iproto_key.SYNC] = conn:_next_sync(),
        [iproto_key.STREAM_ID] = 42,
        [9999] = 'foobar',
    }, {
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_commit = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.BEGIN,
        [iproto_key.SYNC] = conn:_next_sync(),
        [iproto_key.STREAM_ID] = 42,
    })
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.COMMIT,
        [iproto_key.SYNC] = conn:_next_sync(),
        [iproto_key.STREAM_ID] = 42,
        [9999] = 'foobar',
    }, {
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_rollback = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.BEGIN,
        [iproto_key.SYNC] = conn:_next_sync(),
        [iproto_key.STREAM_ID] = 42,
    })
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.ROLLBACK,
        [iproto_key.SYNC] = conn:_next_sync(),
        [iproto_key.STREAM_ID] = 42,
        [9999] = 'foobar',
    }, {
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_call = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.CALL,
        [iproto_key.SYNC] = conn:_next_sync(),
        [9999] = 'foobar',
    }, {
        [iproto_key.FUNCTION_NAME] = 'box.session.uid',
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_eval = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.EVAL,
        [iproto_key.SYNC] = conn:_next_sync(),
        [9999] = 'foobar',
    }, {
        [iproto_key.EXPR] = 'return box.session.uid()',
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_execute = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.EXECUTE,
        [iproto_key.SYNC] = conn:_next_sync(),
        [9999] = 'foobar',
    }, {
        [iproto_key.SQL_TEXT] = 'SELECT 1',
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_watch_once = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.WATCH_ONCE,
        [iproto_key.SYNC] = conn:_next_sync(),
        [9999] = 'foobar',
    }, {
        [iproto_key.EVENT_KEY] = 'some_key',
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_ping = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.PING,
        [iproto_key.SYNC] = conn:_next_sync(),
        [9999] = 'foobar',
    }, {
        [9999] = 'foobar',
    })
    conn:close()
end

g.test_auth = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    test_request(conn, {
        [iproto_key.REQUEST_TYPE] = iproto_type.AUTH,
        [iproto_key.SYNC] = conn:_next_sync(),
        [9999] = 'foobar',
    }, {
        [iproto_key.USER_NAME] = 'guest',
        [iproto_key.TUPLE] = {},
        [9999] = 'foobar',
    })
    conn:close()
end
