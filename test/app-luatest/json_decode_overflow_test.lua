local t = require('luatest')
local json = require('json')
local ffi = require('ffi')
local msgpack = require('msgpack')

local modes = {'clamp', 'error'}
local overflow_error = 'integer outside the supported range'
local positive = '18446744073709551616'
local negative = '-9223372036854775809'

local g = t.group('json_integer_overflow', t.helpers.matrix({
    mode = modes,
    api = {'cfg', 'decode'},
}))

g.before_each(function(cg)
    cg.json = json.new()
    local mode = cg.params.mode
    if cg.params.api == 'cfg' then
        cg.json.cfg({decode_overflow = mode})
        t.assert_equals(cg.json.cfg.decode_overflow, mode)
    end
    cg.decode = function(data, options)
        options = options or {}
        if cg.params.api == 'decode' then
            options.decode_overflow = mode
        end
        return cg.json.decode(data, options)
    end
end)

local function clamped(value)
    if value:sub(1, 1) == '-' then
        return -9223372036854775808LL
    end
    return 18446744073709551615ULL
end

g.test_overflow = function(cg)
    for _, value in ipairs({positive, negative}) do
        for _, input in ipairs({value, ' \n\t' .. value .. ' \r\n'}) do
            if cg.params.mode == 'error' then
                t.assert_error_msg_contains(overflow_error, cg.decode, input)
            else
                local result = cg.decode(input)
                local exp = clamped(value)
                t.assert_equals(type(result), type(exp))
                t.assert_equals(result, exp)
                t.assert_equals(ffi.typeof(result), ffi.typeof(exp))
            end
        end
    end
end

g.test_integer_boundaries = function(cg)
    local cases = {
        {'0', 0},
        {'-0', 0},
        {'1', 1},
        {'-1', -1},
        {'99999999999999', 99999999999999},
        {'100000000000000', 100000000000000LL},
        {'-99999999999999', -99999999999999},
        {'-100000000000000', -100000000000000LL},
        {'9007199254740991', 9007199254740991LL},
        {'9007199254740992', 9007199254740992LL},
        {'9007199254740993', 9007199254740993LL},
        {'9223372036854775806', 9223372036854775806LL},
        {'9223372036854775807', 9223372036854775807ULL},
        {'9223372036854775808', 9223372036854775808ULL},
        {'18446744073709551614', 18446744073709551614ULL},
        {'18446744073709551615', 18446744073709551615ULL},
        {'-9223372036854775807', -9223372036854775807LL},
        {'-9223372036854775808', -9223372036854775808LL},
    }
    for _, case in ipairs(cases) do
        local result = cg.decode(case[1])
        t.assert_equals(result, case[2])
        t.assert_equals(type(result), type(case[2]))
        if type(result) == 'cdata' then
            t.assert_equals(ffi.typeof(result), ffi.typeof(case[2]))
        end
    end
end

g.test_floating_point_unchanged = function(cg)
    for _, value in ipairs({positive, negative}) do
        for _, suffix in ipairs({'.0', 'e0', 'E+0', 'e-20', '.25e-5'}) do
            local result = cg.decode(value .. suffix)
            t.assert_equals(type(result), 'number')
            t.assert_equals(result, tonumber(value .. suffix))
        end
    end
    t.assert_equals(cg.decode('1e400'), math.huge)
    t.assert_error_msg_contains('number must not be NaN or Inf', cg.decode,
                               '1e400', {decode_invalid_numbers = false})
end

g.test_other_values_unchanged = function(cg)
    t.assert_equals(cg.decode('"' .. positive .. '"'), positive)
    t.assert_equals(cg.decode('[true,false,null,"' .. negative .. '"]'),
                    {true, false, json.NULL, negative})
end

g.test_nested_values = function(cg)
    local input = '{"a":[' .. positive .. ',{"b":' .. negative ..
                  '}],"tail":[1,"text",null]}'
    if cg.params.mode == 'error' then
        t.assert_error_msg_contains(overflow_error, cg.decode, input)
        return
    end
    local pos = clamped(positive)
    local neg = clamped(negative)
    local result = cg.decode(input)
    t.assert_equals(result, {
        a = {pos, {b = neg}},
        tail = {1, 'text', json.NULL},
    })
    t.assert_equals(type(result.a[1]), type(pos))
    t.assert_equals(type(result.a[2].b), type(neg))
    t.assert_equals(cg.decode('[' .. positive .. ',1,' .. negative .. ']'),
                    {pos, 1, neg})
    local object = cg.decode('{"x":' .. positive .. '}')
    t.assert_equals(next(object), 'x')
end

g.test_long_integers = function(cg)
    for _, sign in ipairs({'', '-'}) do
        local value = sign .. string.rep('9', 10000)
        if cg.params.mode == 'error' then
            t.assert_error_msg_contains(overflow_error, cg.decode, value)
        else
            t.assert_equals(cg.decode(value), clamped(value))
        end
        -- A preceding overflow must not affect the next conversion's errno.
        t.assert_equals(cg.decode('42'), 42)
    end
end

g.test_malformed_input = function(cg)
    for _, input in ipairs({
        positive .. 'x', '[' .. positive .. ' 1]',
        '{"x":' .. positive .. ',}', '+' .. positive, '0' .. positive,
        negative .. '-', '--' .. positive,
    }) do
        t.assert_error(cg.decode, input)
    end
end

