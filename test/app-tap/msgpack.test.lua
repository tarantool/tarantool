#!/usr/bin/env tarantool

package.path = "lua/?.lua;"..package.path

local tap = require('tap')
local common = require('serializer_test')

local function is_map(s)
    local b = string.byte(string.sub(s, 1, 1))
    return b >= 0x80 and b <= 0x8f or b == 0xde or b == 0xdf
end

local function is_array(s)
    local b = string.byte(string.sub(s, 1, 1))
    return b >= 0x90 and b <= 0x9f or b == 0xdc or b == 0xdd
end

local function test_offsets(test, s)
    test:plan(6)
    local arr1 = {1, 2, 3}
    local arr2 = {4, 5, 6}
    local dump = s.encode(arr1)..s.encode(arr2)
    test:is(dump:len(), 8, "length of part1 + part2")

    local a
    local offset = 1
    a, offset = s.decode(dump, offset)
    test:is_deeply(a, arr1, "decoded part1")
    test:is(offset, 5, "offset of part2")

    a, offset = s.decode(dump, offset)
    test:is_deeply(a, arr2, "decoded part2")
    test:is(offset, 9, "offset of end")

    test:ok(not pcall(s.decode, dump, offset), "invalid offset")
end

local function test_misc(test, s)
    test:plan(8)
    local ffi = require('ffi')
    local buffer = require('buffer')
    local buf = ffi.cast("const char *", "\x91\x01")
    local bufcopy = ffi.cast('const char *', buf)
    local bufend, result = s.ibuf_decode(buf)
    local st,e = pcall(s.ibuf_decode, buffer.ibuf().rpos)
    test:is(buf, bufcopy, "ibuf_decode argument is constant")
    test:is(buf + 2, bufend, 'ibuf_decode position')
    test:is_deeply(result, {1}, "ibuf_decode result")
    test:ok(not st and e:match("null"), "null ibuf")
    st, e = pcall(s.decode, "\xd4\x0f\x00")
    test:is(e, "Unsupported MsgPack extension type: 15",
               "decode result for \\xd4\\x0f\\x00: " .. e)
    test:ok(not st, "unsupported extension decode")
    st, e = pcall(s.decode, "\xd4\xfe\x00")
    test:is(e, "Unsupported MsgPack extension type: -2",
               "decode result for \\xd4\\xfe\\x00: " .. e)
    test:ok(not st, "unsupported extension decode")
end

