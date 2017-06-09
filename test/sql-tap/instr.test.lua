#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(65)

--!./tcltestrunner.lua
-- 2012 October 24
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.  The
-- focus of this file is testing the built-in INSTR() functions.
--
-- EVIDENCE-OF: R-27549-59611 The instr(X,Y) function finds the first
-- occurrence of string Y within string X and returns the number of prior
-- characters plus 1, or 0 if Y is nowhere found within X.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Create a table to work with.
--
test:do_test(
    "instr-1.1",
    function()
        return test:execsql "SELECT instr('abcdefg','a');"
    end, {
        -- <instr-1.1>
        1
        -- </instr-1.1>
    })

test:do_test(
    "instr-1.2",
    function()
        return test:execsql "SELECT instr('abcdefg','b');"
    end, {
        -- <instr-1.2>
        2
        -- </instr-1.2>
    })

test:do_test(
    "instr-1.3",
    function()
        return test:execsql "SELECT instr('abcdefg','c');"
    end, {
        -- <instr-1.3>
        3
        -- </instr-1.3>
    })

test:do_test(
    "instr-1.4",
    function()
        return test:execsql "SELECT instr('abcdefg','d');"
    end, {
        -- <instr-1.4>
        4
        -- </instr-1.4>
    })

test:do_test(
    "instr-1.5",
    function()
        return test:execsql "SELECT instr('abcdefg','e');"
    end, {
        -- <instr-1.5>
        5
        -- </instr-1.5>
    })

test:do_test(
    "instr-1.6",
    function()
        return test:execsql "SELECT instr('abcdefg','f');"
    end, {
        -- <instr-1.6>
        6
        -- </instr-1.6>
    })

test:do_test(
    "instr-1.7",
    function()
        return test:execsql "SELECT instr('abcdefg','g');"
    end, {
        -- <instr-1.7>
        7
        -- </instr-1.7>
    })

test:do_test(
    "instr-1.8",
    function()
        return test:execsql "SELECT instr('abcdefg','h');"
    end, {
        -- <instr-1.8>
        0
        -- </instr-1.8>
    })

test:do_test(
    "instr-1.9",
    function()
        return test:execsql "SELECT instr('abcdefg','abcdefg');"
    end, {
        -- <instr-1.9>
        1
        -- </instr-1.9>
    })

test:do_test(
    "instr-1.10",
    function()
        return test:execsql "SELECT instr('abcdefg','abcdefgh');"
    end, {
        -- <instr-1.10>
        0
        -- </instr-1.10>
    })

test:do_test(
    "instr-1.11",
    function()
        return test:execsql "SELECT instr('abcdefg','bcdefg');"
    end, {
        -- <instr-1.11>
        2
        -- </instr-1.11>
    })

test:do_test(
    "instr-1.12",
    function()
        return test:execsql "SELECT instr('abcdefg','bcdefgh');"
    end, {
        -- <instr-1.12>
        0
        -- </instr-1.12>
    })

test:do_test(
    "instr-1.13",
    function()
        return test:execsql "SELECT instr('abcdefg','cdefg');"
    end, {
        -- <instr-1.13>
        3
        -- </instr-1.13>
    })

test:do_test(
    "instr-1.14",
    function()
        return test:execsql "SELECT instr('abcdefg','cdefgh');"
    end, {
        -- <instr-1.14>
        0
        -- </instr-1.14>
    })

test:do_test(
    "instr-1.15",
    function()
        return test:execsql "SELECT instr('abcdefg','defg');"
    end, {
        -- <instr-1.15>
        4
        -- </instr-1.15>
    })

test:do_test(
    "instr-1.16",
    function()
        return test:execsql "SELECT instr('abcdefg','defgh');"
    end, {
        -- <instr-1.16>
        0
        -- </instr-1.16>
    })

test:do_test(
    "instr-1.17",
    function()
        return test:execsql "SELECT instr('abcdefg','efg');"
    end, {
        -- <instr-1.17>
        5
        -- </instr-1.17>
    })

test:do_test(
    "instr-1.18",
    function()
        return test:execsql "SELECT instr('abcdefg','efgh');"
    end, {
        -- <instr-1.18>
        0
        -- </instr-1.18>
    })

test:do_test(
    "instr-1.19",
    function()
        return test:execsql "SELECT instr('abcdefg','fg');"
    end, {
        -- <instr-1.19>
        6
        -- </instr-1.19>
    })

test:do_test(
    "instr-1.20",
    function()
        return test:execsql "SELECT instr('abcdefg','fgh');"
    end, {
        -- <instr-1.20>
        0
        -- </instr-1.20>
    })

test:do_test(
    "instr-1.21",
    function()
        return test:execsql "SELECT coalesce(instr('abcdefg',NULL),'nil');"
    end, {
        -- <instr-1.21>
        "nil"
        -- </instr-1.21>
    })

