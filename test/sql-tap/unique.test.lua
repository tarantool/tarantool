#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(35)

--!./tcltestrunner.lua
-- 2001 September 27
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
-- focus of this file is testing the CREATE UNIQUE INDEX statement,
-- and primary keys, and the UNIQUE constraint on table columns
--
-- $Id: unique.test,v 1.9 2009/05/02 15:46:47 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

test:do_catchsql_test(
    "unique-1.1",
    [[
        CREATE TABLE t1(
           a int PRIMARY KEY,
           b int PRIMARY KEY,
           c text
        );
    ]], {
        -- <unique-1.1>
        1, [[Failed to create space 'T1': primary key has been already declared]]
        -- </unique-1.1>
    })

test:do_catchsql_test(
    "unique-1.1b",
    [[
        CREATE TABLE t1(
           a int PRIMARY KEY,
           b int UNIQUE,
           c text
        );
    ]], {
        -- <unique-1.1b>
        0
        -- </unique-1.1b>
    })

test:do_catchsql_test(
    "unique-1.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(1,2,'3')
    ]], {
        -- <unique-1.2>
        0
        -- </unique-1.2>
    })

test:do_catchsql_test(
    "unique-1.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(1,3,'4')
    ]], {
        -- <unique-1.3>
        1, "Duplicate key exists in unique index \"pk_unnamed_T1_1\" in space \"T1\" with old tuple - [1, 2, \"3\"] and new tuple - [1, 3, \"4\"]"
        -- </unique-1.3>
    })

-- Verify the previous test has not inserted anything.
test:do_execsql_test(
    "unique-1.4",
    [[
        SELECT * FROM t1 ORDER BY a;
    ]], {
        -- <unique-1.4>
        1, 2, "3"
        -- </unique-1.4>
    })

test:do_catchsql_test(
    "unique-1.5",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,'4')
    ]], {
        -- <unique-1.5>
        1, "Duplicate key exists in unique index \"unique_unnamed_T1_2\" in space \"T1\" with old tuple - [1, 2, \"3\"] and new tuple - [3, 2, \"4\"]"
        -- </unique-1.5>
    })

-- Verify the previous test has not inserted anything.
test:do_execsql_test(
    "unique-1.6",
    [[
        SELECT * FROM t1 ORDER BY a;
    ]], {
        -- <unique-1.6>
        1, 2, "3"
        -- </unique-1.6>
    })

test:do_catchsql_test(
    "unique-1.7",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,4,'5')
    ]], {
        -- <unique-1.7>
        0
        -- </unique-1.7>
    })

test:do_execsql_test(
    "unique-1.8",
    [[
        SELECT * FROM t1 ORDER BY a;
    ]], {
        -- <unique-1.8>
        1, 2, "3", 3, 4, "5"
        -- </unique-1.8>
    })

--integrity_check unique-1.9
test:do_execsql_test(
    "unique-2.0",
    [[
        DROP TABLE t1;
        CREATE TABLE t2(id int primary key, a int, b int);
        INSERT INTO t2(id, a,b) VALUES(1, 1,2);
        INSERT INTO t2(id, a,b) VALUES(2, 3,4);
        SELECT a,b FROM t2 ORDER BY a;
    ]], {
        -- <unique-2.0>
        1, 2, 3, 4
        -- </unique-2.0>
    })

test:do_catchsql_test(
    "unique-2.1",
    [[
        CREATE UNIQUE INDEX i2 ON t2(a)
    ]], {
        -- <unique-2.1>
        0
        -- </unique-2.1>
    })

test:do_catchsql_test(
    "unique-2.2",
    [[
        SELECT a,b FROM t2 ORDER BY a
    ]], {
        -- <unique-2.2>
        0, {1, 2, 3, 4}
        -- </unique-2.2>
    })

test:do_catchsql_test(
    "unique-2.3",
    [[
        INSERT INTO t2 VALUES(3, 1,5);
    ]], {
        -- <unique-2.3>
        1, "Duplicate key exists in unique index \"I2\" in space \"T2\" with old tuple - [1, 1, 2] and new tuple - [3, 1, 5]"
        -- </unique-2.3>
    })

