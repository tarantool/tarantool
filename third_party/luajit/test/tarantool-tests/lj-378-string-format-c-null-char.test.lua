local tap = require('tap')

local test = tap.test('lj-378-string-format-c-null-char')
test:plan(1)

-- Test file to check that there is no regression in
-- `string.format('%c', 0)` behaviour.
-- See also https://github.com/LuaJIT/LuaJIT/issues/378.

test:is(string.format('%c', 0), '\0', 'string.format %c on null char')
test:done(true)
