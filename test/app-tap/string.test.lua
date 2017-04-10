#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test("string extensions")
test:plan(2)

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

test:test("split", function(test)
    test:plan(10)

    -- testing basic split (works over gsplit)
    test:is_deeply((""):split(""), {""},   "empty split")
    test:is_deeply((""):split("z"), {""},  "empty split")
    test:is_deeply(("a"):split(""), {"a"}, "empty split")
    test:is_deeply(("a"):split("a"), {"", ""}, "split self")
    test:is_deeply(
        (" 1 2  3  "):split(),
        {"1", "2", "3"},
        "complex split on empty separator"
    )
    test:is_deeply(
        (" 1 2  3  "):split(" "),
        {"", "1", "2", "", "3", "", ""},
        "complex split on space separator"
    )
    test:is_deeply(
        (" 1 2  \n\n\n\r\t\n3  "):split(),
        {"1", "2", "3"},
        "complex split on empty separator"
    )
    test:is_deeply(
        ("a*bb*c*ddd"):split("*"),
        {"a", "bb", "c", "ddd"},
        "another * separator"
    )
    test:is_deeply(
        ("dog:fred:bonzo:alice"):split(":", 2),
        {"dog", "fred", "bonzo:alice"},
        "testing max separator"
    )
    test:is_deeply(
        ("///"):split("/"),
        {"", "", "", ""},
        "testing splitting on one char"
    )
end)
