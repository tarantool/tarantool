#!/usr/bin/env tarantool

local tap = require('tap')
local mytest = tap.test('key_def part count tests')

mytest:plan(3)

local key_def = require('key_def')
local kd = key_def.new({{fieldno = 1, type = 'unsigned'}})
local ok, res

-- Should succeed
ok, res = pcall(kd.compare_with_key, kd, {1}, {1})
mytest:ok(ok and res == 0, "Simple equality")

-- Should succeed
ok, res = pcall(kd.compare_with_key, kd, {1}, {2})
mytest:ok(ok and res < 0, "Simple inequality")

-- Should fail
local exp_err = "Invalid key part count (expected [0..1], got 9)"
ok, res = pcall(kd.compare_with_key, kd, {1}, {1, 2, 3, 4, 5, 6, 7, 8, 9})
mytest:is_deeply({ok, tostring(res)}, {false, exp_err},
    "Invalid key part count")

os.exit(mytest:check() and 0 or 1)
