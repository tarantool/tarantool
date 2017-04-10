#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test("string extensions")
test:plan(1)

-- gh-2214 - string.ljust()/string.rjust() Lua API
test:test("ljust/rjust/center", function(test)
    test:plan(15)
    test:is(("help"):ljust(0),  "help", "ljust, length 0, do nothing")
    test:is(("help"):rjust(0),  "help", "rjust, length 0, do nothing")
    test:is(("help"):center(0), "help", "center, length 0, do nothing")

    test:is(("help"):ljust(3),  "help", "ljust, length 3, do nothing")
    test:is(("help"):rjust(3),  "help", "rjust, length 3, do nothing")
    test:is(("help"):center(3), "help", "center, length 3, do nothing")

    test:is(("help"):ljust(5),  "help ", "ljust, length 5, one extra charachter")
    test:is(("help"):rjust(5),  " help", "rjust, length 5, one extra charachter")
    test:is(("help"):center(5), "help ", "center, length 5, one extra charachter")

    test:is(("help"):ljust(6),  "help  ", "ljust, length 6, two extra charachters")
    test:is(("help"):rjust(6),  "  help", "rjust, length 6, two extra charachters")
    test:is(("help"):center(6), " help ", "center, length 6, two extra charachters")

    test:is(("help"):ljust(6, '.'),  "help..", "ljust, length 6, two extra charachters, custom fill char")
    test:is(("help"):rjust(6, '.'),  "..help", "rjust, length 6, two extra charachters, custom fill char")
    test:is(("help"):center(6, '.'), ".help.", "center, length 6, two extra charachters, custom fill char")
end)

os.exit(test:check() == true and 0 or -1)
