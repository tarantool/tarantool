local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

g.test_human_readable_value = function()
    local config = cbuilder:new()
        :set_global_option('lua.memory', '256 MiB')
        :add_instance('i-001', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start()

    cluster['i-001']:exec(function()
        local alloc = require('internal.alloc')
        local config = require('config')

        t.assert_equals(config:get('lua.memory'), 256 * 1024 * 1024)
        t.assert_equals(alloc.getlimit(), 256 * 1024 * 1024)
    end)
end

g.test_size_parse = function()
    local size = require('internal.config.utils.size')

    local cases = {
        {value = 0, expected = 0},
        {value = 42, expected = 42},
        {value = '42', expected = 42},
        {value = '42B', expected = 42},
        {value = ' 32 MiB ', expected = 32 * 1024 * 1024},
        {value = '8GiB', expected = 8 * 1024 * 1024 * 1024},
    }

    for _, case in ipairs(cases) do
        t.assert_equals(size.parse(case.value), case.expected)
    end
end

g.test_size_parse_errors = function()
    local size = require('internal.config.utils.size')

    local _, err
    _, err = size.parse(-1)
    t.assert_str_contains(err, 'Expected a non-negative number')

    _, err = size.parse('2.5MiB')
    t.assert_str_contains(err, 'without a fractional part')

    _, err = size.parse('10MB')
    t.assert_str_contains(err, 'Unknown size suffix')

    _, err = size.parse({})
    t.assert_str_contains(err, 'Expected a number or a string')
end
