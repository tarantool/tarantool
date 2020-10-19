#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(23)

--!./tcltestrunner.lua
-- 2007 May 15
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.
--
-- This file checks to make sure sql is able to gracefully
-- handle malformed UTF-8.
--
-- $Id: badutf.test,v 1.2 2007/09/12 17:01:45 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "badutf-1.1",
    function()
        --test:execsql "PRAGMA encoding='UTF8'"
        return test:execsql2("SELECT hex('\x80') AS x")
    end, {
        -- <badutf-1.1>
        "X", "80"
        -- </badutf-1.1>
    })

test:do_test(
    "badutf-1.2",
    function()
        return test:execsql2("SELECT hex('\x81') AS x")
    end, {
        -- <badutf-1.2>
        "X", "81"
        -- </badutf-1.2>
    })

test:do_test(
    "badutf-1.3",
    function()
        return test:execsql2("SELECT hex('\xbf') AS x")
    end, {
        -- <badutf-1.3>
        "X", "BF"
        -- </badutf-1.3>
    })

test:do_test(
    "badutf-1.4",
    function()
        return test:execsql2("SELECT hex('\xc0') AS x")
    end, {
        -- <badutf-1.4>
        "X", "C0"
        -- </badutf-1.4>
    })

test:do_test(
    "badutf-1.5",
    function()
        return test:execsql2("SELECT hex('\xe0') AS x")
    end, {
        -- <badutf-1.5>
        "X", "E0"
        -- </badutf-1.5>
    })

test:do_test(
    "badutf-1.6",
    function()
        return test:execsql2("SELECT hex('\xf0') AS x")
    end, {
        -- <badutf-1.6>
        "X", "F0"
        -- </badutf-1.6>
    })

test:do_test(
    "badutf-1.7",
    function()
        return test:execsql2("SELECT hex('\xff') AS x")
    end, {
        -- <badutf-1.7>
        "X", "FF"
        -- </badutf-1.7>
    })

-- commented as it uses utf16
if 0>0 then
sql("db2", "")
test:do_test(
    "badutf-1.10",
    function()
        test:execsql "PRAGMA encoding='UTF16be'"
        return sql_exec("db2", "SELECT hex('%80') AS x")
    end, {
        -- <badutf-1.10>
        0, "x 0080"
        -- </badutf-1.10>
    })

test:do_test(
    "badutf-1.11",
    function()
        return sql_exec("db2", "SELECT hex('%81') AS x")
    end, {
        -- <badutf-1.11>
        0, "x 0081"
        -- </badutf-1.11>
    })

test:do_test(
    "badutf-1.12",
    function()
        return sql_exec("db2", "SELECT hex('%bf') AS x")
    end, {
        -- <badutf-1.12>
        0, "x 00BF"
        -- </badutf-1.12>
    })

test:do_test(
    "badutf-1.13",
    function()
        return sql_exec("db2", "SELECT hex('%c0') AS x")
    end, {
        -- <badutf-1.13>
        0, "x FFFD"
        -- </badutf-1.13>
    })

test:do_test(
    "badutf-1.14",
    function()
        return sql_exec("db2", "SELECT hex('%c1') AS x")
    end, {
        -- <badutf-1.14>
        0, "x FFFD"
        -- </badutf-1.14>
    })

test:do_test(
    "badutf-1.15",
    function()
        return sql_exec("db2", "SELECT hex('%c0%bf') AS x")
    end, {
        -- <badutf-1.15>
        0, "x FFFD"
        -- </badutf-1.15>
    })

test:do_test(
    "badutf-1.16",
    function()
        return sql_exec("db2", "SELECT hex('%c1%bf') AS x")
    end, {
        -- <badutf-1.16>
        0, "x FFFD"
        -- </badutf-1.16>
    })

test:do_test(
    "badutf-1.17",
    function()
        return sql_exec("db2", "SELECT hex('%c3%bf') AS x")
    end, {
        -- <badutf-1.17>
        0, "x 00FF"
        -- </badutf-1.17>
    })

test:do_test(
    "badutf-1.18",
    function()
        return sql_exec("db2", "SELECT hex('%e0') AS x")
    end, {
        -- <badutf-1.18>
        0, "x FFFD"
        -- </badutf-1.18>
    })

test:do_test(
    "badutf-1.19",
    function()
        return sql_exec("db2", "SELECT hex('%f0') AS x")
    end, {
        -- <badutf-1.19>
        0, "x FFFD"
        -- </badutf-1.19>
    })

test:do_test(
    "badutf-1.20",
    function()
        return sql_exec("db2", "SELECT hex('%ff') AS x")
    end, {
        -- <badutf-1.20>
        0, "x FFFD"
        -- </badutf-1.20>
    })
