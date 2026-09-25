local msgpack = require('msgpack')
local netbox = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Sends a raw EXECUTE request with the given pre-encoded body and returns the
-- result of net.box's _inject (raises on a server-side error).
local function inject_execute(c, body)
    local header = msgpack.encode({
        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.EXECUTE,
        [box.iproto.key.SYNC] = c:_next_sync(),
    })
    local size = msgpack.encode(#header + #body)
    return c:_inject(size .. header .. body)
end

-- Raw msgpack map values.
local MAP1 = string.fromhex('81')
local MAP2 = string.fromhex('82')

-- A multibyte encoding must be decoded correctly.
g.test_non_optimal_key_encoding = function(cg)
    local c = netbox:connect(cg.server.net_box_uri)

    -- Multibyte IPROTO_SQL_TEXT (0x40).
    local body = MAP1 .. string.fromhex('cc40') .. msgpack.encode('SELECT 1')
    t.assert_equals(inject_execute(c, body), {{1}})


    -- Multibyte unknown key (0x40).
    body = MAP2 .. string.fromhex('cc7f') .. msgpack.encode('ignored') ..
                 msgpack.encode(box.iproto.key.SQL_TEXT) ..
                 msgpack.encode('SELECT 1')
    t.assert_equals(inject_execute(c, body), {{1}})

    c:close()
end

g.test_wrong_iproto_value_type = function(cg)
    local c = netbox:connect(cg.server.net_box_uri)
    local err_msg = 'Invalid MsgPack - packet body'

    local body = MAP1 .. msgpack.encode(box.iproto.key.SQL_TEXT) ..
                 msgpack.encode(42)
    t.assert_error_msg_content_equals(err_msg, inject_execute, c, body)

    body = MAP1 .. msgpack.encode(box.iproto.key.STMT_ID) ..
           msgpack.encode('not a number')
    t.assert_error_msg_content_equals(err_msg, inject_execute, c, body)

    body = MAP2 .. msgpack.encode(box.iproto.key.SQL_TEXT) ..
           msgpack.encode('SELECT 1') ..
           msgpack.encode(box.iproto.key.SQL_BIND) ..
           msgpack.encode(setmetatable({}, {__serialize = 'map'}))
    t.assert_error_msg_content_equals(err_msg, inject_execute, c, body)

    c:close()
end
