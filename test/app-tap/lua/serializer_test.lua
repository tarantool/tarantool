local ffi = require('ffi')

local function rt(test, s, x, t)
    local buf1 = s.encode(x)
    local x1 = s.decode(buf1)
    local xstr
    if type(x) == "table" then
        xstr = "table"
    elseif ffi.istype('float', x) then
        xstr = string.format('%0.2f (ffi float)', tonumber(x))
    elseif ffi.istype('double', x) then
        xstr = string.format('%0.2f (ffi double)', tonumber(x))
    elseif ffi.istype("bool", x) then
        xstr = string.format("%s (ffi bool)", x == 1 and "true" or "false")
    elseif type(x) == "cdata" then
        xstr = tostring(x)
        xstr = xstr:match("cdata<.+>:") or xstr
    else
        xstr = tostring(x)
    end
    test:is_deeply(x, x1, "encode/decode for "..xstr)
    if t ~= nil then
        test:is(type(x1), t, "encode/decode type for "..xstr)
    end
end

local function test_unsigned(test, s)
    test:plan(104)
    rt(test, s, 0, "number")
    rt(test, s, 0LL, "number")
    rt(test, s, 0ULL, "number")

    rt(test, s, 1, "number")
    rt(test, s, 1LL, "number")
    rt(test, s, 1ULL, "number")

    rt(test, s, 127, "number")
    rt(test, s, 127LL, "number")
    rt(test, s, 127ULL, "number")

    rt(test, s, 128, "number")
    rt(test, s, 128LL, "number")
    rt(test, s, 128ULL, "number")

    rt(test, s, 255, "number")
    rt(test, s, 255LL, "number")
    rt(test, s, 255ULL, "number")

    rt(test, s, 256, "number")
    rt(test, s, 256LL, "number")
    rt(test, s, 256ULL, "number")

    rt(test, s, 65535, "number")
    rt(test, s, 65535LL, "number")
    rt(test, s, 65535ULL, "number")

    rt(test, s, 65536, "number")
    rt(test, s, 65536LL, "number")
    rt(test, s, 65536ULL, "number")

    rt(test, s, 4294967294, "number")
    rt(test, s, 4294967294LL, "number")
    rt(test, s, 4294967294ULL, "number")

    rt(test, s, 4294967295, "number")
    rt(test, s, 4294967295LL, "number")
    rt(test, s, 4294967295ULL, "number")

    rt(test, s, 4294967296, "number")
    rt(test, s, 4294967296LL, "number")
    rt(test, s, 4294967296ULL, "number")

    rt(test, s, 4294967297, "number")
    rt(test, s, 4294967297LL, "number")
    rt(test, s, 4294967297ULL, "number")

    -- 1e52 - maximum int that can be stored to double without losing precision
    -- decrease double capacity to fit .14g output format
    rt(test, s, 99999999999999, "number")
    rt(test, s, 99999999999999LL, "number")
    rt(test, s, 99999999999999ULL, "number")
    rt(test, s, 100000000000000, "cdata")
    rt(test, s, 100000000000000LL, "cdata")
    rt(test, s, 100000000000000ULL, "cdata")

    rt(test, s, 9223372036854775807LL, "cdata")
    rt(test, s, 9223372036854775807ULL, "cdata")

    rt(test, s, 9223372036854775808ULL, "cdata")
    rt(test, s, 9223372036854775809ULL, "cdata")
    rt(test, s, 18446744073709551614ULL, "cdata")
    rt(test, s, 18446744073709551615ULL, "cdata")

    rt(test, s, -1ULL, "cdata")

    -- don't use 'unsigned char' or 'signed char' because output
    -- depends -fsigned-char flag.
    rt(test, s, ffi.new('char', 128), 'number')
    rt(test, s, ffi.new('unsigned short', 128), 'number')
    rt(test, s, ffi.new('unsigned int', 128), 'number')
end

local function test_signed(test, s)
    test:plan(53)

    rt(test, s, -1, 'number')
    rt(test, s, -1LL, 'number')

    rt(test, s, -31, 'number')
    rt(test, s, -31LL, 'number')

    rt(test, s, -32, 'number')
    rt(test, s, -32LL, 'number')

    rt(test, s, -127, 'number')
    rt(test, s, -127LL, 'number')

    rt(test, s, -128, 'number')
    rt(test, s, -128LL, 'number')

    rt(test, s, -32767, 'number')
    rt(test, s, -32767LL, 'number')

    rt(test, s, -32768, 'number')
    rt(test, s, -32768LL, 'number')

    rt(test, s, -2147483647, 'number')
    rt(test, s, -2147483647LL, 'number')

    rt(test, s, -2147483648, 'number')
    rt(test, s, -2147483648LL, 'number')

    -- 1e52 - maximum int that can be stored to double without losing precision
    -- decrease double capacity to fit .14g output format
    rt(test, s, -99999999999999, "number")
    rt(test, s, -99999999999999LL, "number")
    rt(test, s, -100000000000000, "cdata")
    rt(test, s, -100000000000000LL, "cdata")

    rt(test, s, -9223372036854775806LL, 'cdata')

    rt(test, s, -9223372036854775807LL, 'cdata')

    rt(test, s, ffi.new('short', -128), 'number')
    rt(test, s, ffi.new('int', -128), 'number')

    -- gh-4672: Make sure that -2^63 encoded as INTEGER.
    test:ok(s.encode(-9223372036854775808LL) == s.encode(-2^63),
            '-2^63 encoded as INTEGER')
