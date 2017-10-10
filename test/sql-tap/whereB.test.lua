#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(63)

--!./tcltestrunner.lua
-- 2009 August 13
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library. The
-- focus of this file is testing WHERE clause conditions with
-- subtle affinity issues.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- For this set of tests:
--
--  *   t1.y holds an integer value with affinity NONE
--  *   t2.b holds a text value with affinity TEXT
--
-- These values are not equal and because neither affinity is NUMERIC
-- no type conversion occurs.
--
test:do_execsql_test(
    "whereB-1.1",
    [[
        CREATE TABLE t1(x primary key,y);    -- affinity of t1.y is NONE
        INSERT INTO t1 VALUES(1,99);

        CREATE TABLE t2(a primary key, b TEXT);  -- affinity of t2.b is TEXT
        CREATE INDEX t2b ON t2(b);
        INSERT INTO t2 VALUES(2,99);

        SELECT x, a, y=b FROM t1, t2 ORDER BY +x, +a;
    ]],
    {
    -- <whereB-1.1>
    1, 2, 0
    -- </whereB-1.1>
    })

test:do_execsql_test(
    "whereB-1.2",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-1.2>
    
    -- </whereB-1.2>
    })

test:do_execsql_test(
    "whereB-1.3",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-1.3>
    
    -- </whereB-1.3>
    })

test:do_execsql_test(
    "whereB-1.4",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-1.4>
    
    -- </whereB-1.4>
    })

test:do_execsql_test(
    "whereB-1.100",
    [[
        DROP INDEX t2b ON t2;
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-1.100>
    
    -- </whereB-1.100>
    })

test:do_execsql_test(
    "whereB-1.101",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-1.101>
    
    -- </whereB-1.101>
    })

test:do_execsql_test(
    "whereB-1.102",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-1.102>
    
    -- </whereB-1.102>
    })

-- For this set of tests:
--
--  *   t1.y holds a text value with affinity TEXT
--  *   t2.b holds an integer value with affinity NONE
--
-- These values are not equal and because neither affinity is NUMERIC
-- no type conversion occurs.
--
test:do_execsql_test(
    "whereB-2.1",
    [[
        DROP TABLE t1;
        DROP TABLE t2;

        CREATE TABLE t1(x primary key, y TEXT);    -- affinity of t1.y is TEXT
        INSERT INTO t1 VALUES(1,99);

        CREATE TABLE t2(a primary key, b BLOB);  -- affinity of t2.b is NONE
        CREATE INDEX t2b ON t2(b);
        INSERT INTO t2 VALUES(2,99);

        SELECT x, a, y=b FROM t1, t2 ORDER BY +x, +a;
    ]],
    {
    -- <whereB-2.1>
    1, 2, 0
    -- </whereB-2.1>
    })

test:do_execsql_test(
    "whereB-2.2",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-2.2>
    
    -- </whereB-2.2>
    })

test:do_execsql_test(
    "whereB-2.3",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-2.3>
    
    -- </whereB-2.3>
    })

test:do_execsql_test(
    "whereB-2.4",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-2.4>
    
    -- </whereB-2.4>
    })

test:do_execsql_test(
    "whereB-2.100",
    [[
        DROP INDEX t2b ON t2;
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-2.100>
    
    -- </whereB-2.100>
    })

test:do_execsql_test(
    "whereB-2.101",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-2.101>
    
    -- </whereB-2.101>
    })

test:do_execsql_test(
    "whereB-2.102",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-2.102>
    
    -- </whereB-2.102>
    })

-- For this set of tests:
--
--  *   t1.y holds a text value with affinity NONE
--  *   t2.b holds an integer value with affinity NONE
--
-- These values are not equal and because neither affinity is NUMERIC
-- no type conversion occurs.
--
test:do_execsql_test(
    "whereB-3.1",
    [[
        DROP TABLE t1;
        DROP TABLE t2;

        CREATE TABLE t1(x primary key, y BLOB);    -- affinity of t1.y is NONE
        INSERT INTO t1 VALUES(1,99);

        CREATE TABLE t2(a primary key, b BLOB);  -- affinity of t2.b is NONE
        CREATE INDEX t2b ON t2(b);
        INSERT INTO t2 VALUES(2,'99');

        SELECT x, a, y=b FROM t1, t2;
    ]],
    {
    -- <whereB-3.1>
    1, 2, 0
    -- </whereB-3.1>
    })

