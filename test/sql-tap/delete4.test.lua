#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(14)

--!./tcltestrunner.lua
-- 2005 August 24
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
-- focus of this script is a test of the DELETE command.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

test:do_execsql_test(
    1.1,
    [[
        CREATE TABLE t1(x INTEGER PRIMARY KEY, y INT );
        INSERT INTO t1 VALUES(1, 0);
        INSERT INTO t1 VALUES(2, 1);
        INSERT INTO t1 VALUES(3, 0);
        INSERT INTO t1 VALUES(4, 1);
        INSERT INTO t1 VALUES(5, 0);
        INSERT INTO t1 VALUES(6, 1);
        INSERT INTO t1 VALUES(7, 0);
        INSERT INTO t1 VALUES(8, 1);
    ]])

test:do_execsql_test(
    1.2,
    [[
        DELETE FROM t1 WHERE y=1;
    ]])

test:do_execsql_test(
    1.3,
    [[
        SELECT x FROM t1;
    ]], {
        -- <1.3>
        1, 3, 5, 7
        -- </1.3>
    })

---------------------------------------------------------------------------
--
--reset_db
test:do_execsql_test(
    2.1,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(x INTEGER PRIMARY KEY, y INT , z SCALAR);
        INSERT INTO t1 VALUES(1, 0, randomblob(200));
        INSERT INTO t1 VALUES(2, 1, randomblob(200));
        INSERT INTO t1 VALUES(3, 0, randomblob(200));
        INSERT INTO t1 VALUES(4, 1, randomblob(200));
        INSERT INTO t1 VALUES(5, 0, randomblob(200));
        INSERT INTO t1 VALUES(6, 1, randomblob(200));
        INSERT INTO t1 VALUES(7, 0, randomblob(200));
        INSERT INTO t1 VALUES(8, 1, randomblob(200));
    ]])

test:do_execsql_test(
    2.2,
    [[
        DELETE FROM t1 WHERE y=1;
    ]])

test:do_execsql_test(
    2.3,
    [[
        SELECT x FROM t1;
    ]], {
        -- <2.3>
        1, 3, 5, 7
        -- </2.3>
    })

---------------------------------------------------------------------------
--
--reset_db
test:do_execsql_test(
    3.1,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a INT , b INT , PRIMARY KEY(a, b));
        INSERT INTO t1 VALUES(1, 2);
        INSERT INTO t1 VALUES(2, 4);
        INSERT INTO t1 VALUES(1, 5);
        DELETE FROM t1 WHERE a=1;
        SELECT * FROM t1;
    ]], {
        -- <3.1>
        2, 4
        -- </3.1>
    })

---------------------------------------------------------------------------
-- DELETE statement that uses the OR optimization
--
--reset_db
test:do_execsql_test(
    3.1,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(i INTEGER PRIMARY KEY, a TEXT, b TEXT);
        CREATE INDEX i1a ON t1(a);
        CREATE INDEX i1b ON t1(b);
        INSERT INTO t1 VALUES(1, 'one', 'i');
        INSERT INTO t1 VALUES(2, 'two', 'ii');
        INSERT INTO t1 VALUES(3, 'three', 'iii');
        INSERT INTO t1 VALUES(4, 'four', 'iv');
        INSERT INTO t1 VALUES(5, 'one', 'i');
        INSERT INTO t1 VALUES(6, 'two', 'ii');
        INSERT INTO t1 VALUES(7, 'three', 'iii');
        INSERT INTO t1 VALUES(8, 'four', 'iv');
    ]], {
        -- <3.1>

        -- </3.1>
    })

test:do_execsql_test(
    3.2,
    [[
        DELETE FROM t1 WHERE a='two' OR b='iv';
    ]])

test:do_execsql_test(
    3.3,
    [[
        SELECT i FROM t1 ORDER BY i;
    ]], {
        -- <3.3>
        1, 3, 5, 7
        -- </3.3>
    })

-- do_execsql_test 3.4 {
--   PRAGMA integrity_check;
-- } {ok}
-- Between 2015-09-14 and 2015-09-28, the following test cases would result
-- in corruption (wrong # of entries in index) due to a bug in the ONEPASS
-- optimization.
--
test:do_execsql_test(
    4.1,
    [[
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t4(col0  INT PRIMARY KEY, col1 TEXT);
        INSERT INTO t4 VALUES(14, 'abcde');
        CREATE INDEX idx_t4_0 ON t4 (col1, col0);
        DELETE FROM t4 WHERE col0=69 OR col0>7;
    ]], {
        -- <4.1>

        -- </4.1>
    })

test:do_execsql_test(
    4.2,
    [[
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t4(col0  INT PRIMARY KEY, col1 TEXT);
        INSERT INTO t4 VALUES(14, 'abcde');
        CREATE INDEX idx_t4_0 ON t4 (col1, col0);
        DELETE FROM t4 WHERE col0=69 OR col0>7;
    ]], {
        -- <4.2>

        -- </4.2>
    })

test:do_execsql_test(
    4.11,
    [[
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t4(col0 INT , col1 TEXT, pk TEXT PRIMARY KEY);
        INSERT INTO t4 VALUES(14, 'abcde','xyzzy');
        CREATE INDEX idx_t4_0 ON t4 (col1, col0);
        CREATE INDEX idx_t4_3 ON t4 (col0);
        DELETE FROM t4 WHERE col0=69 OR col0>7;
    ]], {
        -- <4.11>

        -- </4.11>
    })

test:do_execsql_test(
    4.12,
    [[
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t4(col0 INT , col1 TEXT, pk TEXT PRIMARY KEY);
        INSERT INTO t4 VALUES(14, 'abcde','xyzzy');
        CREATE INDEX idx_t4_3 ON t4 (col0);
        CREATE INDEX idx_t4_0 ON t4 (col1, col0);
        DELETE FROM t4 WHERE col0=69 OR col0>7;
    ]], {
        -- <4.12>

        -- </4.12>
    })



test:finish_test()
