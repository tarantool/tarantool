local buffer = require('buffer')
local console = require('console')
local msgpack = require('msgpack')
local ffi = require('ffi')
local fun = require('fun')
local t = require('luatest')

local g = t.group()

g.test_errors = function()
    -- Encode/decode error messages
    local buf = buffer.ibuf()
    t.assert_error_msg_content_equals("msgpack.encode: a Lua object expected",
        function() msgpack.encode() end)
    t.assert_error_msg_content_equals(
        "msgpack.encode: argument 2 must be of type 'struct ibuf'",
        function() msgpack.encode('test', 'str') end)
    t.assert_error_msg_content_equals(
        "msgpack.encode: argument 2 must be of type 'struct ibuf'",
        function() msgpack.encode('test', buf.buf) end)

    t.assert_error_msg_content_equals(
        "msgpack.decode: a Lua string or 'char *' expected",
        function() msgpack.decode() end)
    t.assert_error_msg_content_equals(
        "msgpack.decode: a Lua string or 'char *' expected",
        function() msgpack.decode(123) end)
    t.assert_error_msg_content_equals(
        "msgpack.decode: a Lua string or 'char *' expected",
        function() msgpack.decode(buf) end)

    t.assert_error_msg_content_equals(
        "bad argument #2 to 'decode' (number expected, got string)",
        function() msgpack.decode(buf.buf, 'size') end)
    t.assert_error_msg_content_equals(
        "msgpack.decode: offset is out of bounds",
        function() msgpack.decode('test', 0) end)
    t.assert_error_msg_content_equals(
        "msgpack.decode: offset is out of bounds",
        function() msgpack.decode('test', 5) end)

    t.assert_error_msg_content_equals(
        "bad argument #2 to 'decode' (number expected, got string)",
        function() msgpack.decode('test', 'offset') end)
    t.assert_error_msg_content_equals(
        "msgpack.decode: a Lua string or 'char *' expected",
        function() msgpack.decode_unchecked() end)
    t.assert_error_msg_content_equals(
        "msgpack.decode: a Lua string or 'char *' expected",
        function() msgpack.decode_unchecked(123) end)
    t.assert_error_msg_content_equals(
        "msgpack.decode: a Lua string or 'char *' expected",
        function() msgpack.decode_unchecked(buf) end)

    t.assert_error_msg_content_equals(
        "msgpack.decode: offset is out of bounds",
        function() msgpack.decode_unchecked('test', 0) end)
    t.assert_error_msg_content_equals(
        "msgpack.decode: offset is out of bounds",
        function() msgpack.decode_unchecked('test', 5) end)
    t.assert_error_msg_content_equals(
        "bad argument #2 to 'decode_unchecked' (number expected, got string)",
        function() msgpack.decode_unchecked('test', 'offset') end)

    t.assert_error_msg_content_equals(
        "msgpack.object: a Lua object expected",
        function() msgpack.object() end)
    t.assert_error_msg_content_equals(
        "msgpack.object_from_raw: a Lua string or 'char *' expected",
        function() msgpack.object_from_raw() end)
    t.assert_error_msg_content_equals(
        "bad argument #2 to 'object_from_raw' (number expected, got string)",
        function() msgpack.object_from_raw(buf.buf, 'test') end)
end