end

local function test_double(test, s)
    test:plan(s.cfg and 15 or 9)
    rt(test, s, -1.1)

    rt(test, s, 3.1415926535898)

    rt(test, s, -3.1415926535898)

    rt(test, s, -1e100)
    rt(test, s, 1e100)
    rt(test, s, ffi.new('float', 123456))
    rt(test, s, ffi.new('double', 123456))
    rt(test, s, ffi.new('float', 12.121))
    rt(test, s, ffi.new('double', 12.121))

    if not s.cfg then
        return
    end
    --
    -- cfg: encode_invalid_numbers / decode_invalid_numbers
    --
    local nan = 0/0
    local inf = 1/0

    local ss = s.new()
    ss.cfg{encode_invalid_numbers = false}
    test:ok(not pcall(ss.encode, nan), "encode exception on nan")
    test:ok(not pcall(ss.encode, inf), "encode exception on inf")

    ss.cfg{encode_invalid_numbers = true}
    local xnan = ss.encode(nan)
    local xinf = ss.encode(inf)

    ss.cfg{decode_invalid_numbers = false}
    test:ok(not pcall(ss.decode, xnan), "decode exception on nan")
    test:ok(not pcall(ss.decode, xinf), "decode exception on inf")

    ss.cfg{decode_invalid_numbers = true}
    rt(test, s, nan)
    rt(test, s, inf)
end

local function test_decimal(test, s)
    local decimal = require('decimal')
    test:plan(10)

    rt(test, s, decimal.new(1), 'cdata')
    rt(test, s, decimal.new('1e37'), 'cdata')
    rt(test, s, decimal.new('1e-38'), 'cdata')
    rt(test, s, decimal.new('1234567891234567890.0987654321987654321'), 'cdata')
    rt(test, s, decimal.new('-1234567891234567890.0987654321987654321'), 'cdata')
end

local function test_uuid(test, s)
    local uuid = require('uuid')
    test:plan(2)

    rt(test, s, uuid.new(), 'cdata')
end

local function test_boolean(test, s)
    test:plan(4)

    rt(test, s, false)

    rt(test, s, true)

    rt(test, s, ffi.new('bool', true))
    rt(test, s, ffi.new('bool', false))
end

local function test_string(test, s)
    test:plan(8)
    rt(test, s, "")
    rt(test, s, "abcde")
    rt(test, s, "Кудыкины горы") -- utf-8
    rt(test, s, string.rep("x", 33))
    rt(test, s, '$a\t $')
    rt(test, s, '$a\t $')
    rt(test, s, [[$a\t $]])
    rt(test, s, [[$a\\t $]])
end

local function test_nil(test, s)
    test:plan(6)
    rt(test, s, nil)
    rt(test, s, s.NULL)
    test:iscdata(s.NULL, 'void *', '.NULL is cdata')
    test:ok(s.NULL == nil, '.NULL == nil')
    rt(test, s, {1, 2, 3, s.NULL, 5})
    local t = s.decode(s.encode({1, 2, 3, [5] = 5}))
    test:is(t[4], s.NULL, "sparse array with NULL")
end