test:do_execsql_test(
    "whereB-3.2",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-3.2>
    
    -- </whereB-3.2>
    })

test:do_execsql_test(
    "whereB-3.3",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-3.3>
    
    -- </whereB-3.3>
    })

test:do_execsql_test(
    "whereB-3.4",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-3.4>
    
    -- </whereB-3.4>
    })

test:do_execsql_test(
    "whereB-3.100",
    [[
        DROP INDEX t2b ON t2;
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-3.100>
    
    -- </whereB-3.100>
    })

test:do_execsql_test(
    "whereB-3.101",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-3.101>
    
    -- </whereB-3.101>
    })

test:do_execsql_test(
    "whereB-3.102",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-3.102>
    
    -- </whereB-3.102>
    })

-- For this set of tests:
--
--  *   t1.y holds a text value with affinity NONE
--  *   t2.b holds an integer value with affinity NUMERIC
--
-- Because t2.b has a numeric affinity, type conversion should occur
-- and the two fields should be equal.
--
test:do_execsql_test(
    "whereB-4.1",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;

        CREATE TABLE t1(x primary key, y BLOB);    -- affinity of t1.y is NONE
        INSERT INTO t1 VALUES(1,'99');

        CREATE TABLE t2(a primary key, b NUMERIC);  -- affinity of t2.b is NUMERIC
        CREATE INDEX t2b ON t2(b);
        INSERT INTO t2 VALUES(2,99);

        SELECT x, a, y=b FROM t1, t2;
    ]],
    {
    -- <whereB-4.1>
    1, 2, 1
    -- </whereB-4.1>
    })

test:do_execsql_test(
    "whereB-4.2",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-4.2>
    1, 2, 1
    -- </whereB-4.2>
    })

test:do_execsql_test(
    "whereB-4.3",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-4.3>
    1, 2, 1
    -- </whereB-4.3>
    })

test:do_execsql_test(
    "whereB-4.4",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-4.4>
    
    -- </whereB-4.4>
    })

test:do_execsql_test(
    "whereB-4.100",
    [[
        DROP INDEX t2b ON t2;
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-4.100>
    1, 2, 1
    -- </whereB-4.100>
    })

test:do_execsql_test(
    "whereB-4.101",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-4.101>
    1, 2, 1
    -- </whereB-4.101>
    })

test:do_execsql_test(
    "whereB-4.102",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-4.102>
    
    -- </whereB-4.102>
    })

-- For this set of tests:
--
--  *   t1.y holds a text value with affinity NONE
--  *   t2.b holds an integer value with affinity INTEGER
--
-- Because t2.b has a numeric affinity, type conversion should occur
-- and the two fields should be equal.
--
test:do_execsql_test(
    "whereB-5.1",
    [[
        DROP TABLE t1;
        DROP TABLE t2;

        CREATE TABLE t1(x primary key, y BLOB);    -- affinity of t1.y is NONE
        INSERT INTO t1 VALUES(1,'99');

        CREATE TABLE t2(a primary key, b INT);  -- affinity of t2.b is INTEGER
        CREATE INDEX t2b ON t2(b);
        INSERT INTO t2 VALUES(2,99);

        SELECT x, a, y=b FROM t1, t2;
    ]],
    {
    -- <whereB-5.1>
    1, 2, 1
    -- </whereB-5.1>
    })

test:do_execsql_test(
    "whereB-5.2",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-5.2>
    1, 2, 1
    -- </whereB-5.2>
    })

