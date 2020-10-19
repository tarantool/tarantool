#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(7)

--!./tcltestrunner.lua
-- 2015-03-06
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-----------------------------------------------------------------
--
-- This file implements regression tests for sql library. The
-- focus of this file is testing the LIKE operator and
-- in particular the optimizations that occur to help this
-- operator run faster and that those optimizations work
-- correctly when there are both strings and blobs being tested.
--
-- Ticket 05f43be8fdda9fbd948d374319b99b054140bc36 shows that the following
-- SQL was not working correctly:
--
--     CREATE TABLE t1(x TEXT UNIQUE COLLATE nocase);
--     INSERT INTO t1(x) VALUES(x'616263');
--     SELECT 'query-1', x FROM t1 WHERE x LIKE 'a%';
--     SELECT 'query-2', x FROM t1 WHERE +x LIKE 'a%';
--
-- This script verifies that it works right now.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

test:execsql([[
    --PRAGMA encoding='UTF8';
    CREATE TABLE t1(a INT PRIMARY KEY,b TEXT COLLATE "unicode_ci");
    INSERT INTO t1(a,b)
       VALUES(1,'abc'),
             (2,'ABX'),
             (3,'BCD'),
             (4, char(0x61, 0x62, 0x63)),
             (5, char(0x41, 0x42, 0x58)),
             (6, char(0x42, 0x43, 0x44));
    CREATE INDEX t1ba ON t1(b,a);
]])

-- MUST_WORK #1476 collate nocase
--test:do_execsql_test(
--    "like3-1.1",
--    [[
--        SELECT a, b FROM t1 WHERE b LIKE 'aB%' ORDER BY +a;
--    ]], {
--        -- <like3-1.1>
--        1, "abc", 2, "ABX", 4, "abc", 5, "ABX"
--        -- </like3-1.1>
--    })

test:do_execsql_test(
    "like3-1.2",
    [[
        SELECT a, b FROM t1 WHERE +b LIKE 'aB%' ORDER BY +a;
    ]], {
        -- <like3-1.2>
        1, "abc", 2, "ABX", 4, "abc", 5, "ABX"
        -- </like3-1.2>
    })

test:do_execsql_test(
    "like3-2.0",
    [[
        CREATE TABLE t2(a INT PRIMARY KEY, b TEXT);
        INSERT INTO t2 SELECT a, b FROM t1;
        CREATE INDEX t2ba ON t2(b,a);
        SELECT a, b FROM t2 WHERE b LIKE 'ab%' COLLATE "unicode_ci" ORDER BY +a;
    ]], {
        -- <like3-2.0>
        1, "abc", 2, "ABX", 4, "abc", 5, "ABX"
        -- </like3-2.0>
    })
test:do_execsql_test(
    "like3-2.1",
    [[
        SELECT a, b FROM t2 WHERE +b LIKE 'ab%' COLLATE "unicode_ci" ORDER BY +a;
    ]], {
        -- <like3-2.1>
        1, "abc", 2, "ABX", 4, "abc", 5, "ABX"
        -- </like3-2.1>
    })
test:do_execsql_test(
    "like3-2.2",
    [[
        SELECT a, b FROM t2 WHERE b>='ab' AND b LIKE 'ab%'
    ]], {
        -- <like3-2.2>
        1, "abc", 4, "abc"
        -- </like3-2.2>
    })
test:do_execsql_test(
    "like3-2.3",
    [[
        SELECT a, b FROM t2 WHERE +b>='ab' AND +b LIKE 'ab%'
    ]], {
        -- <like3-2.3>
        1, "abc", 4, "abc"
        -- </like3-2.3>
    })
test:do_execsql_test(
    "like3-2.4",
    [[
        SELECT a, b FROM t2 WHERE b LIKE 'ab%' AND b>='ab'
    ]], {
        -- <like3-2.4>
        1, "abc", 4, "abc"
        -- </like3-2.4>
    })
test:do_execsql_test(
    "like3-2.5",
    [[
        SELECT a, b FROM t2 WHERE +b LIKE 'ab%' AND +b>='ab'
    ]], {
        -- <like3-2.5>
        1, "abc", 4, "abc"
        -- </like3-2.5>
    })

