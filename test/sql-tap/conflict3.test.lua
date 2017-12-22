#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(44)

--!./tcltestrunner.lua
-- 2013-11-05
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.
--
-- This file implements tests for the conflict resolution extension
-- to SQLite.
--
-- This file focuses on making sure that combinations of REPLACE,
-- IGNORE, and FAIL conflict resolution play well together.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- MUST_WORK_TEST
test:do_execsql_test(
    "conflict-1.1",
    [[
        CREATE TABLE t1(
          a INTEGER PRIMARY KEY ON CONFLICT REPLACE, 
          b UNIQUE ON CONFLICT IGNORE,
          c UNIQUE ON CONFLICT FAIL
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-1.1>
        1, 2, 3, 2, 3, 4
        -- </conflict-1.1>
    })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
    "conflict-1.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,5);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-1.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-1.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-1.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
        -- <conflict-1.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-1.3>
    })

test:do_execsql_test(
    "conflict-1.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-1.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-1.4>
    })

-- Replete the tests above, but this time on a table non-INTEGER primary key.
--
test:do_execsql_test(
    "conflict-2.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(
          a INT PRIMARY KEY ON CONFLICT REPLACE, 
          b UNIQUE ON CONFLICT IGNORE,
          c UNIQUE ON CONFLICT FAIL
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-2.1>
        1, 2, 3, 2, 3, 4
        -- </conflict-2.1>
    })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
    "conflict-2.2",
    [[
       INSERT INTO t1(a,b,c) VALUES(3,2,5);
       SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-2.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-2.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-2.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
        -- <conflict-2.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-2.3>
    })

test:do_execsql_test(
    "conflict-2.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-2.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-2.4>
    })

-- Replete again
--
test:do_execsql_test(
    "conflict-3.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(
          a INT PRIMARY KEY ON CONFLICT REPLACE, 
          b UNIQUE ON CONFLICT IGNORE,
          c UNIQUE ON CONFLICT FAIL
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-3.1>
        1, 2, 3, 2, 3, 4
        -- </conflict-3.1>
    })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
    "conflict-3.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,5);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-3.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-3.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-3.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
        -- <conflict-3.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-3.3>
    })

test:do_execsql_test(
    "conflict-3.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-3.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-3.4>
    })

-- Arrange the table rows in a different order and repeat.
--
test:do_execsql_test(
    "conflict-4.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(
          b UNIQUE ON CONFLICT IGNORE,
          c UNIQUE ON CONFLICT FAIL,
          a INT PRIMARY KEY ON CONFLICT REPLACE
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-4.1>
        1, 2, 3, 2, 3, 4
        -- </conflict-4.1>
    })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
    "conflict-4.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,5);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-4.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-4.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-4.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
        -- <conflict-4.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-4.3>
    })

test:do_execsql_test(
    "conflict-4.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-4.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-4.4>
    })

-- Arrange the table rows in a different order and repeat.
--
test:do_execsql_test(
    "conflict-5.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(
          b UNIQUE ON CONFLICT IGNORE,
          a INT PRIMARY KEY ON CONFLICT REPLACE,
          c UNIQUE ON CONFLICT FAIL
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
    -- <conflict-5.1>
    1, 2, 3, 2, 3, 4
        -- </conflict-5.1>
    })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
   "conflict-5.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,5);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-5.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-5.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-5.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
        -- <conflict-5.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-5.3>
    })

test:do_execsql_test(
    "conflict-5.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-5.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-5.4>
    })

-- Arrange the table rows in a different order and repeat.
--
test:do_execsql_test(
    "conflict-6.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(
          c UNIQUE ON CONFLICT FAIL,
          a INT PRIMARY KEY ON CONFLICT REPLACE,
          b UNIQUE ON CONFLICT IGNORE
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-6.1>
        1, 2, 3, 2, 3, 4
        -- </conflict-6.1>
    })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
    "conflict-6.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,5);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-6.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-6.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-6.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
        -- <conflict-6.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-6.3>
    })

