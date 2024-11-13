local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function(net_box_uri)
        rawset(_G, 'iproto_insert_arrow', function(hex_body)
            local uri = require('uri')
            local socket = require('socket')
            -- Connect to the server.
            local u = uri.parse(net_box_uri)
            local s = socket.tcp_connect(u.host, u.service)
            local greeting = s:read(box.iproto.GREETING_SIZE)
            greeting = box.iproto.decode_greeting(greeting)
            t.assert_covers(greeting, {protocol = 'Binary'})
            -- Send the request.
            local request = box.iproto.encode_packet(
                {request_type = box.iproto.type.INSERT_ARROW, sync = 123},
                -- Ignore whitespaces.
                string.fromhex(hex_body:gsub("%s+", ""))
            )
            t.assert_equals(s:write(request), #request)
            -- Read the response.
            local response = ''
            local header, body
            repeat
                header, body = box.iproto.decode_packet(response)
                if header == nil then
                    local size = body
                    local data = s:read(size)
                    t.assert_is_not(data)
                    response = response .. data
                end
            until header ~= nil
            s:close()
            return body
        end)
    end, {cg.server.net_box_uri})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

-- Test various invalid requests.
g.test_iproto_insert_arrow_invalid = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        -- <MP_MAP> {}
        local r = _G.iproto_insert_arrow('80')
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        "Missing mandatory field 'SPACE_ID' in request")
        -- <MP_MAP> {
        --     IPROTO_SPACE_ID: 512}
        r = _G.iproto_insert_arrow('8110cd0200')
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        "Missing mandatory field 'ARROW' in request")
        -- <MP_MAP> {
        --     IPROTO_SPACE_ID: 512,
        --     IPROTO_ARROW: <MP_NIL> null}
        r = _G.iproto_insert_arrow('8210cd020036c0')
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        'Invalid MsgPack - packet body')
        -- <MP_MAP> {
        --     IPROTO_SPACE_ID: 512,
        --     IPROTO_ARROW: <MP_EXT> {
        --         type: MP_UNKNOWN_EXTENSION,
        --         size: 0}}
        r = _G.iproto_insert_arrow('8210cd020036c70000')
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        'Invalid MsgPack - packet body')
        -- <MP_MAP> {
        --     IPROTO_SPACE_ID: 512,
        --     IPROTO_ARROW: <MP_EXT> {
        --         type: MP_ARROW,
        --         size: 0}}
        r = _G.iproto_insert_arrow('8210cd020036c70008')
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        'Invalid MsgPack - packet body')
        t.assert_equals(r[box.iproto.key.ERROR][0][2][3],
                        'Invalid MsgPack - invalid extension')
        t.assert_equals(r[box.iproto.key.ERROR][0][3][3],
                        'Invalid MsgPack - cannot unpack arrow data')
        t.assert_equals(r[box.iproto.key.ERROR][0][4][3],
                        'Arrow decode error: unexpected data size')
        -- <MP_MAP> {
        --     IPROTO_SPACE_ID: 512,
        --     IPROTO_ARROW: <MP_EXT> {
        --         type: MP_ARROW,
        --         size: 4,
        --         data: [0xde, 0xad, 0xbe, 0xef]}}
        r = _G.iproto_insert_arrow('8210cd020036c70408deadbeef')
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        'Invalid MsgPack - packet body')
        t.assert_equals(r[box.iproto.key.ERROR][0][2][3],
                        'Invalid MsgPack - invalid extension')
        t.assert_equals(r[box.iproto.key.ERROR][0][3][3],
                        'Invalid MsgPack - cannot unpack arrow data')
        t.assert_equals(r[box.iproto.key.ERROR][0][4][3],
                        'Arrow decode error: ' ..
                        'Expected at least 8 bytes in remainder of stream')

        -- Correct Schema, but Array is missing.
        r = _G.iproto_insert_arrow([[
            8210cd020036c8007c08ffffffff70000000040000009effffff0400010004000000
            b6ffffff0c00000004000000000000000100000004000000daffffff140000000202
            000004000000f0ffffff4000000001000000610000000600080004000c0010000400
            080009000c000c000c0000000400000008000a000c00040006000800ffffffff]])
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        'Invalid MsgPack - packet body')
        t.assert_equals(r[box.iproto.key.ERROR][0][2][3],
                        'Invalid MsgPack - invalid extension')
        t.assert_equals(r[box.iproto.key.ERROR][0][3][3],
                        'Invalid MsgPack - cannot unpack arrow data')
        t.assert_equals(r[box.iproto.key.ERROR][0][4][3],
                        'Arrow decode error: ' ..
                        'Expected at least 8 bytes in remainder of stream')

        -- A valid request, but memtx does not support arrow format.
        r = _G.iproto_insert_arrow([[
            8210cd020036c8011008ffffffff70000000040000009effffff0400010004000000
            b6ffffff0c00000004000000000000000100000004000000daffffff140000000202
            000004000000f0ffffff4000000001000000610000000600080004000c0010000400
            080009000c000c000c0000000400000008000a000c00040006000800ffffffff8800
            0000040000008affffff0400030010000000080000000000000000000000acffffff
            01000000000000003400000008000000000000000200000000000000000000000000
            00000000000000000000000000000800000000000000000000000100000001000000
            0000000000000000000000000a00140004000c0010000c0014000400060008000c00
            00000000000000000000]])
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        'memtx does not support arrow format')
    end)
end
