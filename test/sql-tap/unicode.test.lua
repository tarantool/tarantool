#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(23 * 3)

-- 23 entities
local utf8_spaces = {"\u{0009}", "\u{000A}", "\u{000B}", "\u{000C}", "\u{000D}",
                     "\u{0085}", "\u{1680}", "\u{2000}", "\u{2001}", "\u{2002}",
                     "\u{2003}", "\u{2004}", "\u{2005}", "\u{2006}", "\u{2007}",
                     "\u{2008}", "\u{2009}", "\u{200A}", "\u{2028}", "\u{2029}",
                     "\u{202F}", "\u{205F}", "\u{3000}"}
local spaces_cnt = 23

-- 1. Check UTF-8 single space
for i, v in pairs(utf8_spaces) do
    test:do_execsql_test(
        "utf8-spaces-1."..i,
        "select" .. v .. "1",
        { 1 })
end

-- 2. Check pair simple + UTF-8 space
for i, v in pairs(utf8_spaces) do
    test:do_execsql_test(
        "utf8-spaces-2."..i,
        "select" .. v .. "1",
        { 1 })
end

-- 3. Sequence of spaces
for i, v in pairs(utf8_spaces) do
    test:do_execsql_test(
        "utf8-spaces-3."..i,
        "select" .. v .. " " .. utf8_spaces[spaces_cnt - i + 1] .. " 1",
        { 1 })
end

test:finish_test()