test:do_test(
    "instr-1.22",
    function()
        return test:execsql "SELECT coalesce(instr(NULL,'x'),'nil');"
    end, {
        -- <instr-1.22>
        "nil"
        -- </instr-1.22>
    })

test:do_test(
    "instr-1.23",
    function()
        return test:execsql "SELECT instr(12345,34);"
    end, {
        -- <instr-1.23>
        3
        -- </instr-1.23>
    })

test:do_test(
    "instr-1.24",
    function()
        return test:execsql "SELECT instr(123456.78,34);"
    end, {
        -- <instr-1.24>
        3
        -- </instr-1.24>
    })

test:do_test(
    "instr-1.25",
    function()
        return test:execsql "SELECT instr(123456.78,x'3334');"
    end, {
        -- <instr-1.25>
        3
        -- </instr-1.25>
    })

test:do_test(
    "instr-1.26",
    function()
        return test:execsql "SELECT instr('äbcdefg','efg');"
    end, {
        -- <instr-1.26>
        5
        -- </instr-1.26>
    })

test:do_test(
    "instr-1.27",
    function()
        return test:execsql "SELECT instr('€xyzzy','xyz');"
    end, {
        -- <instr-1.27>
        2
        -- </instr-1.27>
    })

test:do_test(
    "instr-1.28",
    function()
        return test:execsql "SELECT instr('abc€xyzzy','xyz');"
    end, {
        -- <instr-1.28>
        5
        -- </instr-1.28>
    })

test:do_test(
    "instr-1.29",
    function()
        return test:execsql "SELECT instr('abc€xyzzy','€xyz');"
    end, {
        -- <instr-1.29>
        4
        -- </instr-1.29>
    })

test:do_test(
    "instr-1.30",
    function()
        return test:execsql "SELECT instr('abc€xyzzy','c€xyz');"
    end, {
        -- <instr-1.30>
        3
        -- </instr-1.30>
    })

test:do_test(
    "instr-1.31",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'01');"
    end, {
        -- <instr-1.31>
        1
        -- </instr-1.31>
    })

test:do_test(
    "instr-1.32",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'02');"
    end, {
        -- <instr-1.32>
        2
        -- </instr-1.32>
    })

test:do_test(
    "instr-1.33",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'03');"
    end, {
        -- <instr-1.33>
        3
        -- </instr-1.33>
    })

test:do_test(
    "instr-1.34",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'04');"
    end, {
        -- <instr-1.34>
        4
        -- </instr-1.34>
    })

test:do_test(
    "instr-1.35",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'05');"
    end, {
        -- <instr-1.35>
        5
        -- </instr-1.35>
    })

test:do_test(
    "instr-1.36",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'06');"
    end, {
        -- <instr-1.36>
        0
        -- </instr-1.36>
    })

test:do_test(
    "instr-1.37",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'0102030405');"
    end, {
        -- <instr-1.37>
        1
        -- </instr-1.37>
    })

test:do_test(
    "instr-1.38",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'02030405');"
    end, {
        -- <instr-1.38>
        2
        -- </instr-1.38>
    })

test:do_test(
    "instr-1.39",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'030405');"
    end, {
        -- <instr-1.39>
        3
        -- </instr-1.39>
    })

test:do_test(
    "instr-1.40",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'0405');"
    end, {
        -- <instr-1.40>
        4
        -- </instr-1.40>
    })

test:do_test(
    "instr-1.41",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'0506');"
    end, {
        -- <instr-1.41>
        0
        -- </instr-1.41>
    })

test:do_test(
    "instr-1.42",
    function()
        return test:execsql "SELECT instr(x'0102030405',x'');"
    end, {
        -- <instr-1.42>
        1
        -- </instr-1.42>
    })

test:do_test(
    "instr-1.43",
    function()
        return test:execsql "SELECT instr(x'',x'');"
    end, {
        -- <instr-1.43>
        1
        -- </instr-1.43>
    })

test:do_test(
    "instr-1.44",
    function()
        return test:execsql "SELECT instr('','');"
    end, {
        -- <instr-1.44>
        1
        -- </instr-1.44>
    })

test:do_test(
    "instr-1.45",
    function()
        return test:execsql "SELECT instr('abcdefg','');"
    end, {
        -- <instr-1.45>
        1
        -- </instr-1.45>
    })

-- ["unset","-nocomplain","longstr"]
local longstr = "abcdefghijklmonpqrstuvwxyz"
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
-- puts [string length '"..longstr.."']
longstr = longstr .. "Xabcde"
test:do_test(
    "instr-1.46",
    function()
        return test:execsql("SELECT instr('"..longstr.."','X');")
    end, {
        -- <instr-1.46>
        106497
        -- </instr-1.46>
    })

