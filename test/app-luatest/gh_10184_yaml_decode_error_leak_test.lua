local t = require('luatest')
local yaml = require('yaml')

local g = t.group()

local function assert_decode_error(decoder, document, options, message)
    -- Repeated failures leave unreachable allocations for LeakSanitizer.
    for _ = 1, 10 do
        t.assert_error_msg_contains(message, decoder.decode, document, options)
    end
    t.assert_equals(decoder.decode('a: 1'), {a = 1})
end

g.test_invalid_options = function()
    assert_decode_error(yaml, 'a: 1', false, 'Usage: yaml.decode')
end

g.test_options_index_error = function()
    local options = setmetatable({}, {
        __index = function() error('invalid options') end,
    })
    assert_decode_error(yaml, 'a: 1', options, 'invalid options')
end

g.test_parser_errors = function()
    local cases = {
        {'foo: bar: baz', 'mapping values are not allowed'},
        {'"\\uD800"', 'found invalid Unicode character escape code'},
        {'"\\U00110000"', 'found invalid Unicode character escape code'},
        {'*missing', 'invalid reference: missing'},
    }
    for _, case in ipairs(cases) do
        assert_decode_error(yaml, case[1], nil, case[2])
    end
end

g.test_invalid_numbers = function()
    local decoder = yaml.new()
    decoder.cfg{decode_invalid_numbers = false}
    for _, number in ipairs({'nan', 'inf', '-inf'}) do
        for _, prefix in ipairs({'', '!!float '}) do
            assert_decode_error(decoder, prefix .. number, nil,
                                'number must not be NaN or Inf')
        end
    end
end

g.test_nan_keys = function()
    local decoder = yaml.new()
    decoder.cfg{decode_invalid_numbers = true}
    local documents = {
        'nan: 1',
        '{nan: 1}',
        '!!float nan: 1',
        '? !!float nan\n: 1',
    }
    for _, document in ipairs(documents) do
        assert_decode_error(decoder, document, nil, 'table index is NaN')
    end
    local result = decoder.decode('a: nan')
    t.assert_not_equals(result.a, result.a)
    t.assert_equals(decoder.decode('inf'), math.huge)
    t.assert_equals(decoder.decode('-inf'), -math.huge)
end

g.test_tag_only_error = function()
    for _ = 1, 10 do
        local tag, err = yaml.decode('%TAG invalid\n---\na: 1',
                                     {tag_only = true})
        t.assert_equals(tag, nil)
        t.assert_str_contains(err, 'did not find expected')
    end
    t.assert_equals(yaml.decode('a: 1', {tag_only = true}), nil)
    local document = '%TAG !test! tag:example.com,2026:\n---\n'
    local handle, prefix = yaml.decode(document, {tag_only = true})
    t.assert_equals(handle, '!test!')
    t.assert_equals(prefix, 'tag:example.com,2026:')
end

g.test_successful_decode = function()
    local first, second = yaml.decode('---\na: 1\n---\nb: 2')
    t.assert_equals(first, {a = 1})
    t.assert_equals(second, {b = 2})
    local result = yaml.decode('a: &value [1, 2]\nb: *value')
    t.assert_equals(result, {a = {1, 2}, b = {1, 2}})
    t.assert_is(result.a, result.b)
end
