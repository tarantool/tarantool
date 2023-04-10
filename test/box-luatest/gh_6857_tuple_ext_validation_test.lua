local t = require('luatest')
local cluster = require('luatest.replica_set')

local g = t.group('gh-6857-tuple-ext-validation')

local net_box = require('net.box')
local msgpack = require('msgpack')
local fiber = require('fiber')

local decimal = require('decimal')
local uuid = require('uuid')
local datetime = require('datetime')

g.before_all(function()
    g.cluster = cluster:new({})
    local box_cfg = {
        log_level = 6,
    }
    g.server = g.cluster:build_and_add_server({
        alias='default',
        box_cfg = box_cfg,
    })
    g.cluster:start()
    g.server:eval('function test(val) return true end')
end)

g.after_all(function()
    g.cluster:drop()
end)

-- Iproto keys to encode custom call request.
local IPROTO_REQUEST_TYPE = 0x00
local IPROTO_SYNC = 0x01
local IPROTO_TUPLE = 0x21
local IPROTO_FUNCTION_NAME = 0x22

local IPROTO_CALL = 0x0a

local function inject_call(conn, raw_val)
    local header = msgpack.encode({
        [IPROTO_REQUEST_TYPE] = IPROTO_CALL,
        [IPROTO_SYNC] = conn:_next_sync(),
    })
    local body = msgpack.encode({
        [IPROTO_FUNCTION_NAME] = 'test',
        -- Reserve a slot for raw_val.
        [IPROTO_TUPLE] = {box.NULL},
    })
    -- Replace the nil placeholder with val.
    body = string.sub(body, 1, -2)..raw_val
    local size = msgpack.encode(#header + #body)
    local request = size..header..body
    return conn:_inject(request)
end

-- Check extension types in tuple handling: previously mp_check() simply
-- iterated over tuple field headers without looking into the field contents.
-- This was ok for plain types, like MP_STR, MP_INT and others, because any
-- msgpack encoded string of any of these types represents a valid value.
-- This isn't true for extension types, however. Types like decimal, uuid and
-- others have complex internal structure, which requires additional validation.
g.test_extension_tuple_validation = function()
    local correct_data = {
        ['decimal'] = msgpack.encode(decimal.new(fiber.time())),
        ['uuid'] = msgpack.encode(uuid.new()),
        ['error'] =
            msgpack.encode(box.error.new(box.error.UNSUPPORTED, 'a', 'b')),
        ['datetime'] = msgpack.encode(datetime.now()),
        ['interval'] = msgpack.encode(datetime.now() - datetime.new()),
    }
    local test_data = {
        ['decimal #1'] = string.fromhex('d5010010'), -- bad last nibble 0x0
        ['decimal #2'] = string.fromhex('d401cc'), -- bad scaling (unsigned)
        ['decimal #3'] = string.fromhex('d401d0'), -- bad scaling (signed)
        ['uuid #1'] = string.fromhex('d702c93f2d79bca74235'), -- bad length
        ['error #1'] = string.fromhex('c708038100918600ab436c'), -- bad length
        ['error #2'] = string.fromhex('d5030000'), -- bad length
        ['datetime #1'] = string.fromhex('c7070400000000000000'), -- bad length
        ['interval #1'] = string.fromhex('c7050602060fff01'), -- bad key 0xff
        ['interval #2'] = string.fromhex('d40601'), -- bad length
        ['interval #3'] = string.fromhex('d5060000'), -- bad length
        ['interval #4'] = string.fromhex('d5060100'), -- bad length
        ['interval #5'] = string.fromhex('d6060100c000'), -- bad value type
        ['interval #6'] =
            string.fromhex('d6060100cd00'), -- bad value (unsigned)
        ['interval #7'] = string.fromhex('d6060100d100'), -- bad value (signed)
    }

    local c = net_box.connect(g.cluster.servers[1].net_box_uri)
    t.assert_equals(c.state, 'active', 'Connection established')

    -- First check that there are no complaints on correctly-encoded ext types.
    for name, val in pairs(correct_data) do
        local ok = pcall(inject_call, c, val)
        t.assert_equals(ok, true, ('No error with %s argument'):format(name))
    end
    -- Now check that badly encoded ext types are detected.
    for name, val in pairs(test_data) do
        local ok, err = pcall(inject_call, c, val)
        t.assert(not ok, 'Error discovered')
        t.assert_equals(err.message, 'Invalid MsgPack - packet body',
                        ('Malformed %s argument discovered'):format(name))
    end
    c:close()
end