local function test_decode_array_map_header(test, s)
    local ffi = require('ffi')

    local usage_err = 'Usage: msgpack%.decode_[^_(]+_header%(ptr, size%)'
    local end_of_buffer_err = 'msgpack%.decode_[^_]+_header: unexpected end ' ..
        'of buffer'
    local non_positive_size_err = 'msgpack.decode_[^_]+_header: ' ..
        'non%-positive size'
    local wrong_type_err = "msgpack.decode_[^_]+_header: 'char %*' expected"

    local decode_cases = {
        {
            'fixarray',
            func = s.decode_array_header,
            data = ffi.cast('char *', '\x94'),
            size = 1,
            exp_len = 4,
            exp_rewind = 1,
        },
        {
            'array 16',
            func = s.decode_array_header,
            data = ffi.cast('char *', '\xdc\x00\x04'),
            size = 3,
            exp_len = 4,
            exp_rewind = 3,
        },
        {
            'array 32',
            func = s.decode_array_header,
            data = ffi.cast('char *', '\xdd\x00\x00\x00\x04'),
            size = 5,
            exp_len = 4,
            exp_rewind = 5,
        },
        {
            'truncated array 16',
            func = s.decode_array_header,
            data = ffi.cast('char *', '\xdc\x00'),
            size = 2,
            exp_err = end_of_buffer_err,
        },
        {
            'truncated array 32',
            func = s.decode_array_header,
            data = ffi.cast('char *', '\xdd\x00\x00\x00'),
            size = 4,
            exp_err = end_of_buffer_err,
        },
        {
            'fixmap',
            func = s.decode_map_header,
            data = ffi.cast('char *', '\x84'),
            size = 1,
            exp_len = 4,
            exp_rewind = 1,
        },
        {
            'map 16',
            func = s.decode_map_header,
            data = ffi.cast('char *', '\xde\x00\x04'),
            size = 3,
            exp_len = 4,
            exp_rewind = 3,
        },
        {
            'array 32',
            func = s.decode_map_header,
            data = ffi.cast('char *', '\xdf\x00\x00\x00\x04'),
            size = 5,
            exp_len = 4,
            exp_rewind = 5,
        },
        {
            'truncated map 16',
            func = s.decode_map_header,
            data = ffi.cast('char *', '\xde\x00'),
            size = 2,
            exp_err = end_of_buffer_err,
        },
        {
            'truncated map 32',
            func = s.decode_map_header,
            data = ffi.cast('char *', '\xdf\x00\x00\x00'),
            size = 4,
            exp_err = end_of_buffer_err,
        },
        -- gh-3926: Ensure that a returned pointer has the same
        -- cdata type as passed argument.
        --
        -- cdata<char *> arguments are passed in the cases above,
        -- so only cdata<const char *> argument is checked here.
        {
            'fixarray (const char *)',
            func = s.decode_array_header,
            data = ffi.cast('const char *', '\x94'),
            size = 1,
            exp_len = 4,
            exp_rewind = 1,
        },
        {
            'fixmap (const char *)',
            func = s.decode_map_header,
            data = ffi.cast('const char *', '\x84'),
            size = 1,
            exp_len = 4,
            exp_rewind = 1,
        },
    }

    local bad_api_cases = {
        {
            'wrong msgpack type',
            data = ffi.cast('char *', '\xc0'),
            size = 1,
            exp_err = 'msgpack.decode_[^_]+_header: unexpected msgpack type',
        },
        {
            'zero size buffer',
            data = ffi.cast('char *', ''),
            size = 0,
            exp_err = non_positive_size_err,
        },
        {
            'negative size buffer',
            data = ffi.cast('char *', ''),
            size = -1,
            exp_err = non_positive_size_err,
        },
        {
            'size is nil',
            data = ffi.cast('char *', ''),
            size = nil,
            exp_err = 'bad argument',
        },
        {
            'no arguments',
            args = {},
            exp_err = usage_err,
        },
        {
            'one argument',
            args = {ffi.cast('char *', '')},
            exp_err = usage_err,
        },
        {
            'data is nil',
            args = {nil, 1},
            exp_err = wrong_type_err,
        },
        {
            'data is not cdata',
            args = {1, 1},
            exp_err = wrong_type_err,
        },
        {
            'data with wrong cdata type',
            args = {box.tuple.new(), 1},
            exp_err = wrong_type_err,
        },
        {
            'size has wrong type',
            args = {ffi.cast('char *', ''), 'eee'},
            exp_err = 'bad argument',
        },
    }

    test:plan(#decode_cases + 2 * #bad_api_cases)

    -- Decode cases.
    for _, case in ipairs(decode_cases) do
        if case.exp_err ~= nil then
            local ok, err = pcall(case.func, case.data, case.size)
            local description = ('bad; %s'):format(case[1])
            test:ok(ok == false and err:match(case.exp_err), description)
        else
            local len, new_buf = case.func(case.data, case.size)
            local rewind = new_buf - case.data

            -- gh-3926: Verify cdata type of a returned buffer.
            local data_ctype = tostring(ffi.typeof(case.data))
            local new_buf_ctype = tostring(ffi.typeof(new_buf))

            local description = ('good; %s'):format(case[1])
            test:is_deeply({len, rewind, new_buf_ctype}, {case.exp_len,
                case.exp_rewind, data_ctype}, description)
        end
    end

    -- Bad api usage cases.
    for _, func_name in ipairs({'decode_array_header', 'decode_map_header'}) do
        for _, case in ipairs(bad_api_cases) do
            local ok, err
            if case.args ~= nil then
                local args_len = table.maxn(case.args)
                ok, err = pcall(s[func_name], unpack(case.args, 1, args_len))
            else
                ok, err = pcall(s[func_name], case.data, case.size)
            end
            local description = ('%s bad api usage; %s'):format(func_name,
                                                                case[1])
            test:ok(ok == false and err:match(case.exp_err), description)
        end
    end
end

tap.test("msgpack", function(test)
    local serializer = require('msgpack')
    test:plan(13)
    test:test("unsigned", common.test_unsigned, serializer)
    test:test("signed", common.test_signed, serializer)
    test:test("double", common.test_double, serializer)
    test:test("boolean", common.test_boolean, serializer)
    test:test("string", common.test_string, serializer)
    test:test("nil", common.test_nil, serializer)
    test:test("table", common.test_table, serializer, is_array, is_map)
    test:test("ucdata", common.test_ucdata, serializer)
    test:test("depth", common.test_depth, serializer)
    test:test("offsets", test_offsets, serializer)
    test:test("misc", test_misc, serializer)
    test:test("decode_array_map", test_decode_array_map_header, serializer)
    test:test("decode_buffer", common.test_decode_buffer, serializer)
end)
