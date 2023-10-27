local msgpack = require('msgpack')
local t = require('luatest')
local tarantool = require('tarantool')
local uuid = require('uuid')

local g = t.group()

--
-- Checks exported constant values.
--
g.test_constants = function()
    t.assert_equals(box.iproto.GREETING_SIZE, 128)
    t.assert_equals(box.iproto.GREETING_PROTOCOL_LEN_MAX, 32)
    t.assert_equals(box.iproto.GREETING_SALT_LEN_MAX, 44)
end

--
-- Checks errors raised on invalid arguments passed to
-- box.iproto.encode_greeting() and box.iproto.decode_greeting().
--
g.test_encode_decode_greeting_invalid_args = function()
    local encode = box.iproto.encode_greeting
    local decode = box.iproto.decode_greeting

    local errmsg = 'Usage: box.iproto.encode_greeting({' ..
                   'version = x, uuid = x, salt = x})'
    t.assert_error_msg_equals(errmsg, encode, 123)
    t.assert_error_msg_equals(errmsg, encode, 'foo')
    t.assert_error_msg_equals(errmsg, encode, {}, 123)

    t.assert_error_msg_equals('version must be a string',
                              encode, {version = 123})
    t.assert_error_msg_equals('cannot parse version string',
                              encode, {version = 'foo'})
    t.assert_error_msg_equals('uuid must be a string',
                              encode, {uuid = 123})
    t.assert_error_msg_equals('cannot parse uuid string',
                              encode, {uuid = 'foo'})
    t.assert_error_msg_equals('salt must be a string',
                              encode, {salt = 123})
    t.assert_error_msg_equals('salt string length cannot be greater than 44',
                              encode, {salt = string.rep('x', 45)})

    errmsg = 'Usage: box.iproto.decode_greeting(string)'
    t.assert_error_msg_equals(errmsg, decode, 123)
    t.assert_error_msg_equals(errmsg, decode, {})
    t.assert_error_msg_equals(errmsg, decode, 'foo', 123)

    t.assert_error_msg_equals('greeting length must equal 128', decode, 'foo')
end