-- Verify the previous test has not inserted anything.
test:do_catchsql_test(
    "unique-2.4",
    [[
        SELECT a,b FROM t2 ORDER BY a
    ]], {
        -- <unique-2.4>
        0, {1, 2, 3, 4}
        -- </unique-2.4>
    })

test:do_catchsql_test(
    "unique-2.5",
    [[
        DROP INDEX i2 ON t2;
        SELECT a,b FROM t2 ORDER BY a;
    ]], {
        -- <unique-2.5>
        0, {1, 2, 3, 4}
        -- </unique-2.5>
    })

test:do_catchsql_test(
    "unique-2.6",
    [[
        INSERT INTO t2 VALUES(4, 1,5)
    ]], {
        -- <unique-2.6>
        0
        -- </unique-2.6>
    })

test:do_catchsql_test(
    "unique-2.7",
    [[
        SELECT a,b FROM t2 ORDER BY a, b;
    ]], {
        -- <unique-2.7>
        0, {1, 2, 1, 5, 3, 4}
        -- </unique-2.7>
    })

test:do_catchsql_test(
    "unique-2.8",
    [[
        CREATE UNIQUE INDEX i2 ON t2(a);
    ]], {
        -- <unique-2.8>
        1, "Duplicate key exists in unique index \"I2\" in space \"T2\" with old tuple - [1, 1, 2] and new tuple - [4, 1, 5]"
        -- </unique-2.8>
    })

test:do_catchsql_test(
    "unique-2.9",
    [[
        CREATE INDEX i2 ON t2(a);
    ]], {
        -- <unique-2.9>
        0
        -- </unique-2.9>
    })

--integrity_check unique-2.10
-- Test the UNIQUE keyword as used on two or more fields.
--
test:do_catchsql_test(
    "unique-3.1",
    [[
        CREATE TABLE t3(
          id int primary key,
           a int,
           b int,
           c int,
           d int,
           unique(a,c,d)
         );
    ]], {
        -- <unique-3.1>
        0
        -- </unique-3.1>
    })

test:do_catchsql_test(
    "unique-3.2",
    [[
        INSERT INTO t3(id,a,b,c,d) VALUES(1,1,2,3,4);
        SELECT a,b,c,d FROM t3 ORDER BY a,b,c,d;
    ]], {
        -- <unique-3.2>
        0, {1, 2, 3, 4}
        -- </unique-3.2>
    })

test:do_catchsql_test(
    "unique-3.3",
    [[
        INSERT INTO t3(id,a,b,c,d) VALUES(2,1,2,3,5);
        SELECT a,b,c,d FROM t3 ORDER BY a,b,c,d;
    ]], {
        -- <unique-3.3>
        0, {1, 2, 3, 4, 1, 2, 3, 5}
        -- </unique-3.3>
    })

test:do_catchsql_test(
    "unique-3.4",
    [[
        INSERT INTO t3(id,a,b,c,d) VALUES(3,1,4,3,5);
        SELECT a,b,c,d FROM t3 ORDER BY a,b,c,d;
    ]], {
        -- <unique-3.4>
        1, "Duplicate key exists in unique index \"unique_unnamed_T3_2\" in space \"T3\" with old tuple - [2, 1, 2, 3, 5] and new tuple - [3, 1, 4, 3, 5]"
        -- </unique-3.4>
    })

--integrity_check unique-3.5
-- Make sure NULLs are distinct as far as the UNIQUE tests are
-- concerned.
--
test:do_execsql_test(
    "unique-4.1",
    [[
        CREATE TABLE t4(id int primary key,a int UNIQUE, b int, c int, UNIQUE(b,c));
        INSERT INTO t4 VALUES(1,1,2,3);
        INSERT INTO t4 VALUES(2, NULL, 2, NULL);
        SELECT a,b,c FROM t4;
    ]], {
        -- <unique-4.1>
        1, 2, 3, "", 2, ""
        -- </unique-4.1>
    })