test:do_test(
    "instr-1.47",
    function()
        return test:execsql("SELECT instr('"..longstr.."','Y');")
    end, {
        -- <instr-1.47>
        0
        -- </instr-1.47>
    })

test:do_test(
    "instr-1.48",
    function()
        return test:execsql( "SELECT instr('"..longstr.."','Xa');")
    end, {
        -- <instr-1.48>
        106497
        -- </instr-1.48>
    })

test:do_test(
    "instr-1.49",
    function()
        return test:execsql("SELECT instr('"..longstr.."','zXa');")
    end, {
        -- <instr-1.49>
        106496
        -- </instr-1.49>
    })

longstr = string.gsub(longstr, "a", "ä")
test:do_test(
    "instr-1.50",
    function()
        return test:execsql("SELECT instr('"..longstr.."','X');")
    end, {
        -- <instr-1.50>
        106497
        -- </instr-1.50>
    })

test:do_test(
    "instr-1.51",
    function()
        return test:execsql("SELECT instr('"..longstr.."','Y');")
    end, {
        -- <instr-1.51>
        0
        -- </instr-1.51>
    })

test:do_test(
    "instr-1.52",
    function()
        return test:execsql("SELECT instr('"..longstr.."','Xä');")
    end, {
        -- <instr-1.52>
        106497
        -- </instr-1.52>
    })

test:do_test(
    "instr-1.53",
    function()
        return test:execsql("SELECT instr('"..longstr.."','zXä');")
    end, {
        -- <instr-1.53>
        106496
        -- </instr-1.53>
    })

test:do_test(
    "instr-1.54",
    function()
        return test:execsql("SELECT instr(x'78c3a4e282ac79','x');")
    end, {
        -- <instr-1.54>
        1
        -- </instr-1.54>
    })

test:do_test(
    "instr-1.55",
    function()
        return test:execsql "SELECT instr(x'78c3a4e282ac79','y');"
    end, {
        -- <instr-1.55>
        4
        -- </instr-1.55>
    })

-- EVIDENCE-OF: R-46421-32541 Or, if X and Y are both BLOBs, then
-- instr(X,Y) returns one more than the number bytes prior to the first
-- occurrence of Y, or 0 if Y does not occur anywhere within X.
--
test:do_test(
    "instr-1.56.1",
    function()
        return test:execsql "SELECT instr(x'78c3a4e282ac79',x'79');"
    end, {
        -- <instr-1.56.1>
        7
        -- </instr-1.56.1>
    })

test:do_test(
    "instr-1.56.2",
    function()
        return test:execsql "SELECT instr(x'78c3a4e282ac79',x'7a');"
    end, {
        -- <instr-1.56.2>
        0
        -- </instr-1.56.2>
    })

test:do_test(
    "instr-1.56.3",
    function()
        return test:execsql "SELECT instr(x'78c3a4e282ac79',x'78');"
    end, {
        -- <instr-1.56.3>
        1
        -- </instr-1.56.3>
    })

test:do_test(
    "instr-1.56.3",
    function()
        return test:execsql "SELECT instr(x'78c3a4e282ac79',x'a4');"
    end, {
        -- <instr-1.56.3>
        3
        -- </instr-1.56.3>
    })

-- EVIDENCE-OF: R-17329-35644 If both arguments X and Y to instr(X,Y) are
-- non-NULL and are not BLOBs then both are interpreted as strings.
--
test:do_test(
    "instr-1.57.1",
    function()
        return test:execsql "SELECT instr('xä€y',x'79');"
    end, {
        -- <instr-1.57.1>
        4
        -- </instr-1.57.1>
    })

test:do_test(
    "instr-1.57.2",
    function()
        return test:execsql "SELECT instr('xä€y',x'a4');"
    end, {
        -- <instr-1.57.2>
        0
        -- </instr-1.57.2>
    })

test:do_test(
    "instr-1.57.3",
    function()
        return test:execsql "SELECT instr(x'78c3a4e282ac79','y');"
    end, {
        -- <instr-1.57.3>
        4
        -- </instr-1.57.3>
    })

-- EVIDENCE-OF: R-14708-27487 If either X or Y are NULL in instr(X,Y)
-- then the result is NULL.
--
test:do_execsql_test(
    "instr-1.60",
    [[
        SELECT coalesce(instr(NULL,'abc'), 999);
    ]], {
        -- <instr-1.60>
        999
        -- </instr-1.60>
    })

test:do_execsql_test(
    "instr-1.61",
    [[
        SELECT coalesce(instr('abc',NULL), 999);
    ]], {
        -- <instr-1.61>
        999
        -- </instr-1.61>
    })

test:do_execsql_test(
    "instr-1.62",
    [[
        SELECT coalesce(instr(NULL,NULL), 999);
    ]], {
        -- <instr-1.62>
        999
        -- </instr-1.62>
    })



test:finish_test()
