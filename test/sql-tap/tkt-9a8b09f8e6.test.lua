#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(47)

--!./tcltestrunner.lua
-- 2014 June 26
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
-- This file implements tests to verify that ticket [9a8b09f8e6] has been
-- fixed.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- MUST_WORK_TEST
test:do_execsql_test(
    1.1,
    [[
        CREATE TABLE t1(x TEXT primary key);
        INSERT INTO t1 VALUES('1');
    ]], {
        -- <1.1>

        -- </1.1>
    })

test:do_execsql_test(
    1.2,
    [[
        CREATE TABLE t2(x INTEGER primary key);
        INSERT INTO t2 VALUES(1);
    ]], {
        -- <1.2>

        -- </1.2>
    })

test:do_execsql_test(
    1.3,
    [[
        CREATE TABLE t3(x NUMBER primary key);
        INSERT INTO t3 VALUES(1.0);
    ]], {
        -- <1.3>

        -- </1.3>
    })

test:do_execsql_test(
    1.4,
    [[
        CREATE TABLE t4(x NUMBER primary key);
        INSERT INTO t4 VALUES(1.11);
    ]], {
        -- <1.4>

        -- </1.4>
    })

test:do_execsql_test(
    1.5,
    [[
        CREATE TABLE t5(id  INT primary key, x INT , y TEXT);
        INSERT INTO t5 VALUES(1, 1, 'one');
        INSERT INTO t5 VALUES(2, 1, 'two');
        INSERT INTO t5 VALUES(3, 1.0, 'three');
        INSERT INTO t5 VALUES(4, 1.0, 'four');
    ]], {
        -- <1.5>

        -- </1.5>
    })

test:do_execsql_test(
    2.1,
    [[
        SELECT x FROM t1 WHERE x IN (1);
    ]], {
        -- <2.1>
        "1"
        -- </2.1>
    })

test:do_execsql_test(
    2.2,
    [[
        SELECT x FROM t1 WHERE x IN (1.0);
    ]], {
        -- <2.2>
        "1"
        -- </2.2>
    })

test:do_execsql_test(
    2.3,
    [[
        SELECT x FROM t1 WHERE x IN ('1');
    ]], {
        -- <2.3>
        "1"
        -- </2.3>
    })

test:do_execsql_test(
    2.4,
    [[
        SELECT x FROM t1 WHERE x IN ('1.0');
    ]], {
        -- <2.4>

        -- </2.4>
    })

test:do_execsql_test(
    2.5,
    [[
        SELECT x FROM t1 WHERE 1 IN (x);
    ]], {
        -- <2.5>
        "1"
        -- </2.5>
    })

test:do_execsql_test(
    2.6,
    [[
        SELECT x FROM t1 WHERE 1.0 IN (x);
    ]], {
        -- <2.6>
        "1"
        -- </2.6>
    })

test:do_execsql_test(
    2.7,
    [[
        SELECT x FROM t1 WHERE '1' IN (x);
    ]], {
        -- <2.7>
        "1"
        -- </2.7>
    })

test:do_execsql_test(
    2.8,
    [[
        SELECT x FROM t1 WHERE '1.0' IN (x);
    ]], {
        -- <2.8>

        -- </2.8>
    })

test:do_execsql_test(
    3.1,
    [[
        SELECT x FROM t2 WHERE x IN (1);
    ]], {
        -- <3.1>
        1
        -- </3.1>
    })

test:do_execsql_test(
    3.2,
    [[
        SELECT x FROM t2 WHERE x IN (1.0);
    ]], {
        -- <3.2>
        1
        -- </3.2>
    })

test:do_execsql_test(
    3.3,
    [[
        SELECT x FROM t2 WHERE x IN ('1');
    ]], {
        -- <3.3>
        1
        -- </3.3>
    })

test:do_execsql_test(
    3.5,
    [[
        SELECT x FROM t2 WHERE 1 IN (x);
    ]], {
        -- <3.5>
        1
        -- </3.5>
    })

test:do_execsql_test(
    3.6,
    [[
        SELECT x FROM t2 WHERE 1.0 IN (x);
    ]], {
        -- <3.6>
        1
        -- </3.6>
    })

test:do_execsql_test(
    3.7,
    [[
        SELECT x FROM t2 WHERE '1' IN (x);
    ]], {
        -- <3.7>
        1
        -- </3.7>
    })

test:do_execsql_test(
    4.1,
    [[
        SELECT x FROM t3 WHERE x IN (1);
    ]], {
        -- <4.1>
        1.0
        -- </4.1>
    })

test:do_execsql_test(
    4.2,
    [[
        SELECT x FROM t3 WHERE x IN (1.0);
    ]], {
        -- <4.2>
        1.0
        -- </4.2>
    })

test:do_catchsql_test(
    4.3,
    [[
        SELECT x FROM t3 WHERE x IN ('1');
    ]], {
        -- <4.3>
        1, "Type mismatch: can not convert string('1') to number"
        -- </4.3>
    })

test:do_catchsql_test(
    4.4,
    [[
        SELECT x FROM t3 WHERE x IN ('1.0');
    ]], {
        -- <4.4>
        1, "Type mismatch: can not convert string('1.0') to number"
        -- </4.4>
    })

test:do_execsql_test(
    4.5,
    [[
        SELECT x FROM t3 WHERE 1 IN (x);
    ]], {
        -- <4.5>
        1.0
        -- </4.5>
    })

