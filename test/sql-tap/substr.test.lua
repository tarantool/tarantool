#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(85)

--!./tcltestrunner.lua
-- 2007 May 14
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.  The
-- focus of this file is testing the built-in SUBSTR() functions.
--
-- $Id: substr.test,v 1.7 2009/02/03 13:10:54 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- Create a table to work with.
--
test:execsql [[
    CREATE TABLE t1(id integer primary key --autoincrement
    , t text, b SCALAR)
]]

local function substr_test(id, string, i1, i2, result)
    local sql = string.format([[
        DELETE FROM t1;
        INSERT INTO t1(id, t) VALUES(1, '%s')
    ]], string)

    test:execsql(sql)
    test:do_execsql_test(
        "substr-"..id..".1",
        string.format("SELECT SUBSTR(t, %s, %s) FROM t1", i1, i2),
        {result})
    --local qstr = string.gsub("' '", string)--"string","map","' ''",["string"]]]=]).."'"
    local qstr = string
    test:do_execsql_test(
        "substr-"..id..".2",
        string.format("SELECT SUBSTR('%s', %s, %s)",qstr, i1, i2),
        {result})
end

local function subblob_test(id, hex, i1, i2, hexresult)
    local sql = string.format([[
        DELETE FROM t1;
        INSERT INTO t1(id, b) VALUES(1, x'%s')
    ]], hex)

    test:execsql(sql)
    test:do_execsql_test(
        "substr-"..id..".1",
        string.format(
            "SELECT HEX(SUBSTR(CAST(b AS VARBINARY), %s, %s)) FROM t1", i1, i2),
        {hexresult})
    --local qstr = string.gsub("' '", string)--"string","map","' ''",["string"]]]=]).."'"
    test:do_execsql_test(
        "substr-"..id..".2",
        string.format("SELECT HEX(SUBSTR(x'%s', %s, %s))",
                      hex, i1, i2),
        {hexresult})
end

-- Basic SUBSTR functionality
--
substr_test("1.1", "abcdefg","1","1","a")
substr_test("1.2", "abcdefg","2","1","b")
substr_test("1.3", "abcdefg","1","2","ab")
substr_test("1.4", "abcdefg","1","100","abcdefg")
substr_test("1.5", "abcdefg","0","2","a")
substr_test("1.6", "abcdefg","-1","1","")
substr_test("1.7", "abcdefg","-1","10","abcdefg")
substr_test("1.8", "abcdefg","-5","3","")
substr_test("1.9", "abcdefg","-7","3","")
substr_test("1.10", "abcdefg","-100","98","")
substr_test("1.17", "abcdefg","100","200","")
substr_test("1.18", "abcdefg","200","100","")
-- Make sure NULL is returned if any parameter is NULL
--
test:do_test(
    "substr-1.90",
    function()
        return test:execsql "SELECT IFNULL(SUBSTR(NULL,1,1),'nil')"
    end, {
        -- <substr-1.90>
        "nil"
        -- </substr-1.90>
    })

test:do_test(
    "substr-1.91",
    function()
        return test:execsql "SELECT IFNULL(SUBSTR(NULL,1),'nil')"
    end, {
        -- <substr-1.91>
        "nil"
        -- </substr-1.91>
    })

test:do_test(
    "substr-1.92",
    function()
        return test:execsql "SELECT IFNULL(SUBSTR('abcdefg',NULL,1),'nil')"
    end, {
        -- <substr-1.92>
        "nil"
        -- </substr-1.92>
    })

test:do_test(
    "substr-1.93",
    function()
        return test:execsql "SELECT IFNULL(SUBSTR('abcdefg',NULL),'nil')"
    end, {
        -- <substr-1.93>
        "nil"
        -- </substr-1.93>
    })

test:do_test(
    "substr-1.94",
    function()
        return test:execsql "SELECT IFNULL(SUBSTR('abcdefg',1,NULL),'nil')"
    end, {
        -- <substr-1.94>
        "nil"
        -- </substr-1.94>
    })