test:execsql([[
    CREATE TABLE t3(x TEXT PRIMARY KEY COLLATE "unicode_ci");
    INSERT INTO t3(x) VALUES('aaa'),('abc'),('abd'),('abe'),('acz');
]])

-- MUST_WORK #1476 collate nocase

if 0>0 then
test:do_execsql_test(
    "like3-3.0",
    [[
        SELECT quote(x) FROM t3 WHERE x LIKE 'ab%' ORDER BY x;
    ]], {
        -- <like3-3.0>
        "'abc'", "'abd'", "'abe'", "X'616263'", "X'616264'", "X'616265'"
        -- </like3-3.0>
    })

test:do_execsql_test(
    "like3-3.1",
    [[
        SELECT quote(x) FROM t3 WHERE x LIKE 'ab%' ORDER BY x DESC;
    ]], {
        -- <like3-3.1>
        "X'616265'", "X'616264'", "X'616263'", "'abe'", "'abd'", "'abc'"
        -- </like3-3.1>
    })

test:do_execsql_test(
    "like3-3.1ck",
    [[
        SELECT quote(x) FROM t3 WHERE x LIKE 'ab%' ORDER BY +x DESC;
    ]], {
        -- <like3-3.1ck>
        "X'616265'", "X'616264'", "X'616263'", "'abe'", "'abd'", "'abc'"
        -- </like3-3.1ck>
    })

test:do_execsql_test(
    "like3-3.2",
    [[
        SELECT quote(x) FROM t3 WHERE x LIKE 'ab%' ORDER BY x ASC;
    ]], {
        -- <like3-3.2>
        "'abc'", "'abd'", "'abe'", "X'616263'", "X'616264'", "X'616265'"
        -- </like3-3.2>
    })

test:do_execsql_test(
    "like3-3.2ck",
    [[
        SELECT quote(x) FROM t3 WHERE x LIKE 'ab%' ORDER BY +x ASC;
    ]], {
        -- <like3-3.2ck>
        "'abc'", "'abd'", "'abe'", "X'616263'", "X'616264'", "X'616265'"
        -- </like3-3.2ck>
    })

test:do_execsql_test(
    "like3-4.0",
    [[
        CREATE TABLE t4(x TEXT COLLATE "unicode_ci" PRIMARY KEY);
        CREATE INDEX t4x ON t4(x DESC);
        INSERT INTO t4(x) SELECT x FROM t3;
        SELECT quote(x) FROM t4 WHERE x LIKE 'ab%' ORDER BY x;
    ]], {
        -- <like3-4.0>
        "'abc'", "'abd'", "'abe'", "X'616263'", "X'616264'", "X'616265'"
        -- </like3-4.0>
    })

test:do_execsql_test(
    "like3-4.1",
    [[
        SELECT quote(x) FROM t4 WHERE x LIKE 'ab%' ORDER BY x DESC;
    ]], {
        -- <like3-4.1>
        "X'616265'", "X'616264'", "X'616263'", "'abe'", "'abd'", "'abc'"
        -- </like3-4.1>
    })

test:do_execsql_test(
    "like3-4.1ck",
    [[
        SELECT quote(x) FROM t4 WHERE x LIKE 'ab%' ORDER BY +x DESC;
    ]], {
        -- <like3-4.1ck>
        "X'616265'", "X'616264'", "X'616263'", "'abe'", "'abd'", "'abc'"
        -- </like3-4.1ck>
    })

test:do_execsql_test(
    "like3-4.2",
    [[
        SELECT quote(x) FROM t4 WHERE x LIKE 'ab%' ORDER BY x ASC;
    ]], {
        -- <like3-4.2>
        "'abc'", "'abd'", "'abe'", "X'616263'", "X'616264'", "X'616265'"
        -- </like3-4.2>
    })

test:do_execsql_test(
    "like3-4.2ck",
    [[
        SELECT quote(x) FROM t4 WHERE x LIKE 'ab%' ORDER BY +x ASC;
    ]], {
        -- <like3-4.2ck>
        "'abc'", "'abd'", "'abe'", "X'616263'", "X'616264'", "X'616265'"
        -- </like3-4.2ck>
    })

end

test:finish_test()
