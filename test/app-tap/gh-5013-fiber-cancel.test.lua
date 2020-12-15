#!/usr/bin/env tarantool

local tap = require('tap')
local fiber = require('fiber')
local test = tap.test("gh-5013-fiber-cancel")

test:plan(2)

local result = {}

local function test_f()
    local cond = fiber.cond()
    local res, err = pcall(cond.wait, cond)
    result.res = res
    result.err = err
end

local f = fiber.create(test_f)
f:cancel()
fiber.yield()

print(require('yaml').encode(result))

test:ok(result.res == false, 'expected result is false')
test:ok(tostring(result.err) == 'fiber is cancelled', 'fiber cancellation should be reported')
os.exit(test:check() and 0 or 1)
