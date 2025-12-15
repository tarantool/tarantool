-- Miscellaneous test for LuaJIT bugs
local tap = require('tap')

local test = tap.test("gh-3196-incorrect-string-length")
test:plan(2)
--
-- gh-3196: incorrect string length if Lua hash returns 0
--
-- luacheck: push no max_line_length
--
local h = "\x1F\x93\xE2\x1C\xCA\xDE\x28\x08\x26\x01\xED\x0A\x2F\xE4\x21\x02\x97\x77\xD9\x3E"
test:is(h:len(), 20)

h = "\x0F\x93\xE2\x1C\xCA\xDE\x28\x08\x26\x01\xED\x0A\x2F\xE4\x21\x02\x97\x77\xD9\x3E"
test:is(h:len(), 20)
-- luacheck: pop

test:done(true)
