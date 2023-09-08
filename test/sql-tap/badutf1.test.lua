#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(20)

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
        return test:execsql2("SELECT HEX(x'80') AS x")
    end, {
        -- <badutf-1.1>
        "x", "80"
        -- </badutf-1.1>
    })

test:do_test(
    "badutf-1.2",
    function()
        return test:execsql2("SELECT HEX(x'81') AS x")
    end, {
        -- <badutf-1.2>
        "x", "81"
        -- </badutf-1.2>
    })

test:do_test(
    "badutf-1.3",
    function()
        return test:execsql2("SELECT HEX(x'bf') AS x")
    end, {
        -- <badutf-1.3>
        "x", "BF"
        -- </badutf-1.3>
    })

test:do_test(
    "badutf-1.4",
    function()
        return test:execsql2("SELECT HEX(x'c0') AS x")
    end, {
        -- <badutf-1.4>
        "x", "C0"
        -- </badutf-1.4>
    })

test:do_test(
    "badutf-1.5",
    function()
        return test:execsql2("SELECT HEX(x'e0') AS x")
    end, {
        -- <badutf-1.5>
        "x", "E0"
        -- </badutf-1.5>
    })

test:do_test(
    "badutf-1.6",
    function()
        return test:execsql2("SELECT HEX(x'f0') AS x")
    end, {
        -- <badutf-1.6>
        "x", "F0"
        -- </badutf-1.6>
    })

test:do_test(
    "badutf-1.7",
    function()
        return test:execsql2("SELECT HEX(x'ff') AS x")
    end, {
        -- <badutf-1.7>
        "x", "FF"
        -- </badutf-1.7>
    })

-- commented as it uses utf16
-- testcases are broken
-- https://github.com/tarantool/tarantool/issues/5743
local is_gh_5743_closed = false
if is_gh_5743_closed then
local sql_exec = nil
test:do_test(
    "badutf-1.10",
    function()
        test:execsql "PRAGMA encoding='UTF16be'"
        return sql_exec("db2", "SELECT HEX('%80') AS x")
    end, {
        -- <badutf-1.10>
        0, "x 0080"
        -- </badutf-1.10>
    })

test:do_test(
    "badutf-1.11",
    function()
        return sql_exec("db2", "SELECT HEX('%81') AS x")
    end, {
        -- <badutf-1.11>
        0, "x 0081"
        -- </badutf-1.11>
    })

test:do_test(
    "badutf-1.12",
    function()
        return sql_exec("db2", "SELECT HEX('%bf') AS x")
    end, {
        -- <badutf-1.12>
        0, "x 00BF"
        -- </badutf-1.12>
    })

test:do_test(
    "badutf-1.13",
    function()
        return sql_exec("db2", "SELECT HEX('%c0') AS x")
    end, {
        -- <badutf-1.13>
        0, "x FFFD"
        -- </badutf-1.13>
    })

test:do_test(
    "badutf-1.14",
    function()
        return sql_exec("db2", "SELECT HEX('%c1') AS x")
    end, {
        -- <badutf-1.14>
        0, "x FFFD"
        -- </badutf-1.14>
    })

test:do_test(
    "badutf-1.15",
    function()
        return sql_exec("db2", "SELECT HEX('%c0%bf') AS x")
    end, {
        -- <badutf-1.15>
        0, "x FFFD"
        -- </badutf-1.15>
    })

test:do_test(
    "badutf-1.16",
    function()
        return sql_exec("db2", "SELECT HEX('%c1%bf') AS x")
    end, {
        -- <badutf-1.16>
        0, "x FFFD"
        -- </badutf-1.16>
    })

test:do_test(
    "badutf-1.17",
    function()
        return sql_exec("db2", "SELECT HEX('%c3%bf') AS x")
    end, {
        -- <badutf-1.17>
        0, "x 00FF"
        -- </badutf-1.17>
    })

test:do_test(
    "badutf-1.18",
    function()
        return sql_exec("db2", "SELECT HEX('%e0') AS x")
    end, {
        -- <badutf-1.18>
        0, "x FFFD"
        -- </badutf-1.18>
    })

test:do_test(
    "badutf-1.19",
    function()
        return sql_exec("db2", "SELECT HEX('%f0') AS x")
    end, {
        -- <badutf-1.19>
        0, "x FFFD"
        -- </badutf-1.19>
    })

