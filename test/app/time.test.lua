#!/usr/bin/env tarantool

time = require("time")
test = require("tap").test("csv")
test:plan(9)
test:ok(time.realtime() > 0, "realtime")
test:ok(time.thread() > 0, "thread")
test:ok(time.monotonic() > 0, "monotonic")
test:ok(time.proc() > 0, "proc")
test:ok(time.realtime64() > 0, "realtime64")
test:ok(time.thread64() > 0, "thread64")
test:ok(time.monotonic64() > 0, "monotonic64")
test:ok(time.proc64() > 0, "proc64")

test:ok(time.monotonic() < time.monotonic(), "time is monotonic")
test:ok(math.abs(time.realtime() - os.time()) < 2, "time.realtime ~ os.time")
