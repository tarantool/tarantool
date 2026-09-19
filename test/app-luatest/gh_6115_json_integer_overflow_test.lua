local t = require('luatest')
local json = require('json')
local decimal = require('decimal')
local ffi = require('ffi')

local modes = {'clamp', 'error', 'number', 'decimal', 'string', 'nil'}
local overflow_error = 'integer outside the supported range'
local decimal_error = 'integer outside the supported decimal range'
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

local function expected(value, mode)
    if mode == 'clamp' then
        if value:sub(1, 1) == '-' then
            return -9223372036854775808LL
        end
        return 18446744073709551615ULL
    elseif mode == 'number' then
        return tonumber(value)
    elseif mode == 'decimal' then
        return decimal.new(value)
    elseif mode == 'string' then
        return value
    elseif mode == 'nil' then
        return json.NULL
    end
end

g.test_overflow = function(cg)
    for _, value in ipairs({positive, negative}) do
        for _, input in ipairs({value, ' \n\t' .. value .. ' \r\n'}) do
            if cg.params.mode == 'error' then
                t.assert_error_msg_contains(overflow_error, cg.decode, input)
            else
                local result = cg.decode(input)
                local exp = expected(value, cg.params.mode)
                t.assert_equals(type(result), type(exp))
                t.assert_equals(result, exp)
                if cg.params.mode == 'decimal' then
                    t.assert(decimal.is_decimal(result))
                    t.assert_equals(tostring(result), value)
                end
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
        {'9007199254740991', 9007199254740991},
        {'9007199254740992', 9007199254740992ULL},
        {'9007199254740993', 9007199254740993ULL},
        {'9223372036854775806', 9223372036854775806LL},
        {'9223372036854775807', 9223372036854775807LL},
        {'9223372036854775808', 9223372036854775808ULL},
        {'18446744073709551614', 18446744073709551614ULL},
        {'18446744073709551615', 18446744073709551615ULL},
        {'-9223372036854775807', -9223372036854775807LL},
        {'-9223372036854775808', -9223372036854775808LL},
    }
    for _, case in ipairs(cases) do
        local result = cg.decode(case[1])
        t.assert_equals(result, case[2])
        t.assert_equals(type(result), type(json.decode(case[1])))
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
    local pos = expected(positive, cg.params.mode)
    local neg = expected(negative, cg.params.mode)
    local result = cg.decode(input)
    t.assert_equals(result, {
        a = {pos, {b = neg}},
        tail = {1, 'text', json.NULL},
    })
    -- A null result must preserve keys and trailing array elements.
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
            t.assert_equals(cg.decode(value), expected(value, cg.params.mode))
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
                            expected(positive, mode))
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
    s.cfg({decode_overflow = 'string'})
    for _, value in ipairs({
        '', 'unknown', 'CLAMP', 'clamp\0error', 'error\0', 0, 1, false, true,
        {}, function() end, json.NULL,
    }) do
        local options = {decode_overflow = value}
        t.assert_error_msg_contains('Invalid decode_overflow', s.cfg, options)
        t.assert_error_msg_contains('Invalid decode_overflow', s.decode,
                                   positive, options)
        t.assert_equals(s.cfg.decode_overflow, 'string')
        t.assert_equals(s.decode(positive), positive)
    end
    t.assert_equals(s.decode(positive, {decode_overflow = nil}), positive)
end

g_cfg.test_decimal_precision_and_limits = function()
    for _, sign in ipairs({'', '-'}) do
        local options = {decode_overflow = 'decimal'}
        local exact = sign .. string.rep('9', 76)
        t.assert_equals(tostring(json.decode(exact, options)), exact)
        -- Use the same precision and rounding as decimal.new().
        for _, value in ipairs({sign .. string.rep('9', 77),
                                sign .. '1' .. string.rep('0', 76)}) do
            local result = json.decode('[' .. value .. ',0]', options)
            t.assert(decimal.is_decimal(result[1]))
            t.assert_equals(tostring(result[1]), tostring(decimal.new(value)))
            t.assert_equals(result[2], 0)
        end
        -- The decimal context allows adjusted exponents up to 999999.
        local largest_power = sign .. '1' .. string.rep('0', 999999)
        t.assert_equals(json.decode(largest_power, options),
                        decimal.new(sign .. '1e999999'))
        t.assert_error_msg_contains(decimal_error, json.decode,
                                   largest_power .. '0', options)
        -- Rounding can push an otherwise valid exponent out of range.
        t.assert_error_msg_contains(decimal_error, json.decode,
                                   sign .. string.rep('9', 1000000), options)
        t.assert_equals(tostring(json.decode(positive, options)), positive)
    end
end

g_cfg.test_number_precision_and_infinity = function()
    local options = {decode_overflow = 'number', decode_invalid_numbers = false}
    local result = json.decode('18446744073709551617', options)
    t.assert_equals(type(result), 'number')
    t.assert_equals(result, 2^64)
    for _, sign in ipairs({'', '-'}) do
        local finite = sign .. string.rep('9', 308)
        t.assert_equals(json.decode(finite, options), tonumber(finite))
        local infinite = sign .. string.rep('9', 309)
        t.assert_error_msg_contains('number must not be NaN or Inf',
                                   json.decode, '[\n' .. infinite .. ']',
                                   options)
        options.decode_invalid_numbers = true
        t.assert_equals(json.decode(infinite, options), tonumber(infinite))
        options.decode_invalid_numbers = false
    end
end

g_cfg.test_error_positions = function()
    t.assert_error_msg_contains('on line 2 at character 3', json.decode,
                               '[\n  ' .. positive .. ']',
                               {decode_overflow = 'error'})
    t.assert_error_msg_contains('on line 2 at character 3', json.decode,
                               '[\n  1' .. string.rep('0', 1000000) .. ']',
                               {decode_overflow = 'decimal'})
    t.assert_error_msg_contains('on line 2 at character 3', json.decode,
                               '[\n  ' .. string.rep('9', 309) .. ']',
                               {decode_overflow = 'number',
                                decode_invalid_numbers = false})
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