test:do_test(
    "badutf-1.20",
    function()
        return sql_exec("db2", "SELECT HEX('%ff') AS x")
    end, {
        -- <badutf-1.20>
        0, "x FFFD"
        -- </badutf-1.20>
    })
end


test:do_test(
    "badutf-3.1",
    function()
        return test:execsql2("SELECT LENGTH('\x80') AS x")
    end, {
        -- <badutf-3.1>
        "x", 1
        -- </badutf-3.1>
    })

test:do_test(
    "badutf-3.2",
    function()
        return test:execsql2("SELECT LENGTH('\x61\x62\x63') AS x")
    end, {
        -- <badutf-3.2>
        "x", 3
        -- </badutf-3.2>
    })

test:do_test(
    "badutf-3.3",
    function()
        return test:execsql2("SELECT LENGTH('\x7f\x80\x81') AS x")
    end, {
        -- <badutf-3.3>
        "x", 3
        -- </badutf-3.3>
    })

test:do_test(
    "badutf-3.4",
    function()
        return test:execsql2("SELECT LENGTH('\x61\xc0') AS x")
    end, {
        -- <badutf-3.4>
        "x", 2
        -- </badutf-3.4>
    })

test:do_test(
    "badutf-3.5",
    function()
        return test:execsql2("SELECT LENGTH('\x80\x80\x80\x80\x80"..
                             "\x80\x80\x80\x80\x80') AS x")
    end, {
        -- <badutf-3.7>
        "x", 10
        -- </badutf-3.7>
    })

test:do_test(
    "badutf-4.1",
    function()
        return test:execsql2([[SELECT HEX(CAST(TRIM(x'80ff' FROM ]]..
                             [[x'808080f0808080ff') AS VARBINARY)) AS x]])
    end, {
        -- <badutf-4.1>
        "x", "F0"
        -- </badutf-4.1>
    })

test:do_test(
    "badutf-4.2",
    function()
        return test:execsql2([[SELECT HEX(CAST(TRIM(LEADING x'80ff' FROM ]]..
                             [[x'808080f0808080ff') AS VARBINARY)) AS x]])
    end, {
        -- <badutf-4.2>
        "x", "F0808080FF"
        -- </badutf-4.2>
    })

test:do_test(
    "badutf-4.3",
    function()
        return test:execsql2([[SELECT HEX(CAST(TRIM(TRAILING x'80ff' FROM ]]..
                             [[x'808080f0808080ff') AS VARBINARY)) AS x]])
    end, {
        -- <badutf-4.3>
        "x", "808080F0"
        -- </badutf-4.3>
    })

test:do_test(
    "badutf-4.4",
    function()
        return test:execsql2([[
            SELECT HEX(TRIM(x'ff80' FROM x'808080f0808080ff')) AS x;
        ]])
    end, {
        -- <badutf-4.4>
        "x", "F0"
        -- </badutf-4.4>
    })

test:do_test(
    "badutf-4.5",
    function()
        return test:execsql2([[
            SELECT HEX(TRIM(x'ff80' FROM x'ff8080f0808080ff')) AS x;
        ]])
    end, {
        -- <badutf-4.5>
        "x", "F0"
        -- </badutf-4.5>
    })

test:do_test(
    "badutf-4.6",
    function()
        return test:execsql2([[
            SELECT HEX(TRIM(x'ff80' FROM x'ff80f0808080ff')) AS x;
        ]])
    end, {
        -- <badutf-4.6>
        "x", "F0"
        -- </badutf-4.6>
    })

test:do_test(
    "badutf-4.7",
    function()
        return test:execsql2([[
            SELECT HEX(TRIM(x'ff8080' FROM x'ff80f0808080ff')) AS x;
        ]])
    end, {
        -- <badutf-4.7>
        "x", "F0"
        -- </badutf-4.7>
    })

-- gh-4145: Make sure that TRIM() properly work with VARBINARY.
test:do_execsql_test(
    "badutf-5",
    [[
        SELECT HEX(TRIM(x'ff1234' from x'1234125678123412'));
    ]],
    {
        '5678'
    }
)

--db2("close")


test:finish_test()
