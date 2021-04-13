#!/usr/bin/env tarantool

local clock = require("clock")
local fiber = require("fiber")
local test = require("tap").test("clock")

test:plan(36)

test:ok(clock.time() > 0, "time")
test:ok(clock.realtime() > 0, "realtime")
test:ok(clock.thread() > 0, "thread")
test:ok(clock.monotonic() > 0, "monotonic")
test:ok(clock.proc() > 0, "proc")
test:ok(fiber.time() > 0, "fiber.time")
test:ok(fiber.clock() > 0, "fiber.clock")
test:ok(clock.time64() > 0, "time64")
test:ok(clock.realtime64() > 0, "realtime64")
test:ok(clock.thread64() > 0, "thread64")
test:ok(clock.monotonic64() > 0, "monotonic64")
test:ok(clock.proc64() > 0, "proc64")
test:ok(fiber.time64() > 0, "fiber.time64")
test:ok(fiber.clock64() > 0, "fiber.clock64")

test:ok(clock.monotonic() <= clock.monotonic(), "time is monotonic")
test:ok(clock.monotonic64() <= clock.monotonic64(), "time is monotonic")
test:ok(math.abs(clock.realtime() - os.time()) < 2, "clock.realtime ~ os.time")

test:ok(fiber.time() == fiber.time(), "fiber.time is cached")
test:ok(fiber.time64() == fiber.time64(), "fiber.time64 is cached")

test:ok(fiber.clock() == fiber.clock(), "fiber.clock is cached")
test:ok(fiber.clock64() == fiber.clock64(), "fiber.clock64 is cached")
test:ok(fiber.clock() < (fiber.yield() or 0) + fiber.clock(),
        "fiber.clock is growing after yield")
test:ok(fiber.clock64() < (fiber.yield() or 0) + fiber.clock64(),
        "fiber.clock64 is growing after yield")

test:ok(math.abs(fiber.time() - tonumber(fiber.time64())/1e6) < 1,
        "fiber.time64 is in microseconds")
test:ok(math.abs(fiber.clock() - tonumber(fiber.clock64())/1e6) < 1,
        "fiber.clock64 is in microseconds")

test:ok(math.abs(clock.time() - tonumber(clock.time64())/1e9) < 1,
        "clock.time64 is in nanoseconds")
test:ok(math.abs(clock.realtime() - tonumber(clock.realtime64())/1e9) < 1,
        "clock.realtime64 is in nanoseconds")
test:ok(math.abs(clock.thread() - tonumber(clock.thread64())/1e9) < 1,
        "clock.thread64 is in nanoseconds")
test:ok(math.abs(clock.proc() - tonumber(clock.proc64())/1e9) < 1,
        "clock.proc64 is in nanoseconds")

local function subtract_future(func)
    local ts1 = func()
    fiber.sleep(0.001)
    return ts1 - func()
end
test:ok(subtract_future(clock.time64) < 0,
        "clock.time64() can be subtracted")
test:ok(subtract_future(clock.realtime64) < 0,
        "clock.realtime64() can be subtracted")
test:ok(subtract_future(clock.thread64) < 0,
        "clock.thread64() can be subtracted")
test:ok(subtract_future(clock.monotonic64) < 0,
        "clock.monotonic64() can be subtracted")
test:ok(subtract_future(clock.proc64) < 0,
        "clock.proc64() can be subtracted")
test:ok(subtract_future(fiber.time64) < 0,
        "fiber.time64 can be subtracted")
test:ok(subtract_future(fiber.clock64) < 0,
        "fiber.clock64 can be subtracted")

os.exit(test:check() and 0 or 1)