test:do_catchsql_test(
    "unique-4.2",
    [[
        INSERT INTO t4 VALUES(3, NULL, 3, 4);
    ]], {
        -- <unique-4.2>
        0
        -- </unique-4.2>
    })

test:do_execsql_test(
    "unique-4.3",
    [[
        SELECT a,b,c FROM t4
    ]], {
        -- <unique-4.3>
        1, 2, 3, "", 2, "", "", 3, 4
        -- </unique-4.3>
    })

test:do_catchsql_test(
    "unique-4.4",
    [[
        INSERT INTO t4 VALUES(4, 2, 2, NULL);
    ]], {
        -- <unique-4.4>
        0
        -- </unique-4.4>
    })

test:do_execsql_test(
    "unique-4.5",
    [[
        SELECT a,b,c FROM t4
    ]], {
        -- <unique-4.5>
        1, 2, 3, "", 2, "", "", 3, 4, 2, 2, ""
        -- </unique-4.5>
    })

-- Ticket #1301.  Any NULL value in a set of unique columns should
-- cause the rows to be distinct.
--
test:do_catchsql_test(
    "unique-4.6",
    [[
        INSERT INTO t4 VALUES(5, NULL, 2, NULL);
    ]], {
        -- <unique-4.6>
        0
        -- </unique-4.6>
    })

test:do_execsql_test(
    "unique-4.7",
    [[
        SELECT a,b,c FROM t4
    ]], {
        -- <unique-4.7>
        1, 2, 3, "", 2, "", "", 3, 4, 2, 2, "", "", 2, ""
        -- </unique-4.7>
    })

test:do_catchsql_test(
    "unique-4.8",
    [[
        CREATE UNIQUE INDEX i4a ON t4(a,b)
    ]], {
        -- <unique-4.8>
        0
        -- </unique-4.8>
    })

test:do_catchsql_test(
    "unique-4.9",
    [[
        CREATE UNIQUE INDEX i4b ON t4(a,b,c)
    ]], {
        -- <unique-4.9>
        0
        -- </unique-4.9>
    })

test:do_catchsql_test(
    "unique-4.10",
    [[
        CREATE UNIQUE INDEX i4c ON t4(b)
    ]], {
        -- <unique-4.10>
        1, "Duplicate key exists in unique index \"I4C\" in space \"T4\" with old tuple - [1, 1, 2, 3] and new tuple - [2, null, 2, null]"
        -- </unique-4.10>
    })

--integrity_check unique-4.99
-- Test the error message generation logic.  In particular, make sure we
-- do not overflow the static buffer used to generate the error message.
--
test:do_execsql_test(
    "unique-5.1",
    [[
        CREATE TABLE t5(
          id  INT primary key,
          first_column_with_long_name INT ,
          second_column_with_long_name INT ,
          third_column_with_long_name INT ,
            fourth_column_with_long_name INT ,
          fifth_column_with_long_name INT ,
          sixth_column_with_long_name INT ,
          UNIQUE(
            first_column_with_long_name,
            second_column_with_long_name,
            third_column_with_long_name,
            fourth_column_with_long_name,
            fifth_column_with_long_name,
            sixth_column_with_long_name
          )
        );
        INSERT INTO t5 VALUES(1,1,2,3,4,5,6);
        SELECT * FROM t5;
    ]], {
        -- <unique-5.1>
        1, 1, 2, 3, 4, 5, 6
        -- </unique-5.1>
    })

test:do_catchsql_test(
    "unique-5.2",
    [[
        INSERT INTO t5 VALUES(2, 1,2,3,4,5,6);
    ]], {
        -- <unique-5.2>
        1, "Duplicate key exists in unique index \"unique_unnamed_T5_2\" in space \"T5\" with old tuple - [1, 1, 2, 3, 4, 5, 6] and new tuple - [2, 1, 2, 3, 4, 5, 6]"
        -- </unique-5.2>
    })

test:finish_test()