--
-- Checks box.iproto.encode_greeting() and box.iproto.decode_greeting() output.
--
g.test_encode_decode_greeting = function()
    local encode = box.iproto.encode_greeting
    local decode = box.iproto.decode_greeting

    local pattern =
        'Tarantool%s+%d+%.%d+%.%d+%s+%(Binary%)%s+' ..
        string.rep('%x', 8) .. '%-' .. string.rep('%x', 4) .. '%-' ..
        string.rep('%x', 4) .. '%-' .. string.rep('%x', 4) .. '%-' ..
        string.rep('%x', 12) .. '%s*\n[%w%p]+%s*$'

    local str = encode()
    t.assert_equals(#str, box.iproto.GREETING_SIZE)
    t.assert_str_matches(str, pattern)

    str = encode({})
    t.assert_equals(#str, box.iproto.GREETING_SIZE)
    t.assert_str_matches(str, pattern)

    local greeting = decode(str)
    t.assert_type(greeting, 'table')
    t.assert_equals(greeting.version, tarantool.version:match('%d%.%d%.%d'))
    t.assert_equals(greeting.protocol, 'Binary')
    t.assert(uuid.fromstr(greeting.uuid))
    t.assert_not_equals(uuid.fromstr(greeting.uuid), uuid.NULL)
    t.assert_type(greeting.salt, 'string')
    t.assert_equals(#greeting.salt, 32)
    t.assert_equals(encode(greeting), str)

    greeting = {
        version = '2.3.4',
        protocol = 'Binary',
        uuid = uuid.str(),
        salt = string.rep('x', 40),
    }
    str = encode(greeting)
    t.assert_equals(#str, box.iproto.GREETING_SIZE)
    t.assert_str_matches(str, pattern)
    t.assert_equals(decode(str), greeting)
end

--
-- Checks errors raised on invalid arguments passed to
-- box.iproto.encode_packet() and box.iproto.decode_packet().
--
g.test_encode_decode_packet_invalid_args = function()
    local encode = box.iproto.encode_packet
    local decode = box.iproto.decode_packet

    local errmsg = 'Usage: box.iproto.encode_packet(header[, body])'
    t.assert_error_msg_equals(errmsg, encode)
    t.assert_error_msg_equals(errmsg, encode, {}, {}, {})

    t.assert_error_msg_equals('header must be a string or a table',
                              encode, 123)
    t.assert_error_msg_equals('body must be a string or a table',
                              encode, {}, 123)
    t.assert_error_msg_equals("unsupported Lua type 'function'",
                              encode, {function() end})
    t.assert_error_msg_equals("unsupported Lua type 'function'",
                              encode, {}, {function() end})

    errmsg = 'Usage: box.iproto.decode_packet(string[, pos])'
    t.assert_error_msg_equals(errmsg, decode)
    t.assert_error_msg_equals(errmsg, decode, {})
    t.assert_error_msg_equals(errmsg, decode, 123)
    t.assert_error_msg_equals(errmsg, decode, '', '1')
    t.assert_error_msg_equals(errmsg, decode, '', 1, '')

    errmsg = 'position must be greater than 0'
    t.assert_error_msg_equals(errmsg, decode, '', 0)
    t.assert_error_msg_equals(errmsg, decode, '', -1)
end

--
-- Checks errors raised by box.iproto.decode_packet() on bad input.
--
g.test_decode_packet_bad_input = function()
    local decode = box.iproto.decode_packet

    local errmsg = 'invalid fixheader'
    t.assert_error_msg_equals(errmsg, decode, string.fromhex('00'))
    t.assert_error_msg_equals(errmsg, decode, string.fromhex('ff'))
    t.assert_error_msg_equals(errmsg, decode, string.fromhex('80'))

    t.assert_error_msg_equals('Invalid MsgPack - illegal code',
                              decode, string.fromhex('01c1'))
    t.assert_error_msg_equals('Invalid MsgPack - truncated input',
                              decode, string.fromhex('0281c0'))
    t.assert_error_msg_equals('Invalid MsgPack - truncated input',
                              decode, string.fromhex('0281c0c0'))
    t.assert_error_msg_equals('Invalid MsgPack - truncated input',
                              decode, string.fromhex('0581c0c081c0'))
    t.assert_error_msg_equals('Invalid MsgPack - truncated input',
                              decode, string.fromhex('0581c0c081c0c0'))
    t.assert_error_msg_equals('Invalid MsgPack - junk after input',
                              decode, string.fromhex('0781c0c081c0c0c0'))
end

--
-- Checks output of box.iproto.decode_packet() on truncated input.
--
g.test_decode_packet_truncated_input = function()
    local decode = box.iproto.decode_packet

    t.assert_equals({decode('')}, {nil, 1})
    t.assert_equals({decode(string.fromhex('ce'))}, {nil, 4})
    t.assert_equals({decode(string.fromhex('ce0000'))}, {nil, 2})
    t.assert_equals({decode(string.fromhex('05'))}, {nil, 5})
    t.assert_equals({decode(string.fromhex('ce00000005'))}, {nil, 5})
    t.assert_equals({decode(string.fromhex('ce0000000581'))}, {nil, 4})
end

--
-- Checks box.iproto.encode_packet() and box.iproto.decode_packet() output
-- on input containing a single packet.
--
g.test_encode_decode_packet_one = function()
    local encode = box.iproto.encode_packet
    local decode = box.iproto.decode_packet

    local data = encode({
        sync = 123,
        request_type = box.iproto.type.INSERT,
    }, {
        space_id = 512,
        tuple = {1, 2, 3},
    })
    t.assert_equals(string.hex(data),
                    'ce0000000f820002017b8210cd02002193010203')
    local header, body, pos = decode(data)
    t.assert(msgpack.is_object(header))
    t.assert_equals(header:decode(), {
        [box.iproto.key.SYNC] = 123,
        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.INSERT,
    })
    t.assert_equals(header.sync, 123)
    t.assert_equals(header.request_type, box.iproto.type.INSERT)
    t.assert(msgpack.is_object(body))
    t.assert_equals(body:decode(), {
        [box.iproto.key.SPACE_ID] = 512,
        [box.iproto.key.TUPLE] = {1, 2, 3},
    })
    t.assert_equals(body.space_id, 512)
    t.assert_equals(body.tuple, {1, 2, 3})
    t.assert_equals(pos, #data + 1)

    data = encode({
        sync = 123,
        request_type = box.iproto.type.NOP,
    })
    t.assert_equals(string.hex(data), 'ce0000000582000c017b')
    header, body, pos = decode(data)
    t.assert(msgpack.is_object(header))
    t.assert_equals(header:decode(), {
        [box.iproto.key.SYNC] = 123,
        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.NOP,
    })
    t.assert_equals(header.sync, 123)
    t.assert_equals(header.request_type, box.iproto.type.NOP)
    t.assert_is(body, nil)
    t.assert_equals(pos, #data + 1)
end

--
-- Checks box.iproto.encode_packet() and box.iproto.decode_packet() output
-- on input containing multiple packets.
--
g.test_encode_decode_packet_many = function()
    local encode = box.iproto.encode_packet
    local decode = box.iproto.decode_packet

    local data = encode({
        sync = 1,
        request_type = box.iproto.type.INSERT,
    }, {
        space_id = 512,
        tuple = {'a', 'b', 'c'},
    }) .. encode({
        sync = 2,
        request_type = box.iproto.type.NOP,
    }) .. encode({
        sync = 3,
        request_type = box.iproto.type.REPLACE,
    }, {
        space_name = 'test',
        tuple = {1, 2, 3},
    })

    local header, body, pos = decode(data)
    t.assert(msgpack.is_object(header))
    t.assert_equals(header:decode(), {
        [box.iproto.key.SYNC] = 1,
        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.INSERT,
    })
    t.assert(msgpack.is_object(body))
    t.assert_equals(body:decode(), {
        [box.iproto.key.SPACE_ID] = 512,
        [box.iproto.key.TUPLE] = {'a', 'b', 'c'},
    })

    header, body, pos = decode(data, pos)
    t.assert(msgpack.is_object(header))
    t.assert_equals(header:decode(), {
        [box.iproto.key.SYNC] = 2,
        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.NOP,
    })
    t.assert_is(body, nil)

    header, body, pos = decode(data, pos)
    t.assert(msgpack.is_object(header))
    t.assert_equals(header:decode(), {
        [box.iproto.key.SYNC] = 3,
        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.REPLACE,
    })
    t.assert(msgpack.is_object(body))
    t.assert_equals(body:decode(), {
        [box.iproto.key.SPACE_NAME] = 'test',
        [box.iproto.key.TUPLE] = {1, 2, 3},
    })

    t.assert_equals(pos, #data + 1)
    t.assert_equals({decode(data, pos)}, {nil, 1})
end

--
-- Checks box.iproto.encode_packet() output on binary input.
--
g.test_encode_packet_bin = function()
    local encode = box.iproto.encode_packet

    local data = encode(string.fromhex('820002017b'),
                        string.fromhex('8210cd02002193010203'))
    t.assert_equals(string.hex(data),
                    'ce0000000f820002017b8210cd02002193010203')
    data = encode(string.fromhex('82000c017b'))
    t.assert_equals(string.hex(data), 'ce0000000582000c017b')

    -- box.iproto.encode_packet() doesn't check binary input.
    data = encode(string.fromhex('c1'), string.fromhex('82'))
    t.assert_equals(string.hex(data), 'ce00000002c182')
end
