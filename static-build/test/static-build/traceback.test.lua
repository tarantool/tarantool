#!/usr/bin/env tarantool

local tap = require('tap')
local fiber = require('fiber')

local test = tap.test('traceback')
test:plan(1)

local info = fiber.info()[fiber.id()]
test:ok(info.backtrace ~= nil, 'fiber.info() has backtrace')
for _, l in pairs(info.backtrace or {}) do
    test:diag('%s: %s', next(l))
end

os.exit(test:check() and 0 or 1)