-- Make sure everything works with long unicode characters
--
substr_test("2.1", "ሴ⍅㑖","1","1","ሴ")
substr_test("2.2", "ሴ⍅㑖","2","1","⍅")
substr_test("2.3", "ሴ⍅㑖","1","2","ሴ⍅")
substr_test("2.4", "ሴ⍅㑖","-1","1","")
substr_test("2.5", "aሴb⍅c㑖c","-5","3","")
-- Basic functionality for BLOBs
--
subblob_test("3.1", "61626364656667","1","1","61")
subblob_test("3.2", "61626364656667","2","1","62")
subblob_test("3.3", "61626364656667","1","2","6162")
subblob_test("3.4", "61626364656667","1","100","61626364656667")
subblob_test("3.5", "61626364656667", "0", "2", "61")
subblob_test("3.6", "61626364656667", "-1", "1", "")
subblob_test("3.7", "61626364656667", "-1", "10", "61626364656667")
subblob_test("3.8", "61626364656667", "-5", "3", "")
subblob_test("3.9", "61626364656667","-7","3", "")
subblob_test("3.10", "61626364656667", "-100", "98", "")
subblob_test("3.11", "61626364656667","100","200","")
subblob_test("3.12", "61626364656667","200","100","")
-- If these blobs were strings, then they would contain multi-byte
-- characters.  But since they are blobs, the substr indices refer
-- to bytes.
--
subblob_test("4.1", "61E188B462E28D8563E3919663","1","1","61")
subblob_test("4.2", "61E188B462E28D8563E3919663","2","1","E1")
subblob_test("4.3", "61E188B462E28D8563E3919663","1","2","61E1")
subblob_test("4.4", "61E188B462E28D8563E3919663", "-2", "1", "")
subblob_test("4.5", "61E188B462E28D8563E3919663", "-5", "4", "")
subblob_test("4.6", "61E188B462E28D8563E3919663", "-100", "98", "")
-- Two-argument SUBSTR
--
local function substr_2_test(id, string, idx, result)
    local sql = string.format([[
        DELETE FROM t1;
        INSERT INTO t1(id, t) VALUES(1, '%s')
    ]], string)

    test:execsql(sql)
    test:do_execsql_test(
        "substr-"..id..".1",
        string.format("SELECT SUBSTR(t,%s) FROM t1",idx),
        {result})
    --qstr = "'"..X(153, "X!cmd", [=[["string","map","' ''",["string"]]]=]).."'"
    local qstr = string
    test:do_execsql_test(
        "substr-"..id..".2",
        string.format("SELECT SUBSTR('%s',%s) FROM t1",qstr, idx),
        {result})
end

substr_2_test("5.1","abcdefghijklmnop","5","efghijklmnop")
substr_2_test("5.2","abcdef","-5","abcdef")

--
-- gh-4145: Make sure SUBSTR() throws an error if the third argument is
-- negative.
--
test:do_catchsql_test(
    "substr-6.1",
    [[
        SELECT SUBSTR('12345', 1, -1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[Length of the result cannot be less than 0]]
    }
)

test:do_catchsql_test(
    "substr-6.2",
    [[
        SELECT SUBSTR(x'3132333435', 1, -1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[Length of the result cannot be less than 0]]
    }
)

-- gh-4145: Make sure that SUBSTR() works according to ANSI.

--
-- Make sure SUBSTR() returns "" if the sum of the second and third arguments is
-- 1 or less.
--
test:do_execsql_test(
    "builtins-6.3",
    [[
        SELECT SUBSTR('asdfg', -10, 5), SUBSTR('asdfg', -4, 5);
    ]],
    {
        '', ''
    }
)

--
-- Make sure that if the sum of the second and third arguments is more than 1
-- and the second argument is negative, the result starts from the start of the
-- string and length of the result will be one less than sum of the  second and
-- third arguments.
--
test:do_execsql_test(
    "builtins-6.4",
    [[
        SELECT SUBSTR('123456789', -5, 10);
    ]],
    {
        '1234'
    }
)

-- Make sure SUBSTR() can work with big INTEGERs.
test:do_execsql_test(
    "builtins-6.5",
    [[
        SELECT SUBSTR('123456789', -9223372036854775808, 9223372036854775812);
    ]],
    {
        '123'
    }
)

test:do_execsql_test(
    "builtins-6.6",
    [[
        SELECT SUBSTR('123456789', 0, 18000000000000000000);
    ]],
    {
        '123456789'
    }
)

test:finish_test()
