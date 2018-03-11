#!/usr/bin/env tarantool

-- Miscellaneous test for LuaJIT bugs
tap = require('tap')

test = tap.test("gh")
test:plan(2)
--
-- gh-3196: incorrect string length if Lua hash returns 0
--
h = "\x1F\x93\xE2\x1C\xCA\xDE\x28\x08\x26\x01\xED\x0A\x2F\xE4\x21\x02\x97\x77\xD9\x3E"
test:is(h:len(), 20)

h = "\x0F\x93\xE2\x1C\xCA\xDE\x28\x08\x26\x01\xED\x0A\x2F\xE4\x21\x02\x97\x77\xD9\x3E"
test:is(h:len(), 20)

test:check()
