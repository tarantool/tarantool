local t = require('luatest')
local v = require('version')

local g = t.group()

g.test_version_new = function()
    -- Test, that the format of every arg is validated.
    local err_template = '%s should be a number'
    local err = string.format(err_template, 'major')
    t.assert_error_msg_contains(err, function() v.new('string') end)
    err = string.format(err_template, 'minor')
    t.assert_error_msg_contains(err, function() v.new(3, {}) end)
    err = string.format(err_template, 'patch')
    t.assert_error_msg_contains(err, function() v.new(3, 2, nil) end)

    -- Basic version.
    local a = v.new(1, 2, 3)
    t.assert_equals(a, v(1, 2, 3))
    t.assert_equals(a.major, 1)
    t.assert_equals(a.minor, 2)
    t.assert_equals(a.patch, 3)
end

g.test_version_from_string = function()
    -- Test, that parsing returns error.
    local err = 'error during parsing version string'
    local broken_versions_strs = {
        '1',
        '1.2',
        '1.2.a',
        'b.2.3',
        'string1.2.3',
        '1.2.3.4',
        '1.2.3-string.',
    }
    for _, str in pairs(broken_versions_strs) do
        t.assert_error_msg_contains(err, function() v.fromstr(str) end)
    end

    -- Basic test of the conversion from string.
    local version_str = '5.4.3'
    local a = v.fromstr(version_str)
    t.assert_equals(a.major, 5)
    t.assert_equals(a.minor, 4)
    t.assert_equals(a.patch, 3)
    -- fromstr and __tostring give same results, when basic version is used.
    t.assert_equals(tostring(a), version_str)

    -- All additional symbols are ignored (if they don't include dot).
    version_str = '3.3.0-entrypoint-163-gb33f17b25d'
    t.assert_equals(v.fromstr(version_str), v(3, 3, 0))
end

g.test_version_cmp = function()
    -- This test intentionally uses relational methamethods and not the
    -- luatest compare functions. Just make sure, that id properly works.
    local a = v(1, 2, 3)
    local b = v(3, 0, 0)

    t.assert(a <= a)
    t.assert(a >= a)
    t.assert(a == a)
    t.assert(a ~= b)
    t.assert(a < b)
    t.assert(a <= b)
    t.assert_not(a >= b)
end