local function test_table(test, s, is_array, is_map)
    test:plan(s.cfg and 31 or 13)

    rt(test, s, {})
    test:ok(is_array(s.encode({})), "empty table is array")
    rt(test, s, {1, 2, 3})
    test:ok(is_array(s.encode({1, 2, 3})), "array is array")
    rt(test, s, {k1 = 'v1', k2 = 'v2', k3 = 'v3'})
    test:ok(is_map(s.encode({k1 = 'v1', k2 = 'v2', k3 = 'v3'})), "map is map")

    -- utf-8 pairs
    rt(test, s, {['Метапеременная'] = { 'Метазначение' }})
    rt(test, s, {test = { 'Результат' }})

    local arr = setmetatable({1, 2, 3, k1 = 'v1', k2 = 'v2', 4, 5},
        { __serialize = 'seq'})
    local map = setmetatable({1, 2, 3, 4, 5}, { __serialize = 'map'})
    local obj = setmetatable({}, {
        __serialize = function() return 'serialize' end
    })

    -- __serialize on encode
    test:ok(is_array(s.encode(arr)), "array load __serialize")
    -- map
    test:ok(is_map(s.encode(map)), "map load __serialize")
    -- string (from __serialize hook)
    test:is(s.decode(s.encode(obj)), "serialize", "object load __serialize")

    -- __serialize on decode
    test:is(getmetatable(s.decode(s.encode(arr))).__serialize, "seq",
        "array save __serialize")
    test:is(getmetatable(s.decode(s.encode(map))).__serialize, "map",
        "map save __serialize")

    if not s.cfg then
        return
    end

    --
    -- encode_load_metatables
    --

    local ss = s.new()
    ss.cfg{encode_load_metatables = false}
    -- map
    test:ok(is_map(ss.encode(arr)), "array ignore __serialize")
    -- array
    test:ok(is_array(ss.encode(map)), "map ignore __serialize")
    -- array
    test:ok(is_array(ss.encode(obj)), "object ignore __serialize")

    ss.cfg{encode_load_metatables = true}
    -- array
    test:ok(is_array(ss.encode(arr)), "array load __serialize")
    -- map
    test:ok(is_map(ss.encode(map)), "map load __serialize")
    -- string (from __serialize hook)
    test:is(ss.decode(ss.encode(obj)), "serialize", "object load __serialize")

    --
    -- decode_save_metatables
    --

    local arr = {1, 2, 3}
    local map = {k1 = 'v1', k2 = 'v2', k3 = 'v3'}

    ss = s.new()
    ss.cfg{decode_save_metatables = false}
    test:isnil(getmetatable(ss.decode(ss.encode(arr))), "array __serialize")
    test:isnil(getmetatable(ss.decode(ss.encode(map))), "map __serialize")

    ss.cfg{decode_save_metatables = true}
    test:is(getmetatable(ss.decode(ss.encode(arr))).__serialize, "seq",
        "array save __serialize")
    test:is(getmetatable(ss.decode(ss.encode(map))).__serialize, "map",
        "map save __serialize")

    --
    -- encode_sparse_convert / encode_sparse_ratio / encode_sparse_safe
    --

    ss = s.new()

    ss.cfg{encode_sparse_ratio = 2, encode_sparse_safe = 10}

    ss.cfg{encode_sparse_convert = false}
    test:ok(is_array(ss.encode({[1] = 1, [3] = 3, [4] = 4, [6] = 6, [9] = 9,
        [12] = 12})), "sparse convert off")
    test:ok(is_array(ss.encode({[1] = 1, [3] = 3, [4] = 4, [6] = 6,
        [10] = 10})), "sparse convert off")
    test:ok(not pcall(ss.encode, {[1] = 1, [3] = 3, [4] = 4, [6] = 6,
        [12] = 12}), "excessively sparse array")

    ss.cfg{encode_sparse_convert = true}
    test:ok(is_array(ss.encode({[1] = 1, [3] = 3, [4] = 4, [6] = 6, [9] = 9,
        [12] = 12})), "sparse convert on")
    test:ok(is_array(ss.encode({[1] = 1, [3] = 3, [4] = 4, [6] = 6,
        [10] = 10})), "sparse convert on")
    test:ok(is_map(ss.encode({[1] = 1, [3] = 3, [4] = 4, [6] = 6, [12] = 12})),
       "sparse convert on")

    -- map
    test:ok(is_map(ss.encode({1, 2, 3, 4, 5, [100] = 100})),
       "sparse safe 1")
    ss.cfg{encode_sparse_safe = 100}
    -- array
    test:ok(is_array(ss.encode({1, 2, 3, 4, 5, [100] = 100})),
        "sparse safe 2")
end

