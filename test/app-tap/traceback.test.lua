#!/usr/bin/env tarantool

local tap = require('tap')
local tarantool = require('tarantool')
local fiber = require('fiber')

local test = tap.test('traceback')

local _, _, enable_bt = string.find(tarantool.build.options,
                                    '-DENABLE_BACKTRACE=(%a+)')
if enable_bt == 'FALSE' or enable_bt == 'OFF' then
    test:plan(1)
    test:skip('backtrace is disabled')
    os.exit(0)
end

test:plan(1)

local info = fiber.info()[fiber.id()]
test:ok(info.backtrace ~= nil, 'fiber.info() has backtrace')
for _, l in pairs(info.backtrace or {}) do
    test:diag('%s: %s', next(l))
end

os.exit(test:check() and 0 or 1)