test:do_execsql_test(
    "whereB-5.3",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-5.3>
    1, 2, 1
    -- </whereB-5.3>
    })

test:do_execsql_test(
    "whereB-5.4",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-5.4>
    
    -- </whereB-5.4>
    })

test:do_execsql_test(
    "whereB-5.100",
    [[
        DROP INDEX t2b ON t2;
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-5.100>
    1, 2, 1
    -- </whereB-5.100>
    })

test:do_execsql_test(
    "whereB-5.101",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-5.101>
    1, 2, 1
    -- </whereB-5.101>
    })

test:do_execsql_test(
    "whereB-5.102",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-5.102>
    
    -- </whereB-5.102>
    })

-- For this set of tests:
--
--  *   t1.y holds a text value with affinity NONE
--  *   t2.b holds an integer value with affinity REAL
--
-- Because t2.b has a numeric affinity, type conversion should occur
-- and the two fields should be equal.
--
test:do_execsql_test(
    "whereB-6.1",
    [[
        DROP TABLE t1;
        DROP TABLE t2;

        CREATE TABLE t1(x primary key, y BLOB);    -- affinity of t1.y is NONE
        INSERT INTO t1 VALUES(1,'99');

        CREATE TABLE t2(a primary key, b REAL);  -- affinity of t2.b is REAL
        CREATE INDEX t2b ON t2(b);
        INSERT INTO t2 VALUES(2,99.0);

        SELECT x, a, y=b FROM t1, t2;
    ]],
    {
    -- <whereB-6.1>
    1, 2, 1
    -- </whereB-6.1>
    })

test:do_execsql_test(
    "whereB-6.2",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-6.2>
    1, 2, 1
    -- </whereB-6.2>
    })

test:do_execsql_test(
    "whereB-6.3",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-6.3>
    1, 2, 1
    -- </whereB-6.3>
    })

test:do_execsql_test(
    "whereB-6.4",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-6.4>
    
    -- </whereB-6.4>
    })

test:do_execsql_test(
    "whereB-6.100",
    [[
        DROP INDEX t2b ON t2;
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-6.100>
    1, 2, 1
    -- </whereB-6.100>
    })

test:do_execsql_test(
    "whereB-6.101",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-6.101>
    1, 2, 1
    -- </whereB-6.101>
    })

test:do_execsql_test(
    "whereB-6.102",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-6.102>
    
    -- </whereB-6.102>
    })

-- For this set of tests:
--
--  *   t1.y holds an integer value with affinity NUMERIC
--  *   t2.b holds a text value with affinity NONE
--
-- Because t1.y has a numeric affinity, type conversion should occur
-- and the two fields should be equal.
--
test:do_execsql_test(
    "whereB-7.1",
    [[
        DROP TABLE t1;
        DROP TABLE t2;

        CREATE TABLE t1(x primary key, y NUMERIC);  -- affinity of t1.y is NUMERIC
        INSERT INTO t1 VALUES(1,99);

        CREATE TABLE t2(a primary key, b BLOB);  -- affinity of t2.b is NONE
        CREATE INDEX t2b ON t2(b);
        INSERT INTO t2 VALUES(2,'99');

        SELECT x, a, y=b FROM t1, t2;
    ]],
    {
    -- <whereB-7.1>
    1, 2, 1
    -- </whereB-7.1>
    })

test:do_execsql_test(
    "whereB-7.2",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-7.2>
    1, 2, 1
    -- </whereB-7.2>
    })

test:do_execsql_test(
    "whereB-7.3",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-7.3>
    1, 2, 1
    -- </whereB-7.3>
    })

test:do_execsql_test(
    "whereB-7.4",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-7.4>
    
    -- </whereB-7.4>
    })

test:do_execsql_test(
    "whereB-7.100",
    [[
        DROP INDEX t2b ON t2;
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-7.100>
    1, 2, 1
    -- </whereB-7.100>
    })

test:do_execsql_test(
    "whereB-7.101",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-7.101>
    1, 2, 1
    -- </whereB-7.101>
    })