test:do_execsql_test(
    4.6,
    [[
        SELECT x FROM t3 WHERE 1.0 IN (x);
    ]], {
        -- <4.6>
        1.0
        -- </4.6>
    })

test:do_catchsql_test(
    4.7,
    [[
        SELECT x FROM t3 WHERE '1' IN (x);
    ]], {
        -- <4.7>
        1, "Type mismatch: can not convert string('1') to number"
        -- </4.7>
    })

test:do_catchsql_test(
    4.8,
    [[
        SELECT x FROM t3 WHERE '1.0' IN (x);
    ]], {
        -- <4.8>
        1, "Type mismatch: can not convert string('1.0') to number"
        -- </4.8>
    })

test:do_execsql_test(
    5.1,
    [[
        SELECT x FROM t4 WHERE x IN (1);
    ]], {
        -- <5.1>

        -- </5.1>
    })

test:do_execsql_test(
    5.2,
    [[
        SELECT x FROM t4 WHERE x IN (1.0);
    ]], {
        -- <5.2>

        -- </5.2>
    })

test:do_catchsql_test(
    5.3,
    [[
        SELECT x FROM t4 WHERE x IN ('1');
    ]], {
        -- <5.3>
        1, "Type mismatch: can not convert string('1') to number"
        -- </5.3>
    })

test:do_catchsql_test(
    5.4,
    [[
        SELECT x FROM t4 WHERE x IN ('1.0');
    ]], {
        -- <5.4>
        1, "Type mismatch: can not convert string('1.0') to number"
        -- </5.4>
    })

test:do_execsql_test(
    5.5,
    [[
        SELECT x FROM t4 WHERE x IN (1.11);
    ]], {
        -- <5.5>
        1.11
        -- </5.5>
    })

test:do_catchsql_test(
    5.6,
    [[
        SELECT x FROM t4 WHERE x IN ('1.11');
    ]], {
        -- <5.6>
        1, "Type mismatch: can not convert string('1.11') to number"
        -- </5.6>
    })

test:do_execsql_test(
    5.7,
    [[
        SELECT x FROM t4 WHERE 1 IN (x);
    ]], {
        -- <5.7>

        -- </5.7>
    })

test:do_execsql_test(
    5.8,
    [[
        SELECT x FROM t4 WHERE 1.0 IN (x);
    ]], {
        -- <5.8>

        -- </5.8>
    })

test:do_catchsql_test(
    5.9,
    [[
        SELECT x FROM t4 WHERE '1' IN (x);
    ]], {
        -- <5.9>
        1, "Type mismatch: can not convert string('1') to number"
        -- </5.9>
    })

test:do_catchsql_test(
    5.10,
    [[
        SELECT x FROM t4 WHERE '1.0' IN (x);
    ]], {
        -- <5.10>
        1, "Type mismatch: can not convert string('1.0') to number"
        -- </5.10>
    })

test:do_execsql_test(
    5.11,
    [[
        SELECT x FROM t4 WHERE 1.11 IN (x);
    ]], {
        -- <5.11>
        1.11
        -- </5.11>
    })

test:do_catchsql_test(
    5.12,
    [[
        SELECT x FROM t4 WHERE '1.11' IN (x);
    ]], {
        -- <5.12>
        1, "Type mismatch: can not convert string('1.11') to number"
        -- </5.12>
    })

test:do_execsql_test(
    6.1,
    [[
        SELECT x, y FROM t5 WHERE x IN (1);
    ]], {
        -- <6.1>
        1, "one", 1, "two", 1, "three", 1.0, "four"
        -- </6.1>
    })

test:do_execsql_test(
    6.2,
    [[
        SELECT x, y FROM t5 WHERE x IN (1.0);
    ]], {
        -- <6.2>
        1, "one", 1, "two", 1, "three", 1.0, "four"
        -- </6.2>
    })

test:do_execsql_test(
    6.3,
    [[
        SELECT x, y FROM t5 WHERE x IN ('1');
    ]], {
        -- <6.3>
        1, "one", 1, "two", 1, "three", 1.0, "four"
        -- </6.3>
    })

test:do_execsql_test(
    6.4,
    [[
        SELECT x, y FROM t5 WHERE x IN ('1.0');
    ]], {
        -- <6.4>
        1, "one", 1, "two", 1, "three", 1.0, "four"
        -- </6.4>
    })

test:do_execsql_test(
    6.5,
    [[
        SELECT x, y FROM t5 WHERE 1 IN (x);
    ]], {
        -- <6.5>
        1, "one", 1, "two", 1, "three", 1.0, "four"
        -- </6.5>
    })

test:do_execsql_test(
    6.6,
    [[
        SELECT x, y FROM t5 WHERE 1.0 IN (x);
    ]], {
        -- <6.6>
        1, "one", 1, "two", 1, "three", 1.0, "four"
        -- </6.6>
    })

test:do_execsql_test(
    6.7,
    [[
        SELECT x, y FROM t5 WHERE '1' IN (x);
    ]], {
        -- <6.7>
        1, "one", 1, "two", 1, "three", 1.0, "four"
        -- </6.7>
    })

test:do_execsql_test(
    6.8,
    [[
        SELECT x, y FROM t5 WHERE '1.0' IN (x);
    ]], {
        -- <6.8>
        1, "one", 1, "two", 1, "three", 1, "four"
        -- </6.8>
    })



test:finish_test()