g.test_object_keys_must_be_quoted = function(cg)
    for _, key in ipairs({'123', positive, negative}) do
        for _, input in ipairs({
            '{' .. key .. ':1}',
            '{"valid":0,' .. key .. ':1}',
            '[{"nested":{' .. key .. ':1}}]',
        }) do
            t.assert_error_msg_contains('Expected object key string',
                                       cg.decode, input)
        end
        t.assert_equals(cg.decode('{"' .. key .. '":1}'), {[key] = 1})
    end
end

local g_cfg = t.group('json_integer_overflow_config')

g_cfg.test_default_and_overrides = function()
    local s = json.new()
    t.assert_equals(s.cfg.decode_overflow, 'clamp')
    t.assert_equals(s.decode(positive), 18446744073709551615ULL)
    t.assert_equals(s.decode(negative), -9223372036854775808LL)
    s.cfg({decode_overflow = 'error'})
    for _, mode in ipairs(modes) do
        if mode == 'error' then
            t.assert_error_msg_contains(overflow_error, s.decode, positive,
                                       {decode_overflow = mode})
        else
            t.assert_equals(s.decode(positive, {decode_overflow = mode}),
                            clamped(positive))
        end
        t.assert_equals(s.cfg.decode_overflow, 'error')
        t.assert_error_msg_contains(overflow_error, s.decode, positive)
    end
    t.assert_equals(json.new().cfg.decode_overflow, 'clamp')
    t.assert_equals(json.cfg.decode_overflow, 'clamp')
    t.assert_equals(json.decode(positive), 18446744073709551615ULL)
    s.cfg({decode_overflow = 'clamp'})
    t.assert_equals(s.decode(positive), 18446744073709551615ULL)
end

g_cfg.test_invalid_options = function()
    local s = json.new()
    s.cfg({decode_overflow = 'error', encode_key_order = {'a', 'b'}})
    local max_depth = s.cfg.decode_max_depth
    for _, value in ipairs({
        '', 'nil', 'null', 'number', 'decimal', 'string', 'unknown',
        'CLAMP', 'clamp\0error', 'error\0',
        0, 1, false, true, {}, function() end, json.NULL,
    }) do
        local options = {
            decode_overflow = value,
            decode_max_depth = 1,
            encode_invalid_numbers = false,
            encode_key_order = {'b', 'a'},
        }
        t.assert_error_msg_contains('Invalid decode_overflow', s.cfg, options)
        t.assert_error_msg_contains('Invalid decode_overflow', s.decode,
                                   positive, options)
        t.assert_equals(s.cfg.decode_overflow, 'error')
        t.assert_equals(s.cfg.decode_max_depth, max_depth)
        t.assert_equals(s.cfg.encode_invalid_numbers, true)
        t.assert_equals(s.cfg.encode_key_order, {'a', 'b'})
        t.assert_error_msg_contains(overflow_error, s.decode, positive)
        t.assert_equals(s.decode('{"a":{"b":1}}'), {a = {b = 1}})
        t.assert_equals(s.encode({a = 1, b = 2}), '{"a":1,"b":2}')
        t.assert_equals(s.encode(math.huge), 'inf')
    end
    t.assert_error_msg_contains(overflow_error, s.decode, positive,
                               {decode_overflow = nil})
end

g_cfg.before_test('test_invalid_msgpack_cfg', function(cg)
    cg.encode_invalid_numbers = msgpack.cfg.encode_invalid_numbers
    msgpack.cfg({encode_invalid_numbers = true})
end)

g_cfg.after_test('test_invalid_msgpack_cfg', function(cg)
    msgpack.cfg({encode_invalid_numbers = cg.encode_invalid_numbers})
end)

g_cfg.test_invalid_msgpack_cfg = function()
    t.assert_error_msg_contains('Invalid decode_overflow', msgpack.cfg, {
        encode_invalid_numbers = false,
        decode_overflow = 'typo',
    })
    -- Rejected options must not desynchronize the dependent tuple serializer.
    local msgpack_ok = pcall(msgpack.encode, {math.huge})
    local tuple_ok = pcall(box.tuple.new, {math.huge})
    t.assert_equals(msgpack_ok, tuple_ok)
    t.assert(msgpack_ok)
    t.assert_equals(msgpack.cfg.encode_invalid_numbers, true)

    msgpack.cfg({encode_invalid_numbers = false})
    t.assert_error_msg_contains('number must not be NaN or Inf',
                               msgpack.encode, {math.huge})
    t.assert_error_msg_contains('number must not be NaN or Inf',
                               box.tuple.new, {math.huge})
end

g_cfg.test_error_positions = function()
    for _, value in ipairs({positive, negative}) do
        t.assert_error_msg_contains('on line 2 at character 3', json.decode,
                                   '[\n  ' .. value .. ']',
                                   {decode_overflow = 'error'})
    end
end

g_cfg.test_encoder_unchanged = function()
    for _, mode in ipairs(modes) do
        local s = json.new()
        s.cfg({decode_overflow = mode})
        t.assert_equals(s.encode({1, 'text', json.NULL}), '[1,"text",null]')
        t.assert_equals(s.encode(18446744073709551615ULL),
                        '18446744073709551615')
        t.assert_equals(s.encode(-9223372036854775808LL),
                        '-9223372036854775808')
        local options = {decode_overflow = mode}
        t.assert_equals(s.encode(ffi.new('int64_t', 1), options), '1')
    end
end
