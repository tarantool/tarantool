#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test("string extensions")

test:plan(6)

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

test:test("unicode", function(test)
    test:plan(102)
    local str = 'хеЛлоу вОрЛд ё Ё я Я э Э ъ Ъ hElLo WorLd 1234 i I İ 勺#☢༺'
    local upper_res = 'ХЕЛЛОУ ВОРЛД Ё Ё Я Я Э Э Ъ Ъ HELLO WORLD 1234 I I İ 勺#☢༺'
    local lower_res = 'хеллоу ворлд ё ё я я э э ъ ъ hello world 1234 i i i̇ 勺#☢༺'
    local s = utf8.upper(str)
    test:is(s, upper_res, 'default locale upper')
    s = utf8.lower(str)
    test:is(s, lower_res, 'default locale lower')
    test:is(utf8.upper(''), '', 'empty string upper')
    test:is(utf8.lower(''), '', 'empty string lower')
    local err
    s, err = pcall(utf8.upper, true)
    test:isnt(err:find('Usage'), nil, 'upper usage is checked')
    s, err = pcall(utf8.lower, true)
    test:isnt(err:find('Usage'), nil, 'lower usage is checked')

    test:is(utf8.isupper('a'), false, 'isupper("a")')
    test:is(utf8.isupper('A'), true, 'isupper("A")')
    test:is(utf8.islower('a'), true, 'islower("a")')
    test:is(utf8.islower('A'), false, 'islower("A")')
    test:is(utf8.isalpha('a'), true, 'isalpha("a")')
    test:is(utf8.isalpha('A'), true, 'isalpha("A")')
    test:is(utf8.isalpha('aa'), false, 'isalpha("aa")')
    test:is(utf8.isalpha('勺'), true, 'isalpha("勺")')
    test:is(utf8.isupper('Ё'), true, 'isupper("Ё")')
    test:is(utf8.islower('ё'), true, 'islower("ё")')
    test:is(utf8.isdigit('a'), false, 'isdigit("a")')
    test:is(utf8.isdigit('1'), true, 'isdigit("1")')
    test:is(utf8.isdigit('9'), true, 'isdigit("9")')

    test:is(utf8.len(str), 56, 'len works on complex string')
    s = '12İ☢勺34'
    test:is(utf8.len(s), 7, 'len works no options')
    test:is(utf8.len(s, 1), 7, 'default start is 1')
    test:is(utf8.len(s, 2), 6, 'start 2')
    test:is(utf8.len(s, 3), 5, 'start 3')
    local c
    c, err = utf8.len(s, 4)
    test:isnil(c, 'middle of symbol offset is error')
    test:is(err, 4, 'error on 4 byte')
    test:is(utf8.len(s, 5), 4, 'start 5')
    c, err = utf8.len(s, 6)
    test:is(err, 6, 'error on 6 byte')
    c, err = utf8.len(s, 0)
    test:is(err, 'position is out of string', 'range is out of string')
    test:is(utf8.len(s, #s), 1, 'start from the end')
    test:is(utf8.len(s, #s + 1), 0, 'position is out of string')
    test:is(utf8.len(s, 1, -1), 7, 'default end is -1')
    test:is(utf8.len(s, 1, -2), 6, 'end -2')
    test:is(utf8.len(s, 1, -3), 5, 'end -3')
    test:is(utf8.len(s, 1, -4), 5, 'end in the middle of symbol')
    test:is(utf8.len(s, 1, -5), 5, 'end in the middle of symbol')
    test:is(utf8.len(s, 1, -6), 5, 'end in the middle of symbol')
    test:is(utf8.len(s, 1, -7), 4, 'end -7')
    test:is(utf8.len(s, 2, -7), 3, '[2, -7]')
    test:is(utf8.len(s, 3, -7), 2, '[3, -7]')
    c, err = utf8.len(s, 4, -7)
    test:is(err, 4, '[4, -7] is error - start from the middle of symbol')
    test:is(utf8.len(s, 10, -100), 0, 'it is ok to be out of str by end pos')
    test:is(utf8.len(s, 10, -10), 0, 'it is ok to swap end and start pos')
    test:is(utf8.len(''), 0, 'empty len')
    test:is(utf8.len(s, -6, -1), 3, 'pass both negative offsets')
    test:is(utf8.len(s, 3, 3), 1, "end in the middle on the same symbol as start")
    c, err = utf8.len('a\xF4')
    test:is(err, 2, "invalid unicode in the middle of the string")

    local chars = {}
    local codes = {}
    for _, code in utf8.next, s do
        table.insert(chars, utf8.char(code))
        table.insert(codes, code)
    end
    test:is(table.concat(chars), s, "next and char works")
    c, err = pcall(utf8.char, 'kek')
    test:isnt(err:find('bad argument'), nil, 'char usage is checked')
    c, err = pcall(utf8.next, true)
    test:isnt(err:find('Usage'), nil, 'next usage is checked')
    c, err = pcall(utf8.next, '1234', true)
    test:isnt(err:find('bad argument'), nil, 'next usage is checked')
    local offset
    offset, c = utf8.next('')
    test:isnil(offset, 'next on empty - nil offset')
    test:isnil(c, 'next on empty - nil code')
    offset, c = utf8.next('123', 100)
    test:isnil(offset, 'out of string - nil offset')
    test:isnil(c, 'out of string - nil code')
    test:is(utf8.char(unpack(codes)), s, 'char with multiple values')

    local uppers = 0
    local lowers = 0
    local digits = 0
    local letters = 0
    for _, code in utf8.next, str do
        if utf8.isupper(code) then uppers = uppers + 1 end
        if utf8.islower(code) then lowers = lowers + 1 end
        if utf8.isalpha(code) then letters = letters + 1 end
        if utf8.isdigit(code) then digits = digits + 1 end
    end
    test:is(uppers, 13, 'uppers by code')
    test:is(lowers, 19, 'lowers by code')
    test:is(letters, 33, 'letters by code')
    test:is(digits, 4, 'digits by code')

    s = '12345678'
    test:is(utf8.sub(s, 1, 1), '1', 'sub [1]')
    test:is(utf8.sub(s, 1, 2), '12', 'sub [1:2]')
    test:is(utf8.sub(s, 2, 2), '2', 'sub [2:2]')
    test:is(utf8.sub(s, 0, 2), '12', 'sub [0:2]')
    test:is(utf8.sub(s, 3, 7), '34567', 'sub [3:7]')
    test:is(utf8.sub(s, 7, 3), '', 'sub [7:3]')
    test:is(utf8.sub(s, 3, 100), '345678', 'sub [3:100]')
    test:is(utf8.sub(s, 100, 3), '', 'sub [100:3]')

    test:is(utf8.sub(s, 5), '5678', 'sub [5:]')
    test:is(utf8.sub(s, 1, -1), s, 'sub [1:-1]')
    test:is(utf8.sub(s, 1, -2), '1234567', 'sub [1:-2]')
    test:is(utf8.sub(s, 2, -2), '234567', 'sub [2:-2]')
    test:is(utf8.sub(s, 3, -3), '3456', 'sub [3:-3]')
    test:is(utf8.sub(s, 5, -4), '5', 'sub [5:-4]')
    test:is(utf8.sub(s, 7, -7), '', 'sub[7:-7]')

    test:is(utf8.sub(s, -2, -1), '78', 'sub [-2:-1]')
    test:is(utf8.sub(s, -1, -1), '8', 'sub [-1:-1]')
    test:is(utf8.sub(s, -4, -2), '567', 'sub [-4:-2]')
    test:is(utf8.sub(s, -400, -2), '1234567', 'sub [-400:-2]')
    test:is(utf8.sub(s, -3, -5), '', 'sub [-3:-5]')

    test:is(utf8.sub(s, -6, 5), '345', 'sub [-6:5]')
    test:is(utf8.sub(s, -5, 4), '4', 'sub [-5:4]')
    test:is(utf8.sub(s, -2, 2), '', 'sub [-2:2]')
    test:is(utf8.sub(s, -1, 8), '8', 'sub [-1:8]')

    c, err = pcall(utf8.sub)
    test:isnt(err:find('Usage'), nil, 'usage is checked')
    c, err = pcall(utf8.sub, true)
    test:isnt(err:find('Usage'), nil, 'usage is checked')
    c, err = pcall(utf8.sub, '123')
    test:isnt(err:find('Usage'), nil, 'usage is checked')
    c, err = pcall(utf8.sub, '123', true)
    test:isnt(err:find('bad argument'), nil, 'usage is checked')
    c, err = pcall(utf8.sub, '123', 1, true)
    test:isnt(err:find('bad argument'), nil, 'usage is checked')

    local s1 = '☢'
    local s2 = 'İ'
    test:is(s1 < s2, false, 'test binary cmp')
    test:is(utf8.cmp(s1, s2) < 0, true, 'test unicode <')
    test:is(utf8.cmp(s1, s1) == 0, true, 'test unicode eq')
    test:is(utf8.cmp(s2, s1) > 0, true, 'test unicode >')
    test:is(utf8.casecmp('a', 'A') == 0, true, 'test icase ==')
    test:is(utf8.casecmp('b', 'A') > 0, true, 'test icase >, first')
    test:is(utf8.casecmp('B', 'a') > 0, true, 'test icase >, second >')
    test:is(utf8.cmp('', '') == 0, true, 'test empty compare')
    test:is(utf8.cmp('', 'a') < 0, true, 'test left empty compare')
    test:is(utf8.cmp('a', '') > 0, true, 'test right empty compare')
    test:is(utf8.casecmp('', '') == 0, true, 'test empty icompare')
    test:is(utf8.casecmp('', 'a') < 0, true, 'test left empty icompare')
    test:is(utf8.casecmp('a', '') > 0, true, 'test right empty icompare')
end)

os.exit(test:check() == true and 0 or -1)