local function test_ucdata(test, s)
    test:plan(11)

    --
    -- encode_use_unpack / encode_use_tostring
    --

    ffi.cdef[[struct serializer_cdata_test {}]]
    local ctype = ffi.typeof('struct serializer_cdata_test')
    ffi.metatype(ctype, {
        __index = {
            __serialize = function() return 'unpack' end,
        },
        __tostring = function() return 'tostring' end
    });

    local cdata = ffi.new(ctype)
    -- use fiber's userdata for test (supports both __serialize and __tostring)
    local udata = require('fiber').self()

    local ss = s.new()
    ss.cfg{
        encode_load_metatables = false,
        encode_use_tostring = false,
        encode_invalid_as_nil = false
    }
    test:ok(not pcall(ss.encode, cdata), "encode exception on cdata")
    test:ok(not pcall(ss.encode, udata), "encode exception on udata")

    ss.cfg{encode_invalid_as_nil = true}
    test:ok(ss.decode(ss.encode(cdata)) == nil, "encode_invalid_as_nil")
    test:ok(ss.decode(ss.encode(udata)) == nil, "encode_invalid_as_nil")

    ss.cfg{encode_load_metatables = true, encode_use_tostring = false}
    test:is(ss.decode(ss.encode(cdata)), 'unpack', 'cdata __serialize')
    test:istable(ss.decode(ss.encode(udata)), 'udata __serialize')

    ss.cfg{encode_load_metatables = false, encode_use_tostring = true}
    test:is(ss.decode(ss.encode(cdata)), 'tostring', 'cdata __tostring')
    test:isstring(ss.decode(ss.encode(udata)), 'udata __tostring')

    ss.cfg{encode_load_metatables = true, encode_use_tostring = true}
    test:is(ss.decode(ss.encode(cdata)), 'unpack', 'cdata hook priority')
    test:istable(ss.decode(ss.encode(udata)), 'udata  hook priority')
    -- gh-1226: luaL_convertfield should ignore __serialize hook for ctypes
    test:like(ss.decode(ss.encode(ctype)), 'ctype<struct', 'ctype __serialize')
end

local function test_depth(test, s)
    test:plan(3)
    --
    -- gh-4434: serializer update should be reflected in Lua.
    --
    local max_depth = s.cfg.encode_max_depth
    s.cfg({encode_max_depth = max_depth + 5})
    test:is(s.cfg.encode_max_depth, max_depth + 5,
            "cfg({<name> = value}) is reflected in cfg.<name>")
    s.cfg({encode_max_depth = max_depth})

    --
    -- gh-4434 (yes, the same issue): let users choose whether
    -- they want to raise an error on tables with too high nest
    -- level.
    --
    local deep_as_nil = s.cfg.encode_deep_as_nil
    s.cfg({encode_deep_as_nil = false})

    local t = nil
    for _ = 1, max_depth + 1 do t = {t} end
    local ok = pcall(s.encode, t)
    test:ok(not ok, "too deep encode depth")

    s.cfg({encode_max_depth = max_depth + 1})
    ok = pcall(s.encode, t)
    test:ok(ok, "no throw in a corner case")

    s.cfg({encode_deep_as_nil = deep_as_nil, encode_max_depth = max_depth})
end

-- gh-3926: Ensure that a returned pointer has the same cdata type
-- as passed argument.
--
-- The test case is applicable for msgpack and msgpackffi.
local function test_decode_buffer(test, s)
    local cases = {
        {
            'decode(cdata<const char *>, number)',
            func = s.decode,
            args = {ffi.cast('const char *', '\x93\x01\x02\x03'), 4},
            exp_res = {1, 2, 3},
            exp_rewind = 4,
        },
        {
            'decode(cdata<char *>, number)',
            func = s.decode,
            args = {ffi.cast('char *', '\x93\x01\x02\x03'), 4},
            exp_res = {1, 2, 3},
            exp_rewind = 4,
        },
        {
            'decode_unchecked(cdata<const char *>)',
            func = s.decode_unchecked,
            args = {ffi.cast('const char *', '\x93\x01\x02\x03')},
            exp_res = {1, 2, 3},
            exp_rewind = 4,
        },
        {
            'decode_unchecked(cdata<char *>)',
            func = s.decode_unchecked,
            args = {ffi.cast('char *', '\x93\x01\x02\x03')},
            exp_res = {1, 2, 3},
            exp_rewind = 4,
        },
    }

    test:plan(#cases)

    for _, case in ipairs(cases) do
        test:test(case[1], function(test)
            test:plan(4)
            local args_len = table.maxn(case.args)
            local res, res_buf = case.func(unpack(case.args, 1, args_len))
            test:is_deeply(res, case.exp_res, 'verify result')
            local buf = case.args[1]
            local rewind = res_buf - buf
            test:is(rewind, case.exp_rewind, 'verify resulting buffer')
            -- test:iscdata() is not sufficient here, because it
            -- ignores 'const' qualifier (because of using
            -- ffi.istype()).
            test:is(type(res_buf), 'cdata', 'verify resulting buffer type')
            local buf_ctype = tostring(ffi.typeof(buf))
            local res_buf_ctype = tostring(ffi.typeof(res_buf))
            test:is(res_buf_ctype, buf_ctype, 'verify resulting buffer ctype')
        end)
    end
end

return {
    test_unsigned = test_unsigned;
    test_signed = test_signed;
    test_double = test_double;
    test_boolean = test_boolean;
    test_string = test_string;
    test_nil = test_nil;
    test_table = test_table;
    test_ucdata = test_ucdata;
    test_decimal = test_decimal;
    test_uuid = test_uuid;
    test_depth = test_depth;
    test_decode_buffer = test_decode_buffer;
}