end



test:do_test(
    "badutf-3.1",
    function()
        return test:execsql2("SELECT length('\x80') AS x")
    end, {
        -- <badutf-3.1>
        "X", 1
        -- </badutf-3.1>
    })

test:do_test(
    "badutf-3.2",
    function()
        return test:execsql2("SELECT length('\x61\x62\x63') AS x")
    end, {
        -- <badutf-3.2>
        "X", 3
        -- </badutf-3.2>
    })

test:do_test(
    "badutf-3.3",
    function()
        return test:execsql2("SELECT length('\x7f\x80\x81') AS x")
    end, {
        -- <badutf-3.3>
        "X", 3
        -- </badutf-3.3>
    })

test:do_test(
    "badutf-3.4",
    function()
        return test:execsql2("SELECT length('\x61\xc0') AS x")
    end, {
        -- <badutf-3.4>
        "X", 2
        -- </badutf-3.4>
    })

test:do_test(
    "badutf-3.5",
    function()
        return test:execsql2("SELECT length('\x61\xc0\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80') AS x")
    end, {
        -- <badutf-3.5>
        "X", 12
        -- </badutf-3.5>
    })

test:do_test(
    "badutf-3.6",
    function()
        return test:execsql2("SELECT length('\xc0\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80') AS x")
    end, {
        -- <badutf-3.6>
        "X", 11
        -- </badutf-3.6>
    })

test:do_test(
    "badutf-3.7",
    function()
        return test:execsql2("SELECT length('\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80') AS x")
    end, {
        -- <badutf-3.7>
        "X", 10
        -- </badutf-3.7>
    })

test:do_test(
    "badutf-3.8",
    function()
        return test:execsql2("SELECT length('\x80\x80\x80\x80\x80\xf0\x80\x80\x80\x80') AS x")
    end, {
        -- <badutf-3.8>
        "X", 7
        -- </badutf-3.8>
    })

test:do_test(
    "badutf-3.9",
    function()
        return test:execsql2("SELECT length('\x80\x80\x80\x80\x80\xf0\x80\x80\x80\xff') AS x")
    end, {
        -- <badutf-3.9>
        "X", 7
        -- </badutf-3.9>
    })

test:do_test(
    "badutf-4.1",
    function()
        return test:execsql2("SELECT hex(TRIM('\x80\xff' FROM '\x80\x80\x80\xf0\x80\x80\x80\xff')) AS x")
    end, {
        -- <badutf-4.1>
        "X", "F0"
        -- </badutf-4.1>
    })

test:do_test(
    "badutf-4.2",
    function()
        return test:execsql2("SELECT hex(TRIM(LEADING '\x80\xff' FROM '\x80\x80\x80\xf0\x80\x80\x80\xff')) AS x")
    end, {
        -- <badutf-4.2>
        "X", "F0808080FF"
        -- </badutf-4.2>
    })

test:do_test(
    "badutf-4.3",
    function()
        return test:execsql2("SELECT hex(TRIM(TRAILING '\x80\xff' FROM '\x80\x80\x80\xf0\x80\x80\x80\xff')) AS x")
    end, {
        -- <badutf-4.3>
        "X", "808080F0"
        -- </badutf-4.3>
    })

test:do_test(
    "badutf-4.4",
    function()
        return test:execsql2("SELECT hex(TRIM('\xff\x80' FROM '\x80\x80\x80\xf0\x80\x80\x80\xff')) AS x")
    end, {
        -- <badutf-4.4>
        "X", "808080F0808080FF"
        -- </badutf-4.4>
    })

test:do_test(
    "badutf-4.5",
    function()
        return test:execsql2("SELECT hex(TRIM('\xff\x80' FROM '\xff\x80\x80\xf0\x80\x80\x80\xff')) AS x")
    end, {
        -- <badutf-4.5>
        "X", "80F0808080FF"
        -- </badutf-4.5>
    })

test:do_test(
    "badutf-4.6",
    function()
        return test:execsql2("SELECT hex(TRIM('\xff\x80' FROM '\xff\x80\xf0\x80\x80\x80\xff')) AS x")
    end, {
        -- <badutf-4.6>
        "X", "F0808080FF"
        -- </badutf-4.6>
    })

test:do_test(
    "badutf-4.7",
    function()
        return test:execsql2("SELECT hex(TRIM('\xff\x80\x80' FROM '\xff\x80\xf0\x80\x80\x80\xff')) AS x")
    end, {
        -- <badutf-4.7>
        "X", "FF80F0808080FF"
        -- </badutf-4.7>
    })

--db2("close")


test:finish_test()
