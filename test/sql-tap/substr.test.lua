#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(93)

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
        string.format("SELECT substr(t, %s, %s) FROM t1", i1, i2),
        {result})
    --local qstr = string.gsub("' '", string)--"string","map","' ''",["string"]]]=]).."'"
    local qstr = string
    test:do_execsql_test(
        "substr-"..id..".2",
        string.format("SELECT substr('%s', %s, %s)",qstr, i1, i2),
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
        string.format("SELECT hex(substr(b, %s, %s)) FROM t1", i1, i2),
        {hexresult})
    --local qstr = string.gsub("' '", string)--"string","map","' ''",["string"]]]=]).."'"
    test:do_execsql_test(
        "substr-"..id..".2",
        string.format("SELECT hex(substr(x'%s', %s, %s))",hex, i1, i2),
        {hexresult})
end

-- Basic SUBSTR functionality
--
substr_test("1.1", "abcdefg","1","1","a")
substr_test("1.2", "abcdefg","2","1","b")
substr_test("1.3", "abcdefg","1","2","ab")
substr_test("1.4", "abcdefg","1","100","abcdefg")
substr_test("1.5", "abcdefg","0","2","a")
substr_test("1.6", "abcdefg","-1","1","g")
substr_test("1.7", "abcdefg","-1","10","g")
substr_test("1.8", "abcdefg","-5","3","cde")
substr_test("1.9", "abcdefg","-7","3","abc")
substr_test("1.10", "abcdefg","-100","98","abcde")
substr_test("1.11", "abcdefg","5","-1","d")
substr_test("1.12", "abcdefg","5","-4","abcd")
substr_test("1.13", "abcdefg","5","-5","abcd")
substr_test("1.14", "abcdefg","-5","-1","b")
substr_test("1.15", "abcdefg","-5","-2","ab")
substr_test("1.16", "abcdefg","-5","-3","ab")
substr_test("1.17", "abcdefg","100","200","")
substr_test("1.18", "abcdefg","200","100","")
-- Make sure NULL is returned if any parameter is NULL
--
test:do_test(
    "substr-1.90",
    function()
        return test:execsql "SELECT ifnull(substr(NULL,1,1),'nil')"
    end, {
        -- <substr-1.90>
        "nil"
        -- </substr-1.90>
    })

test:do_test(
    "substr-1.91",
    function()
        return test:execsql "SELECT ifnull(substr(NULL,1),'nil')"
    end, {
        -- <substr-1.91>
        "nil"
        -- </substr-1.91>
    })

test:do_test(
    "substr-1.92",
    function()
        return test:execsql "SELECT ifnull(substr('abcdefg',NULL,1),'nil')"
    end, {
        -- <substr-1.92>
        "nil"
        -- </substr-1.92>
    })

test:do_test(
    "substr-1.93",
    function()
        return test:execsql "SELECT ifnull(substr('abcdefg',NULL),'nil')"
    end, {
        -- <substr-1.93>
        "nil"
        -- </substr-1.93>
    })

test:do_test(
    "substr-1.94",
    function()
        return test:execsql "SELECT ifnull(substr('abcdefg',1,NULL),'nil')"
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
substr_test("2.4", "ሴ⍅㑖","-1","1","㑖")
substr_test("2.5", "aሴb⍅c㑖c","-5","3","b⍅c")
substr_test("2.6", "aሴb⍅c㑖c","-2","-3","b⍅c")
-- Basic functionality for BLOBs
--
subblob_test("3.1", "61626364656667","1","1","61")
subblob_test("3.2", "61626364656667","2","1","62")
subblob_test("3.3", "61626364656667","1","2","6162")
subblob_test("3.4", "61626364656667","1","100","61626364656667")
subblob_test("3.5", "61626364656667","0","2","61")
subblob_test("3.6", "61626364656667","-1","1","67")
subblob_test("3.7", "61626364656667","-1","10","67")
subblob_test("3.8", "61626364656667","-5","3","636465")
subblob_test("3.9", "61626364656667","-7","3","616263")
subblob_test("3.10", "61626364656667","-100","98","6162636465")
subblob_test("3.11", "61626364656667","100","200","")
subblob_test("3.12", "61626364656667","200","100","")
-- If these blobs were strings, then they would contain multi-byte
-- characters.  But since they are blobs, the substr indices refer
-- to bytes.
--
subblob_test("4.1", "61E188B462E28D8563E3919663","1","1","61")
subblob_test("4.2", "61E188B462E28D8563E3919663","2","1","E1")
subblob_test("4.3", "61E188B462E28D8563E3919663","1","2","61E1")
subblob_test("4.4", "61E188B462E28D8563E3919663","-2","1","96")
subblob_test("4.5", "61E188B462E28D8563E3919663","-5","4","63E39196")
subblob_test("4.6", "61E188B462E28D8563E3919663","-100","98","61E188B462E28D8563E391")
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
        string.format("SELECT substr(t,%s) FROM t1",idx),
        {result})
    --qstr = "'"..X(153, "X!cmd", [=[["string","map","' ''",["string"]]]=]).."'"
    local qstr = string
    test:do_execsql_test(
        "substr-"..id..".2",
        string.format("SELECT substr('%s',%s) FROM t1",qstr, idx),
        {result})
end

substr_2_test("5.1","abcdefghijklmnop","5","efghijklmnop")
substr_2_test("5.2","abcdef","-5","bcdef")


test:finish_test()
