#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(15)

--!./tcltestrunner.lua
-- 2013 January 09
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
-- focus of this file is testing that the optimizations that disable
-- ORDER BY clauses work correctly on a 3-way join.  See ticket
-- http://www.sql.org/src/956e4d7f89
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

-- Generate test data for a join.  Verify that the join gets the
-- correct answer.
--
test:do_execsql_test(
    "1.0",
    [[
        CREATE TABLE t1(a INTEGER PRIMARY KEY);
        CREATE TABLE t2(b INTEGER PRIMARY KEY, c INTEGER);
        CREATE TABLE t3(id int primary key, d INTEGER);

        INSERT INTO t1 VALUES(1),(2),(3);

        INSERT INTO t2 VALUES(3, 1);
        INSERT INTO t2 VALUES(4, 2);
        INSERT INTO t2 VALUES(5, 3);

        INSERT INTO t3 VALUES(1, 4),(2, 3),(3, 5);
    ]], {
        -- <1.0>

        -- </1.0>
    })

test:do_execsql_test(
    "1.1.asc",
    [[
        SELECT t1.a
          FROM t1, t2, t3
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a;
    ]], {
        -- <1.1.asc>
        1, 2, 3
        -- </1.1.asc>
    })

test:do_execsql_test(
    "1.1.desc",
    [[
        SELECT t1.a
          FROM t1, t2, t3
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a DESC;
    ]], {
        -- <1.1.desc>
        3, 2, 1
        -- </1.1.desc>
    })

test:do_execsql_test(
    "1.123.asc",
    [[
        SELECT t1.a
          FROM t1 CROSS JOIN t2 CROSS JOIN t3
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a;
    ]], {
        -- <1.123.asc>
        1, 2, 3
        -- </1.123.asc>
    })

test:do_execsql_test(
    "1.123.desc",
    [[
        SELECT t1.a
          FROM t1 CROSS JOIN t2 CROSS JOIN t3
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a DESC;
    ]], {
        -- <1.123.desc>
        3, 2, 1
        -- </1.123.desc>
    })

test:do_execsql_test(
    "1.132.asc",
    [[
        SELECT t1.a
          FROM t1 CROSS JOIN t3 CROSS JOIN t2
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a;
    ]], {
        -- <1.132.asc>
        1, 2, 3
        -- </1.132.asc>
    })

test:do_execsql_test(
    "1.132.desc",
    [[
        SELECT t1.a
          FROM t1 CROSS JOIN t3 CROSS JOIN t2
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a DESC;
    ]], {
        -- <1.132.desc>
        3, 2, 1
        -- </1.132.desc>
    })

test:do_execsql_test(
    "1.213.asc",
    [[
        SELECT t1.a
          FROM t2 CROSS JOIN t1 CROSS JOIN t3
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a;
    ]], {
        -- <1.213.asc>
        1, 2, 3
        -- </1.213.asc>
    })

test:do_execsql_test(
    "1.213.desc",
    [[
        SELECT t1.a
          FROM t2 CROSS JOIN t1 CROSS JOIN t3
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a DESC;
    ]], {
        -- <1.213.desc>
        3, 2, 1
        -- </1.213.desc>
    })

test:do_execsql_test(
    "1.231.asc",
    [[
        SELECT t1.a
          FROM t2 CROSS JOIN t3 CROSS JOIN t1
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a;
    ]], {
        -- <1.231.asc>
        1, 2, 3
        -- </1.231.asc>
    })

test:do_execsql_test(
    "1.231.desc",
    [[
        SELECT t1.a
          FROM t2 CROSS JOIN t3 CROSS JOIN t1
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a DESC;
    ]], {
        -- <1.231.desc>
        3, 2, 1
        -- </1.231.desc>
    })

test:do_execsql_test(
    "1.312.asc",
    [[
        SELECT t1.a
          FROM t3 CROSS JOIN t1 CROSS JOIN t2
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a;
    ]], {
        -- <1.312.asc>
        1, 2, 3
        -- </1.312.asc>
    })

test:do_execsql_test(
    "1.312.desc",
    [[
        SELECT t1.a
          FROM t3 CROSS JOIN t1 CROSS JOIN t2
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a DESC;
    ]], {
        -- <1.312.desc>
        3, 2, 1
        -- </1.312.desc>
    })

test:do_execsql_test(
    "1.321.asc",
    [[
        SELECT t1.a
          FROM t3 CROSS JOIN t2 CROSS JOIN t1
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a;
    ]], {
        -- <1.321.asc>
        1, 2, 3
        -- </1.321.asc>
    })

test:do_execsql_test(
    "1.321.desc",
    [[
        SELECT t1.a
          FROM t3 CROSS JOIN t2 CROSS JOIN t1
         WHERE t1.a=t2.c AND t2.b=t3.d
         ORDER BY t1.a DESC;
    ]], {
        -- <1.321.desc>
        3, 2, 1
        -- </1.321.desc>
    })

test:finish_test()

