local t = require('luatest')

-- XXX: This is temporary. The assertion is present in latest Luatest and
-- is making its way into Tarantool currently.
t.assert_error_covers = function(expected, fn, ...)
    local ok, actual = pcall(fn, ...)
    t.assert_not(ok)
    actual = actual:unpack()
    t.assert_equals(type(actual), 'table')
    t.assert_covers(actual, expected)
end
