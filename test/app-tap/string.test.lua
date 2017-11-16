#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test("string extensions")

test:plan(5)

test:test("split", function(test)
    test:plan(10)

    -- testing basic split (works over gsplit)
    test:ok(not pcall(string.split, "", ""), "empty separator")
    test:ok(not pcall(string.split, "a", ""), "empty separator")
    test:is_deeply((""):split("z"), {""},  "empty split")
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

-- gh-2214 - string.ljust()/string.rjust() Lua API
test:test("ljust/rjust/center", function(test)
    test:plan(18)

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
    local errmsg = "%(char expected, got string%)"
    local _, err = pcall(function() ("help"):ljust(6, "XX") end)
    test:ok(err and err:match(errmsg), "wrong params")
    _, err = pcall(function() ("help"):rjust(6, "XX") end)
    test:ok(err and err:match(errmsg), "wrong params")
    _, err = pcall(function() ("help"):center(6, "XX") end)
    test:ok(err and err:match(errmsg), "wrong params")
end)

-- gh-2215 - string.startswith()/string.endswith() Lua API
test:test("startswith/endswith", function(test)
    test:plan(21)

    test:ok((""):startswith(""),      "empty+empty startswith")
    test:ok((""):endswith(""),        "empty+empty endswith")
    test:ok(not (""):startswith("a"), "empty+non-empty startswith")
    test:ok(not (""):endswith("a"),   "empty+non-empty endswith")
    test:ok(("a"):startswith(""),     "non-empty+empty startswith")
    test:ok(("a"):endswith(""),       "non-empty+empty endswith")

    test:ok(("12345"):startswith("123")            , "simple startswith")
    test:ok(("12345"):startswith("123", 1, 5)      , "startswith with good begin/end")
    test:ok(("12345"):startswith("123", 1, 3)      , "startswith with good begin/end")
    test:ok(("12345"):startswith("123", -5, 3)     , "startswith with good negative begin/end")
    test:ok(("12345"):startswith("123", -5, -3)    , "startswith with good negative begin/end")
    test:ok(not ("12345"):startswith("123", 2, 5)  , "bad startswith with good begin/end")
    test:ok(not ("12345"):startswith("123", 1, 2)  , "bad startswith with good begin/end")

    test:ok(("12345"):endswith("345")              , "simple endswith")
    test:ok(("12345"):endswith("345", 1, 5)        , "endswith with good begin/end")
    test:ok(("12345"):endswith("345", 3, 5)        , "endswith with good begin/end")
    test:ok(("12345"):endswith("345", -3, 5)       , "endswith with good begin/end")
    test:ok(("12345"):endswith("345", -3, -1)      , "endswith with good begin/end")
    test:ok(not ("12345"):endswith("345", 1, 4)    , "bad endswith with good begin/end")
    test:ok(not ("12345"):endswith("345", 4, 5)    , "bad endswith with good begin/end")

    local _, err = pcall(function() ("help"):startswith({'n', 1}) end)
    test:ok(err and err:match("%(string expected, got table%)"), "wrong params")
end)

test:test("hex", function(test)
    test:plan(2)
    test:is(string.hex("hello"), "68656c6c6f", "hex non-empty string")
    test:is(string.hex(""), "", "hex empty string")
end)

test:test("strip", function(test)
    test:plan(6)
    local str = "  hello hello "
    test:is(string.len(string.strip(str)), 11, "strip")
    test:is(string.len(string.lstrip(str)), 12, "lstrip")
    test:is(string.len(string.rstrip(str)), 13, "rstrip")
    local _, err = pcall(string.strip, 12)
    test:ok(err and err:match("%(string expected, got number%)"))
    _, err = pcall(string.lstrip, 12)
    test:ok(err and err:match("%(string expected, got number%)"))
    _, err = pcall(string.rstrip, 12)
    test:ok(err and err:match("%(string expected, got number%)"))
end )

os.exit(test:check() == true and 0 or -1)