g.test_encode_decode_strings = function()
    -- Encode/decode strings.
    local first_table = {1, 2, 3}
    local second_table = {4, 5, 6}
    local s = msgpack.encode(first_table) .. msgpack.encode(second_table)
    local obj, offset = msgpack.decode(s)

    t.assert_equals(obj, first_table)
    obj, offset = msgpack.decode(s, offset)
    t.assert_equals(obj, second_table)
    t.assert_equals(offset, #s + 1)

    obj, offset = msgpack.decode_unchecked(s)
    t.assert_equals(obj, first_table)
    obj, offset = msgpack.decode_unchecked(s, offset)
    t.assert_equals(obj, second_table)
    t.assert_equals(offset, #s + 1)
end

g.test_encode_decode_buffer = function()
    -- Encode/decode a buffer.
    local first_buffer = {1, 2, 3}
    local second_buffer = {4, 5, 6}
    local buf = buffer.ibuf()
    local len = msgpack.encode(first_buffer, buf)
    len = msgpack.encode(second_buffer, buf) + len
    t.assert_equals(buf:size(), len)

    local orig_rpos = buf.rpos
    local obj, rpos = msgpack.decode(buf.rpos, buf:size())
    t.assert_equals(obj, first_buffer)

    buf.rpos = rpos
    obj, rpos = msgpack.decode(buf.rpos, buf:size())
    t.assert_equals(obj, second_buffer)

    buf.rpos = rpos
    t.assert_equals(buf:size(), 0)

    buf.rpos = orig_rpos
    obj, rpos = msgpack.decode_unchecked(buf.rpos, buf:size())
    t.assert_equals(obj, first_buffer)

    buf.rpos = rpos
    obj, rpos = msgpack.decode_unchecked(buf.rpos, buf:size())
    t.assert_equals(obj, second_buffer)

    buf.rpos = rpos
    t.assert_equals(buf:size(), 0)
end

g.test_invalid_msgpack = function()
    local err = "Invalid MsgPack - truncated input"

    -- Invalid msgpack.
    local first_buffer = {1, 2, 3}
    local s = msgpack.encode(first_buffer)
    s = s:sub(1, -2)
    t.assert_error_msg_content_equals(err, msgpack.decode, s)

    local buf = buffer.ibuf()
    t.assert_equals(msgpack.encode(first_buffer, buf), 4)
    t.assert_error_msg_content_equals(err, msgpack.decode,
                                      buf.rpos, buf:size() - 1)

    -- 0xc1 cannot be used in a valid MsgPack.
    err = "Invalid MsgPack - illegal code"
    t.assert_error_msg_content_equals(err, msgpack.decode, '\xc1')
    t.assert_error_msg_content_equals(err, msgpack.decode, '\x91\xc1')
    t.assert_error_msg_content_equals(err, msgpack.decode, '\x81\xff\xc1')
    t.assert_error_msg_content_equals(err, msgpack.decode, '\x93\xff\xc1\xff')
end

g.test_encode_decode_struct_buffer = function()
    -- Provide a buffer. Try both 'struct ibuf' and 'struct ibuf *'.
    local buf_storage = buffer.ibuf()
    local buf = ffi.cast('struct ibuf *', buf_storage)
    local size = msgpack.encode({a = 1, b = 2}, buf)
    t.assert_equals((msgpack.decode(buf.rpos, size)), {a = 1, b = 2})

    buf = buffer.ibuf()
    size = msgpack.encode({c = 3, d = 4}, buf)
    t.assert_equals((msgpack.decode(buf.rpos, size)), {c = 3, d = 4})
end

g.test_encode_decode_char_buffer = function()
    -- Decode should accept both 'char *' and 'const char *'.
    local buf = buffer.ibuf()
    local elem = 100
    local size = msgpack.encode(elem, buf)
    t.assert_equals((msgpack.decode(ffi.cast('char *', buf.rpos), size)), elem)
    t.assert_equals(
        (msgpack.decode(ffi.cast('const char *', buf.rpos), size)), elem)
end

g.test_size_not_negative = function()
    -- gh-4224: msgpack.decode(cdata, size) should check, that size
    -- is not negative.
    t.assert_error_msg_content_equals(
        "msgpack.decode: size can't be negative",
        function() msgpack.decode(ffi.cast('char *', '\x04\x05\x06'), -1) end)
end

g.test_encode_decode_decimals = function()
    -- gh-4333: msgpack encode/decode decimals.
    local decimal = require('decimal')
    local a = decimal.new('1e37')
    local b = decimal.new('1e-38')
    local c = decimal.new('1')
    local d = decimal.new('0.1234567')
    local e = decimal.new('123.4567')
    t.assert_equals(msgpack.decode(msgpack.encode(a)), a)
    t.assert_equals(msgpack.decode(msgpack.encode(b)), b)
    t.assert_equals(msgpack.decode(msgpack.encode(c)), c)
    t.assert_equals(msgpack.decode(msgpack.encode(d)), d)
    t.assert_equals(msgpack.decode(msgpack.encode(e)), e)
end

g.test_encode_decode_uuid = function()
    -- encode/decode uuid
    local uuid = require('uuid')
    for _ = 1, 10 do
        local src = uuid.new()
        local res = msgpack.decode(msgpack.encode(src))
        t.assert_equals(src, res)
    end
end

g.test_object_encode_decode = function()
    local o = nil
    t.assert_equals(msgpack.object(o):decode(), o)
    o = box.NULL
    t.assert_equals(msgpack.object(o):decode(), o)
    o = 123
    t.assert_equals(msgpack.object(o):decode(), o)
    o = 'foobar'
    t.assert_equals(msgpack.object(o):decode(), o)
    o = {foo = 123, bar = 456}
    t.assert_equals(msgpack.object(o):decode(), o)
    o = {foo = {1, 2, 3}, bar = {'a', 'b', 'c'}}
    t.assert_equals(msgpack.object(o):decode(), o)
    local mp = msgpack.object({bar = 123})
    t.assert_equals(
        msgpack.object({mp, {foo = mp}}):decode(),
        {mp:decode(), {foo = mp:decode()}})
    t.assert_equals(
        msgpack.decode(msgpack.encode({mp, {foo = mp}})),
        {mp:decode(), {foo = mp:decode()}})
    t.assert_equals(msgpack.object(box.tuple.new{1, 2, 3}):decode(), {1, 2, 3})
    t.assert_equals(
        msgpack.object({
            foo = box.tuple.new(123),
            bar = box.tuple.new(456),
        }):decode(), {foo = {123}, bar = {456}})
end

g.test_object_from_raw = function()
    local o = {foo = 'bar'}
    local mp

    -- from string
    mp = msgpack.object_from_raw(msgpack.encode(o))
    t.assert_equals(mp:decode(), o)

    -- from buffer
    local buf = buffer.ibuf()
    msgpack.encode(o, buf)
    mp = msgpack.object_from_raw(buf.buf, buf:size())
    t.assert_equals(mp:decode(), o)
    buf:recycle()

    -- invalid msgpack
    local s = msgpack.encode(o)
    t.assert_error_msg_content_equals(
        "Invalid MsgPack - truncated input",
        msgpack.object_from_raw, s:sub(1, -2))
    t.assert_error_msg_content_equals(
        "Invalid MsgPack - junk after input",
        msgpack.object_from_raw, s .. s)
end

g.test_object_iterator = function()
    local o = {foo = {1, 2, 3}, bar = {'a', 'b', 'c'}}
    local it

    -- iterator.decode
    it = msgpack.object(o):iterator()
    t.assert_equals(it:decode(), o)
    t.assert_error_msg_content_equals(
        "iteration ended", function() it:decode() end)

    -- iterator.decode for msgpack.object_from_raw
    it = msgpack.object_from_raw(msgpack.encode(o)):iterator()
    t.assert_equals(it:decode(), o)
    t.assert_error_msg_content_equals(
        "iteration ended", function() it:decode() end)

    -- iterator.take
    it = msgpack.object(o):iterator()
    t.assert_equals(it:take():decode(), o)
    t.assert_error_msg_content_equals(
        "iteration ended", function() it:take() end)

    -- iterator.decode_array_header
    it = msgpack.object({{}}):iterator()
    t.assert_error_msg_content_equals(
        "unexpected msgpack type", function() it:decode_map_header() end)
    t.assert_equals(it:decode_array_header(), 1)
    t.assert_error_msg_content_equals(
        "unexpected msgpack type", function() it:decode_map_header() end)
    t.assert_equals(it:decode_array_header(), 0)
    t.assert_error_msg_content_equals(
        "iteration ended", function() it:decode_map_header() end)

    -- iterator.decode_map_header
    it = msgpack.object(
        {key = setmetatable({}, {__serialize = 'map'})}):iterator()
    t.assert_error_msg_content_equals(
        "unexpected msgpack type", function() it:decode_array_header() end)
    t.assert_equals(it:decode_map_header(), 1)
    t.assert_equals(it:decode(), 'key')
    t.assert_error_msg_content_equals(
        "unexpected msgpack type", function() it:decode_array_header() end)
    t.assert_equals(it:decode_map_header(), 0)
    t.assert_error_msg_content_equals(
        "iteration ended", function() it:decode_map_header() end)

    -- iterator.skip
    it = msgpack.object(
        {'foo', {'bar'}, {foo = 'bar'}, 'fuzz', 'buzz'}):iterator()
    t.assert_equals(it:decode_array_header(), 5)
    it:skip()
    it:skip()
    it:skip()
    t.assert_equals(it:decode(), 'fuzz')
    it:skip()
    t.assert_error_msg_content_equals(
        "iteration ended", function() it:skip() end)

    -- iterator.take.iterator
    it = msgpack.object({{foo = 123}, {bar = 456}}):iterator()
    t.assert_equals(it:decode_array_header(), 2)
    local it2 = it:take():iterator()
    t.assert_equals(it:decode_map_header(), 1)
    t.assert_equals(it:decode(), 'bar')
    t.assert_equals(it:decode(), 456)
    t.assert_error_msg_content_equals(
        "iteration ended", function() it:decode() end)
    t.assert_equals(it2:decode_map_header(), 1)
    t.assert_equals(it2:decode(), 'foo')
    t.assert_equals(it2:decode(), 123)
    t.assert_error_msg_content_equals(
        "iteration ended", function() it2:decode() end)

    -- iterator.take_array
    it = msgpack.object({1, 2, 3}):iterator()
    t.assert_error_msg_content_equals("iteration ended", it.take_array, it, 2)
    t.assert_equals(it:take_array(0):decode(), {})
    t.assert_equals(it:take_array(1):decode(), {{1, 2, 3}})
    t.assert_equals(it:take_array(0):decode(), {})
    t.assert_error_msg_content_equals("iteration ended", it.take_array, it, 1)

    it = msgpack.object({10, 20, {30, 40}, 50, {60, 70, 80}, 90}):iterator()
    t.assert_equals(it:decode_array_header(), 6)
    t.assert_equals(it:decode(), 10)
    t.assert_equals(it:take_array(3):decode(), {20, {30, 40}, 50})
    t.assert_equals(it:decode_array_header(), 3)
    t.assert_equals(it:decode(), 60)
    t.assert_equals(it:take_array(3):decode(), {70, 80, 90})
    t.assert_error_msg_content_equals("iteration ended", it.take, it)

    it = msgpack.object({foo = 123}):iterator()
    t.assert_equals(it:decode_map_header(), 1)
    t.assert_equals(it:take_array(2):decode(), {'foo', 123})
    t.assert_error_msg_content_equals("iteration ended", it.take_array, it, 1)
end

g.test_object_cfg = function()
    local inf = 1 / 0
    local serializer = msgpack.new()
    serializer.cfg({
        encode_invalid_numbers = true,
        decode_invalid_numbers = true,
    })
    local mp = serializer.object(inf)
    t.assert_equals(mp:decode(), inf)
    local mp_from_raw = serializer.object_from_raw(msgpack.encode(inf))
    t.assert_equals(mp_from_raw:decode(), inf)
    serializer.cfg({
        encode_invalid_numbers = false,
        decode_invalid_numbers = false,
    })
    t.assert_error_msg_content_equals(
        "number must not be NaN or Inf",
        function() serializer.object(inf) end)
    t.assert_error_msg_content_equals(
        "number must not be NaN or Inf",
        function() mp:decode() end)
    t.assert_error_msg_content_equals(
        "number must not be NaN or Inf",
        function() mp_from_raw:decode() end)
    t.assert_error_msg_content_equals(
        "number must not be NaN or Inf",
        function() mp:iterator():decode() end)
    t.assert_error_msg_content_equals(
        "number must not be NaN or Inf",
        function() mp:iterator():take():decode() end)
end

g.test_object_gc = function()
    local function gc()
        for _ = 1, 5 do
            collectgarbage('collect')
        end
    end
    local weak = setmetatable({}, {__mode = 'v'})
    local o = {{foo = 1}, {bar = 2}}
    local mp, mp_from_raw, mp_from_it, it

    -- iterator pins object
    mp = msgpack.new().object(o)
    it = mp:iterator()
    weak.mp = mp
    mp = nil -- luacheck: no unused
    gc()
    t.assert_not_equals(weak.mp, nil)
    t.assert_equals(it:decode(), o)
    it = nil -- luacheck: no unused
    gc()
    t.assert_equals(weak.mp, nil)

    -- iterator pins object created from raw msgpack
    mp_from_raw = msgpack.new().object_from_raw(msgpack.encode(o))
    it = mp_from_raw:iterator()
    weak.mp_from_raw = mp_from_raw
    mp_from_raw = nil -- luacheck: no unused
    gc()
    t.assert_not_equals(weak.mp_from_raw, nil)
    t.assert_equals(it:decode(), o)
    it = nil -- luacheck: no unused
    gc()
    t.assert_equals(weak.mp_from_raw, nil)

    -- object created by iterator pins original object, but not iterator
    mp = msgpack.new().object(o)
    it = mp:iterator()
    mp_from_it = it:take()
    weak.mp = mp
    weak.it = it
    mp = nil -- luacheck: no unused
    it = nil -- luacheck: no unused
    gc()
    t.assert_not_equals(weak.mp, nil)
    t.assert_equals(weak.it, nil)
    t.assert_equals(mp_from_it:decode(), o)
    mp_from_it = nil -- luacheck: no unused
    gc()
    t.assert_equals(weak.mp, nil)
end

g.test_object_misc = function()
    -- tostring
    local mp = msgpack.object(nil)
    t.assert_equals(tostring(mp), "msgpack.object")
    local mp_from_raw = msgpack.object_from_raw(msgpack.encode(nil))
    t.assert_equals(tostring(mp), "msgpack.object")
    local it = mp:iterator()
    t.assert_equals(tostring(it), "msgpack.iterator")

    -- msgpack.is_object
    t.assert(msgpack.is_object(mp))
    t.assert(msgpack.is_object(mp_from_raw))
    t.assert_not(msgpack.is_object())
    t.assert_not(msgpack.is_object(it))
    t.assert_not(msgpack.is_object({mp}))
end

local test_array = {true, 1, '2', -2, 3.0, {1, 2, 3}, {a = 1, b = 2, c = 3},
                    'decode', 'iterator', 'get'}
local test_map = {a = nil, [1] = true, ['1'] = 1, b = '2', [-2] = 3.0,
                  ['-2'] = 4, [2.5] = 5, [true] = 6, c = {a = 7, b = 8, c = 9},
                  decode = 10, iterator = 11, get = 12}

-- Checks that `msgpack.object:get` method works correctly.
g.test_object_get = function()
    local mp = msgpack.object(test_array)
    for i, v in ipairs(test_array) do
        t.assert_equals(mp:get(i), v)
    end

    mp = msgpack.object(test_map)
    for k, v in pairs(test_map) do
        t.assert_equals(mp:get(k), v)
    end

    -- Checks that unsigned key is compared correctly with MP_INT.
    mp = msgpack.object_from_raw('\x81\xd0\x01\xc3')
    t.assert(mp:get(1))

    mp = msgpack.object(1)
    t.assert_error_msg_content_equals('not an array or map',
                                      function() return mp:get(1)  end)
    t.assert_error_msg_content_equals('not an array or map',
                                      function() return mp:get('1')  end)
    t.assert_error_msg_content_equals('not an array or map',
                                      function() return mp:get('a')  end)

    local array = {1}
    local mp_array = msgpack.object(array)
    local map = {a = 1, [1] = 2, ['2'] = 3, [-3] = 4}
    local mp_map = msgpack.object(map)

    t.assert_equals(mp_array:get(nil), nil)
    t.assert_equals(mp_map:get(nil), nil)
    t.assert_equals(mp_array:get(1.1), nil)
    t.assert_equals(mp_map:get(1.1), nil)

    t.assert_equals(mp_array:get(0), nil)
    t.assert_equals(mp_array:get(2), nil)
    t.assert_equals(mp_array:get(-1), nil)

    -- Checks that keys of different lengths are compared correctly.
    t.assert_equals(mp_map:get('aa'), nil)

    -- Checks that empty string key is handled correctly.
    t.assert_equals(mp_map:get(''), nil)

    t.assert_equals(mp_array:get('a'), nil)

    t.assert_equals(mp_map:get('b'), nil)
    t.assert_equals(mp_map:get('1'), nil)
    t.assert_equals(mp_map:get(2), nil)
    t.assert_equals(mp_map:get(-1), nil)

    t.assert_error_msg_content_equals("bad argument #1 to '?' " ..
                                      "(msgpack.object expected, got number)",
                                      function()
                                          return mp.__index(1, 'key')
                                      end)
    t.assert_error_msg_content_equals("bad argument #1 to '?' " ..
                                      "(msgpack.object expected, got number)",
                                      function()
                                          return mp.__autocomplete(1, 'key')
                                      end)
end

-- Checks that `msgpack.object:__index` metamethod works correctly, correctly
-- resolving collisions in favor of `msgpack.object` methods.
g.test_object_index = function()
    local mp = msgpack.object(test_array)
    for i, v in ipairs(test_array) do
        t.assert_equals(mp[i], v)
    end

    local methods = {decode = true, iterator = true, get = true}
    mp = msgpack.object(test_map)
    for k, v in pairs(test_map) do
        if methods[k] then
            t.assert_type(mp[k], 'function')
        else
           t.assert_equals(mp[k], v)
        end
    end
end

local function tabcomplete(s)
    return console.completion_handler(s, 0, #s)
end

-- Checks that `msgpack.object` console autocompletion works correctly.
g.test_object_autocomplete = function()
    rawset(_G, 'mp', msgpack.object({}))
    local r = tabcomplete('mp:')
    t.assert_equals(r, {'mp:', 'mp:get(', 'mp:decode(', 'mp:iterator('})
end

local g_error_details_params = {
    decode = {
        func = msgpack.decode,
        exact = false,
    },
    decode_cdata = {
        func = function(data)
            return msgpack.decode(ffi.cast('char *', data), #data)
        end,
        exact = false,
    },
    object_from_raw = {
        func = msgpack.object_from_raw,
        exact = true,
    },
}

local g_error_details = t.group('msgpack.error-details', t.helpers.matrix({
    test = fun.totable(fun.map(function(k) return k end,
                               g_error_details_params)),
}))

g_error_details.test_error_details = function(cg)
    local params = g_error_details_params[cg.params.test]
    local function check_error(data, expected)
        local errmsg_prefix = 'Invalid MsgPack - '
        local ok, err = pcall(params.func, data)
        t.assert_not(ok)
        local actual, prev
        while err ~= nil do
            local v = err:unpack()
            v.prev = nil
            v.trace = nil
            v.base_type = nil
            t.assert_equals(v.type, 'ClientError')
            v.type = nil
            t.assert_equals(v.code, box.error.INVALID_MSGPACK)
            v.code = nil
            if v.message:startswith(errmsg_prefix) then
                v.message = v.message:sub(#errmsg_prefix + 1)
            end
            if prev == nil then
                actual = v
            else
                prev.prev = v
            end
            prev = v
            err = err.prev
        end
        t.assert_equals(actual, expected)
    end
    check_error('\xc1', {
        message = 'illegal code', offset = 0,
    })
    check_error('', {
        message = 'truncated input', offset = 0, trunc_count = 1,
    })
    check_error('\x94\xc0', {
        message = 'truncated input', offset = 2, trunc_count = 3,
    })
    check_error('\x94\xda\x00', {
        message = 'truncated input', offset = 1, trunc_count = 4,
    })
    if params.exact then
        check_error('\xc0\xc0', {
            message = 'junk after input', offset = 1,
        })
        check_error('\x92\x91\xc0\xc0\xc1', {
            message = 'junk after input', offset = 4,
        })
    end
    check_error('\xc7\x04\x01\xc0\xc0\xc0\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 4, ext_type = 1,
        prev = {message = 'cannot unpack decimal'},
    })
    check_error('\xc7\x04\x02\xc0\xc0\xc0\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 4, ext_type = 2,
        prev = {message = 'cannot unpack uuid'},
    })
    check_error('\xc7\x04\x04\xc0\xc0\xc0\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 4, ext_type = 4,
        prev = {message = 'cannot unpack datetime'},
    })
    check_error('\xc7\x04\x06\xc0\xc0\xc0\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 4, ext_type = 6,
        prev = {message = 'cannot unpack interval'},
    })
    check_error('\xc7\x02\x03\x91\xc1', {
        message = 'invalid extension', offset = 3, ext_len = 2, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'illegal code', offset = 1},
        },
    })
    check_error('\xc7\x02\x03\x92\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 2, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'truncated input', offset = 2, trunc_count = 1},
        },
    })
    check_error('\xc7\x04\x03\x92\xc0\xc0\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 4, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'junk after input', offset = 3},
        },
    })
    check_error('\xc7\x01\x03\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 1, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'error data must be MP_MAP', offset = 0},
        },
    })
    check_error('\xc7\x03\x03\x81\xc0\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 3, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'error data key must be MP_UINT', offset = 1},
        },
    })
    check_error('\xc7\x01\x03\x80', {
        message = 'invalid extension', offset = 3, ext_len = 1, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'MP_ERROR_STACK is missing', offset = 1},
        },
    })
    check_error('\xc7\x10\x03\x82\x00\x91\x83\x00\xa1\x00\x01\xa1\x00\x03' ..
                '\xa1\x00\x00\x91\x00', {
        message = 'invalid extension', offset = 3, ext_len = 16, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'duplicate MP_ERROR_STACK key', offset = 14},
        },
    })
    check_error('\xc7\x04\x03\x81\x00\x91\x80', {
        message = 'invalid extension', offset = 3, ext_len = 4, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'MP_ERROR_TYPE is missing', offset = 4},
        },
    })
    check_error('\xc7\x06\x03\x81\x00\x91\x81\x00\xa0', {
        message = 'invalid extension', offset = 3, ext_len = 6, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'MP_ERROR_MESSAGE is missing', offset = 6},
        },
    })
    check_error('\xc7\x08\x03\x81\x00\x91\x82\x00\xa0\x03\xa0', {
        message = 'invalid extension', offset = 3, ext_len = 8, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'MP_ERROR_FILE is missing', offset = 8},
        },
    })
    check_error('\xc7\x03\x03\x81\x00\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 3, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {
                message = 'MP_ERROR_STACK value must be MP_ARRAY',
                offset = 2,
            },
        },
    })
    check_error('\xc7\x04\x03\x81\x00\x91\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 4, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {
                message = 'MP_ERROR_STACK array entry must be MP_MAP',
                offset = 3,
            },
        },
    })
    check_error('\xc7\x06\x03\x81\x00\x91\x81\xc0\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 6, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {message = 'error field key must be MP_UINT', offset = 4},
        },
    })
    check_error('\xc7\x06\x03\x81\x00\x91\x81\x00\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 6, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {
                message = 'MP_ERROR_TYPE value must be MP_STR',
                offset = 5,
            },
        },
    })
    check_error('\xc7\x06\x03\x81\x00\x91\x81\x01\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 6, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {
                message = 'MP_ERROR_FILE value must be MP_STR',
                offset = 5,
            },
        },
    })
    check_error('\xc7\x06\x03\x81\x00\x91\x81\x02\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 6, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {
                message = 'MP_ERROR_LINE value must be MP_UINT',
                offset = 5,
            },
        },
    })
    check_error('\xc7\x06\x03\x81\x00\x91\x81\x03\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 6, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {
                message = 'MP_ERROR_MESSAGE value must be MP_STR',
                offset = 5,
            },
        },
    })
    check_error('\xc7\x06\x03\x81\x00\x91\x81\x04\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 6, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {
                message = 'MP_ERROR_ERRNO value must be MP_UINT',
                offset = 5,
            },
        },
    })
    check_error('\xc7\x06\x03\x81\x00\x91\x81\x05\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 6, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {
                message = 'MP_ERROR_CODE value must be MP_UINT',
                offset = 5,
            },
        },
    })
    check_error('\xc7\x06\x03\x81\x00\x91\x81\x06\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 6, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {
                message = 'MP_ERROR_FIELDS value must be MP_MAP',
                offset = 5,
            },
        },
    })
    check_error('\xc7\x08\x03\x81\x00\x91\x81\x06\x81\xc0\xc0', {
        message = 'invalid extension', offset = 3, ext_len = 8, ext_type = 3,
        prev = {
            message = 'cannot unpack error',
            prev = {
                message = 'error payload field name must be MP_STR',
                offset = 6,
            },
        },
    })
end
