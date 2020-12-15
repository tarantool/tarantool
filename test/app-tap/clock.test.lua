#!/usr/bin/env tarantool

local clock = require("clock")
local test = require("tap").test("csv")
test:plan(10)
test:ok(clock.realtime() > 0, "realtime")
test:ok(clock.thread() > 0, "thread")
test:ok(clock.monotonic() > 0, "monotonic")
test:ok(clock.proc() > 0, "proc")
test:ok(clock.realtime64() > 0, "realtime64")
test:ok(clock.thread64() > 0, "thread64")
test:ok(clock.monotonic64() > 0, "monotonic64")
test:ok(clock.proc64() > 0, "proc64")

test:ok(clock.monotonic() <= clock.monotonic(), "time is monotonic")
test:ok(math.abs(clock.realtime() - os.time()) < 2, "clock.realtime ~ os.time")
