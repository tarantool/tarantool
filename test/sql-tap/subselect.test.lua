#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(26)

--!./tcltestrunner.lua
-- 2001 September 15
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
-- focus of this file is testing SELECT statements that are part of
-- expressions.
--
-- $Id: subselect.test,v 1.16 2008/08/04 03:51:24 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Omit this whole file if the library is build without subquery support.


-- Basic sanity checking.  Try a simple subselect.
--
test:do_test(
    "subselect-1.1",
    function()
        test:execsql [[
            CREATE TABLE t1(a int primary key, b int);
            INSERT INTO t1 VALUES(1,2);
            INSERT INTO t1 VALUES(3,4);
            INSERT INTO t1 VALUES(5,6);
        ]]
        return test:execsql "SELECT * FROM t1 WHERE a = (SELECT count(*) FROM t1)"
    end, {
        -- <subselect-1.1>
        3, 4
        -- </subselect-1.1>
    })

-- Try a select with more than one result column.
--
test:do_catchsql_test(
    "subselect-1.2",
    [[
        SELECT * FROM t1 WHERE a = (SELECT * FROM t1)
    ]], {
        -- <subselect-1.2>
        1, "row value misused"
        -- </subselect-1.2>
    })

-- A subselect without an aggregate.
--
test:do_execsql_test(
    "subselect-1.3a",
    [[
        SELECT b from t1 where a = (SELECT a FROM t1 WHERE b=2)
    ]], {
        -- <subselect-1.3a>
        2
        -- </subselect-1.3a>
    })

test:do_execsql_test(
    "subselect-1.3b",
    [[
        SELECT b from t1 where a = (SELECT a FROM t1 WHERE b=4)
    ]], {
        -- <subselect-1.3b>
        4
        -- </subselect-1.3b>
    })

test:do_execsql_test(
    "subselect-1.3c",
    [[
        SELECT b from t1 where a = (SELECT a FROM t1 WHERE b=6)
    ]], {
        -- <subselect-1.3c>
        6
        -- </subselect-1.3c>
    })

test:do_execsql_test(
    "subselect-1.3d",
    [[
        SELECT b from t1 where a = (SELECT a FROM t1 WHERE b=8)
    ]], {
        -- <subselect-1.3d>
        
        -- </subselect-1.3d>
    })

test:do_execsql_test(
    "subselect-1.3e",
    [[
        SELECT b FROM t1
         WHERE a = (SELECT a FROM t1 UNION SELECT b FROM t1 ORDER BY 1);
    ]], {
        -- <subselect-1.3e>
        2
        -- </subselect-1.3e>
    })



-- What if the subselect doesn't return any value.  We should get
-- NULL as the result.  Check it out.
--
test:do_execsql_test(
    "subselect-1.4",
    [[
        SELECT b from t1 where a = coalesce((SELECT a FROM t1 WHERE b=5),1)
    ]], {
        -- <subselect-1.4>
        2
        -- </subselect-1.4>
    })

-- Try multiple subselects within a single expression.
--
test:do_test(
    "subselect-1.5",
    function()
        test:execsql [[
            CREATE TABLE t2(x int primary key, y int);
            INSERT INTO t2 VALUES(1,2);
            INSERT INTO t2 VALUES(2,4);
            INSERT INTO t2 VALUES(3,8);
            INSERT INTO t2 VALUES(4,16);
        ]]
        return test:execsql [[
            SELECT y from t2 
            WHERE x = (SELECT sum(b) FROM t1 where a is not null) - (SELECT sum(a) FROM t1)
        ]]
    end, {
        -- <subselect-1.5>
        8
        -- </subselect-1.5>
    })

-- Try something useful.  Delete every entry from t2 where the
-- x value is less than half of the maximum.
--
test:do_test(
    "subselect-1.6",
    function()
        test:execsql "DELETE FROM t2 WHERE x < 0.5*(SELECT max(x) FROM t2)"
        return test:execsql "SELECT x FROM t2 ORDER BY x"
    end, {
        -- <subselect-1.6>
        2, 3, 4
        -- </subselect-1.6>
    })

-- Make sure sorting works for SELECTs there used as a scalar expression.
--
test:do_execsql_test(
    "subselect-2.1",
    [[
        SELECT (SELECT a FROM t1 ORDER BY a), (SELECT a FROM t1 ORDER BY a DESC)
    ]], {
        -- <subselect-2.1>
        1, 5
        -- </subselect-2.1>
    })