test:do_execsql_test(
    "whereB-7.102",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-7.102>
    
    -- </whereB-7.102>
    })

-- For this set of tests:
--
--  *   t1.y holds an integer value with affinity INTEGER
--  *   t2.b holds a text value with affinity NONE
--
-- Because t1.y has a numeric affinity, type conversion should occur
-- and the two fields should be equal.
--
test:do_execsql_test(
    "whereB-8.1",
    [[
        DROP TABLE t1;
        DROP TABLE t2;

        CREATE TABLE t1(x primary key, y INT);  -- affinity of t1.y is INTEGER
        INSERT INTO t1 VALUES(1,99);

        CREATE TABLE t2(a primary key, b BLOB);  -- affinity of t2.b is NONE
        CREATE INDEX t2b ON t2(b);
        INSERT INTO t2 VALUES(2,'99');

        SELECT x, a, y=b FROM t1, t2;
    ]],
    {
    -- <whereB-8.1>
    1, 2, 1
    -- </whereB-8.1>
    })

test:do_execsql_test(
    "whereB-8.2",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-8.2>
    1, 2, 1
    -- </whereB-8.2>
    })

test:do_execsql_test(
    "whereB-8.3",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-8.3>
    1, 2, 1
    -- </whereB-8.3>
    })

test:do_execsql_test(
    "whereB-8.4",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-8.4>
    
    -- </whereB-8.4>
    })

test:do_execsql_test(
    "whereB-8.100",
    [[
        DROP INDEX t2b ON t2;
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-8.100>
    1, 2, 1
    -- </whereB-8.100>
    })

test:do_execsql_test(
    "whereB-8.101",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-8.101>
    1, 2, 1
    -- </whereB-8.101>
    })

test:do_execsql_test(
    "whereB-8.102",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-8.102>
    
    -- </whereB-8.102>
    })

-- For this set of tests:
--
--  *   t1.y holds an integer value with affinity REAL
--  *   t2.b holds a text value with affinity NONE
--
-- Because t1.y has a numeric affinity, type conversion should occur
-- and the two fields should be equal.
--
test:do_execsql_test(
    "whereB-9.1",
    [[
        DROP TABLE t1;
        DROP TABLE t2;

        CREATE TABLE t1(x primary key, y REAL);  -- affinity of t1.y is REAL
        INSERT INTO t1 VALUES(1,99.0);

        CREATE TABLE t2(a primary key, b BLOB);  -- affinity of t2.b is NONE
        CREATE INDEX t2b ON t2(b);
        INSERT INTO t2 VALUES(2,'99');

        SELECT x, a, y=b FROM t1, t2;
    ]],
    {
    -- <whereB-9.1>
    1, 2, 1
    -- </whereB-9.1>
    })

test:do_execsql_test(
    "whereB-9.2",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-9.2>
    1, 2, 1
    -- </whereB-9.2>
    })

test:do_execsql_test(
    "whereB-9.3",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-9.3>
    1, 2, 1
    -- </whereB-9.3>
    })

test:do_execsql_test(
    "whereB-9.4",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-9.4>
    
    -- </whereB-9.4>
    })

test:do_execsql_test(
    "whereB-9.100",
    [[
        DROP INDEX t2b ON t2;
        SELECT x, a, y=b FROM t1, t2 WHERE y=b;
    ]],
    {
    -- <whereB-9.100>
    1, 2, 1
    -- </whereB-9.100>
    })

test:do_execsql_test(
    "whereB-9.101",
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE b=y;
    ]],
    {
    -- <whereB-9.101>
    1, 2, 1
    -- </whereB-9.101>
    })

test:do_execsql_test(
    "whereB-9.102",
    -- In this case the unary "+" operator removes the column affinity so
    -- the columns compare false
    [[
        SELECT x, a, y=b FROM t1, t2 WHERE +y=+b;
    ]],
    {
    -- <whereB-9.102>
    
    -- </whereB-9.102>
    })

test:finish_test()

