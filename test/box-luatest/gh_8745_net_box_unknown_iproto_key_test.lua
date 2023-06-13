local msgpack = require('msgpack')
local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

--
-- The test checks that unknown IPROTO keys are silently ignored by both
-- net.box client and IPROTO server.
--
local g = t.group()

local iproto_key = {
    REQUEST_TYPE = 0x00,
    SYNC = 0x01,
    STREAM_ID = 0x0a,
    SPACE_ID = 0x10,
    LIMIT = 0x12,
    KEY = 0x20,
    TUPLE = 0x21,
    FUNCTION_NAME = 0x22,
    USER_NAME = 0x23,
    EXPR = 0x27,
    SQL_TEXT = 0x40,
}
local iproto_type = {
    SELECT = 1,
    REPLACE = 3,
    DELETE = 5,
    AUTH = 7,
    EVAL = 8,
    CALL = 10,
    EXECUTE = 11,
    BEGIN = 14,
    COMMIT = 15,
    ROLLBACK = 16,
    PING = 64,
}

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
