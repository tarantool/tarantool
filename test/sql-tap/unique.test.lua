#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(32)

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
-- This file implements regression tests for SQLite library.  The
-- focus of this file is testing the CREATE UNIQUE INDEX statement,
-- and primary keys, and the UNIQUE constraint on table columns
--
-- $Id: unique.test,v 1.9 2009/05/02 15:46:47 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- MUST_WORK_TEST
if (0 > 0)
 then
    -- Try to create a table with two primary keys.
    -- (This is allowed in SQLite even that it is not valid SQL)
end
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
        1, [[table "t1" has more than one primary key]]
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
        INSERT INTO t1(a,b,c) VALUES(1,2,3)
    ]], {
        -- <unique-1.2>
        0
        -- </unique-1.2>
    })

test:do_catchsql_test(
    "unique-1.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(1,3,4)
    ]], {
        -- <unique-1.3>
        1, "UNIQUE constraint failed: t1.a"
        -- </unique-1.3>
    })

-- verify_ex_errcode unique-1.3b SQLITE_CONSTRAINT_PRIMARYKEY
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
        INSERT INTO t1(a,b,c) VALUES(3,2,4)
    ]], {
        -- <unique-1.5>
        1, "UNIQUE constraint failed: t1.b"
        -- </unique-1.5>
    })

-- verify_ex_errcode unique-1.5b SQLITE_CONSTRAINT_UNIQUE
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
        INSERT INTO t1(a,b,c) VALUES(3,4,5)
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
        CREATE TABLE t2(id primary key, a int, b int);
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
        1, "UNIQUE constraint failed: t2.a"
        -- </unique-2.3>
    })

-- verify_ex_errcode unique-2.3b SQLITE_CONSTRAINT_UNIQUE
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
        DROP INDEX i2;
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

-- MUST_WORK_TEST i2 is checking not only "a" column #2289 #2495
if 0 > 0 then
test:do_catchsql_test(
    "unique-2.8",
    [[
        select a from t2;
        --CREATE UNIQUE INDEX i2 ON t2(a);
    ]], {
        -- <unique-2.8>
        1, "UNIQUE constraint failed: t2.a"
        -- </unique-2.8>
    })

-- verify_ex_errcode unique-2.8b SQLITE_CONSTRAINT_UNIQUE
test:do_catchsql_test(
    "unique-2.9",
    [[
        CREATE INDEX i2 ON t2(a);
    ]], {
        -- <unique-2.9>
        0
        -- </unique-2.9>
    })
end

--integrity_check unique-2.10
-- Test the UNIQUE keyword as used on two or more fields.
--
test:do_catchsql_test(
    "unique-3.1",
    [[
        CREATE TABLE t3(
          id primary key,
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
        1, "UNIQUE constraint failed: t3.a, t3.c, t3.d"
        -- </unique-3.4>
    })

-- verify_ex_errcode unique-3.4b SQLITE_CONSTRAINT_UNIQUE
--integrity_check unique-3.5
-- Make sure NULLs are distinct as far as the UNIQUE tests are
-- concerned.
--
test:do_execsql_test(
    "unique-4.1",
    [[
        CREATE TABLE t4(id primary key,a UNIQUE, b, c, UNIQUE(b,c));
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

-- MUST_WORK_TEST i4c is checking not only "b" column #2289 #2495
if 0 > 0 then
test:do_catchsql_test(
    "unique-4.10",
    [[
        CREATE UNIQUE INDEX i4c ON t4(b)
    ]], {
        -- <unique-4.10>
        1, "UNIQUE constraint failed: t4.b"
        -- </unique-4.10>
    })
end

-- verify_ex_errcode unique-4.10b SQLITE_CONSTRAINT_UNIQUE
--integrity_check unique-4.99
-- Test the error message generation logic.  In particular, make sure we
-- do not overflow the static buffer used to generate the error message.
--
test:do_execsql_test(
    "unique-5.1",
    [[
        CREATE TABLE t5(
          id primary key,
          first_column_with_long_name,
          second_column_with_long_name,
          third_column_with_long_name,
          fourth_column_with_long_name,
          fifth_column_with_long_name,
          sixth_column_with_long_name,
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
        1, "UNIQUE constraint failed: t5.first_column_with_long_name, t5.second_column_with_long_name, t5.third_column_with_long_name, t5.fourth_column_with_long_name, t5.fifth_column_with_long_name, t5.sixth_column_with_long_name"
        -- </unique-5.2>
    })

-- verify_ex_errcode unique-5.2b SQLITE_CONSTRAINT_UNIQUE


test:finish_test()