test:do_execsql_test(
    "subselect-2.2",
    [[
        SELECT 1 IN (SELECT a FROM t1 ORDER BY a);
    ]], {
        -- <subselect-2.2>
        1
        -- </subselect-2.2>
    })

test:do_execsql_test(
    "subselect-2.3",
    [[
        SELECT 2 IN (SELECT a FROM t1 ORDER BY a DESC);
    ]], {
        -- <subselect-2.3>
        0
        -- </subselect-2.3>
    })

-- Verify that the ORDER BY clause is honored in a subquery.
--
test:do_execsql_test(
    "subselect-3.1",
    [[
        CREATE TABLE t3(x int primary key);
        INSERT INTO t3 SELECT a FROM t1 UNION ALL SELECT b FROM t1;
        SELECT * FROM t3 ORDER BY x;
    ]], {
        -- <subselect-3.1>
        1, 2, 3, 4, 5, 6
        -- </subselect-3.1>
    })



-- ifcapable compound


-- ifcapable !compound
test:do_execsql_test(
    "subselect-3.2",
    [[
        SELECT sum(x) FROM (SELECT x FROM t3 ORDER BY x LIMIT 2);
    ]], {
        -- <subselect-3.2>
        3
        -- </subselect-3.2>
    })

test:do_execsql_test(
    "subselect-3.3",
    [[
        SELECT sum(x) FROM (SELECT x FROM t3 ORDER BY x DESC LIMIT 2);
    ]], {
        -- <subselect-3.3>
        11
        -- </subselect-3.3>
    })

test:do_execsql_test(
    "subselect-3.4",
    [[
        SELECT (SELECT x FROM t3 ORDER BY x);
    ]], {
        -- <subselect-3.4>
        1
        -- </subselect-3.4>
    })

test:do_execsql_test(
    "subselect-3.5",
    [[
        SELECT (SELECT x FROM t3 ORDER BY x DESC);
    ]], {
        -- <subselect-3.5>
        6
        -- </subselect-3.5>
    })

test:do_execsql_test(
    "subselect-3.6",
    [[
        SELECT (SELECT x FROM t3 ORDER BY x LIMIT 1);
    ]], {
        -- <subselect-3.6>
        1
        -- </subselect-3.6>
    })

test:do_execsql_test(
    "subselect-3.7",
    [[
        SELECT (SELECT x FROM t3 ORDER BY x DESC LIMIT 1);
    ]], {
        -- <subselect-3.7>
        6
        -- </subselect-3.7>
    })

test:do_execsql_test(
    "subselect-3.8",
    [[
        SELECT (SELECT x FROM t3 ORDER BY x LIMIT 1 OFFSET 2);
    ]], {
        -- <subselect-3.8>
        3
        -- </subselect-3.8>
    })

test:do_execsql_test(
    "subselect-3.9",
    [[
        SELECT (SELECT x FROM t3 ORDER BY x DESC LIMIT 1 OFFSET 2);
    ]], {
        -- <subselect-3.9>
        4
        -- </subselect-3.9>
    })

test:do_execsql_test(
    "subselect-3.10",
    [[
        SELECT x FROM t3 WHERE x IN
           (SELECT x FROM t3 ORDER BY x DESC LIMIT 1 OFFSET 2);
    ]], {
        -- <subselect-3.10>
        4
        -- </subselect-3.10>
    })

-- Ticket #2295.
-- Make sure type affinities work correctly on subqueries with
-- an ORDER BY clause.
--
test:do_execsql_test(
    "subselect-4.1",
    [[
        CREATE TABLE t4(a TEXT primary key, b TEXT);
        INSERT INTO t4 VALUES('a','1');
        INSERT INTO t4 VALUES('b','2');
        INSERT INTO t4 VALUES('c','3');
        SELECT a FROM t4 WHERE b IN (SELECT b FROM t4 ORDER BY b);
    ]], {
        -- <subselect-4.1>
        "a", "b", "c"
        -- </subselect-4.1>
    })

test:do_execsql_test(
    "subselect-4.2",
    [[
        SELECT a FROM t4 WHERE b IN (SELECT b FROM t4 ORDER BY b LIMIT 1);
    ]], {
        -- <subselect-4.2>
        "a"
        -- </subselect-4.2>
    })

test:do_execsql_test(
    "subselect-4.3",
    [[
        SELECT a FROM t4 WHERE b IN (SELECT b FROM t4 ORDER BY b DESC LIMIT 1);
    ]], {
        -- <subselect-4.3>
        "c"
        -- </subselect-4.3>
    })



test:finish_test()