test:do_execsql_test(
    "conflict-6.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-6.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-6.4>
    })

-- Change which column is the PRIMARY KEY
--
test:do_execsql_test(
    "conflict-7.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(
          a UNIQUE ON CONFLICT REPLACE, 
          b INTEGER PRIMARY KEY ON CONFLICT IGNORE,
          c UNIQUE ON CONFLICT FAIL
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-7.1>
        1, 2, 3, 2, 3, 4
        -- </conflict-7.1>
    })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
    "conflict-7.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,5);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-7.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-7.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-7.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
        -- <conflict-7.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-7.3>
    })

test:do_execsql_test(
    "conflict-7.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-7.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-7.4>
    })

-- Change which column is the PRIMARY KEY
--
test:do_execsql_test(
    "conflict-8.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(
          a UNIQUE ON CONFLICT REPLACE, 
          b INT PRIMARY KEY ON CONFLICT IGNORE,
          c UNIQUE ON CONFLICT FAIL
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-8.1>
        1, 2, 3, 2, 3, 4
        -- </conflict-8.1>
    })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
    "conflict-8.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,5);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-8.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-8.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-8.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
        -- <conflict-8.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-8.3>
    })

test:do_execsql_test(
    "conflict-8.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-8.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-8.4>
    })

-- Change which column is the PRIMARY KEY
--
test:do_execsql_test(
    "conflict-9.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(
          a UNIQUE ON CONFLICT REPLACE, 
          b INT PRIMARY KEY ON CONFLICT IGNORE,
          c UNIQUE ON CONFLICT FAIL
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-9.1>
        1, 2, 3, 2, 3, 4
        -- </conflict-9.1>
   })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
    "conflict-9.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,5);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-9.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-9.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-9.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
        -- <conflict-9.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-9.3>
    })

test:do_execsql_test(
    "conflict-9.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-9.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-9.4>
    })

-- Change which column is the PRIMARY KEY
--
test:do_execsql_test(
    "conflict-10.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(
          a UNIQUE ON CONFLICT REPLACE, 
          b UNIQUE ON CONFLICT IGNORE,
          c INTEGER PRIMARY KEY ON CONFLICT FAIL
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-10.1>
        1, 2, 3, 2, 3, 4
        -- </conflict-10.1>
    })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
    "conflict-10.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,5);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-10.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-10.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-10.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
    -- <conflict-10.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-10.3>
    })

test:do_execsql_test(
    "conflict-10.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-10.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-10.4>
    })

-- Change which column is the PRIMARY KEY
--
test:do_execsql_test(
    "conflict-11.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(
          a UNIQUE ON CONFLICT REPLACE, 
          b UNIQUE ON CONFLICT IGNORE,
          c PRIMARY KEY ON CONFLICT FAIL
        );
        INSERT INTO t1(a,b,c) VALUES(1,2,3), (2,3,4);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-11.1>
        1, 2, 3, 2, 3, 4
        -- </conflict-11.1>
    })

-- Insert a row that conflicts on column B.  The insert should be ignored.
--
test:do_execsql_test(
    "conflict-11.2",
    [[
        INSERT INTO t1(a,b,c) VALUES(3,2,5);
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-11.2>
        1, 2, 3, 2, 3, 4
        -- </conflict-11.2>
    })

-- Insert two rows where the second conflicts on C.  The first row show go
-- and and then there should be a constraint error.
--
test:do_catchsql_test(
    "conflict-11.3",
    [[
        INSERT INTO t1(a,b,c) VALUES(4,5,6), (5,6,4);
    ]], {
        -- <conflict-11.3>
        1, "UNIQUE constraint failed: T1.C"
        -- </conflict-11.3>
    })

test:do_execsql_test(
    "conflict-11.4",
    [[
        SELECT a,b,c FROM t1 ORDER BY a;
    ]], {
        -- <conflict-11.4>
        1, 2, 3, 2, 3, 4, 4, 5, 6
        -- </conflict-11.4>
    })



test:finish_test()
