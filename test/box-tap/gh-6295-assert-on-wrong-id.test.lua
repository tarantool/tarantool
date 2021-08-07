#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('gh-6295-assert-on-wrong-id')

test:plan(5)

local ok, res

box.cfg{}

-- Should be an error, not an assertion.
local _priv = box.space._priv
local errmsg = "Function '1000000' does not exist"
ok, res = pcall(_priv.replace, _priv, {1, 2, 'function', 1000000, box.priv.A})
test:is_deeply({ok, tostring(res)}, {false, errmsg}, "Proper error is returned")

errmsg = "Sequence '1000000' does not exist"
ok, res = pcall(_priv.replace, _priv, {1, 2, 'sequence', 1000000, box.priv.A})
test:is_deeply({ok, tostring(res)}, {false, errmsg}, "Proper error is returned")

errmsg = "Space '1000000' does not exist"
ok, res = pcall(_priv.replace, _priv, {1, 2, 'space', 1000000, box.priv.A})
test:is_deeply({ok, tostring(res)}, {false, errmsg}, "Proper error is returned")

errmsg = "User '1000000' is not found"
ok, res = pcall(_priv.replace, _priv, {1, 2, 'user', 1000000, box.priv.A})
test:is_deeply({ok, tostring(res)}, {false, errmsg}, "Proper error is returned")

errmsg = "Role '1000000' is not found"
ok, res = pcall(_priv.replace, _priv, {1, 2, 'role', 1000000, box.priv.A})
test:is_deeply({ok, tostring(res)}, {false, errmsg}, "Proper error is returned")

os.exit(test:check() and 0 or 1)
