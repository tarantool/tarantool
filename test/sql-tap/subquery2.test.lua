#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(11)

--!./tcltestrunner.lua
-- 2011 September 16
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
---------------------------------------------------------------------------
-- This file implements regression tests for sql library.  The
-- focus of this script is testing correlated subqueries
--
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "subquery2"


test:do_test(
    "subquery2-1.1",
    function()
        test:execsql [[
            CREATE TABLE t1(a  INT PRIMARY KEY,b INT );
            CREATE TABLE t2(c  INT PRIMARY KEY,d INT );
            CREATE TABLE t3(e  INT PRIMARY KEY,f INT );
            START TRANSACTION;
            INSERT INTO t1 VALUES(1,2);
            INSERT INTO t1 VALUES(3,4);
            INSERT INTO t1 VALUES(5,6);
            INSERT INTO t1 VALUES(7,8);
            INSERT INTO t2 VALUES(1,1);
            INSERT INTO t2 VALUES(3,9);
            INSERT INTO t2 VALUES(5,25);
            INSERT INTO t2 VALUES(7,49);
            INSERT INTO t3 VALUES(1,1);
            INSERT INTO t3 VALUES(3,27);
            INSERT INTO t3 VALUES(5,125);
            INSERT INTO t3 VALUES(7,343);
            COMMIT;
        ]]
        return test:execsql [[
            SELECT a FROM t1
             WHERE b IN (SELECT x+1 FROM (SELECT DISTINCT f/(a*a) AS x FROM t3));
        ]]
    end, {
        -- <subquery2-1.1>
        1, 3, 5, 7
        -- </subquery2-1.1>
    })

test:do_execsql_test(
    "subquery2-1.2",
    [[
        CREATE INDEX t1b ON t1(b);
        SELECT a FROM t1
         WHERE b IN (SELECT x+1 FROM (SELECT DISTINCT f/(a*a) AS x FROM t3));
    ]], {
        -- <subquery2-1.2>
        1, 3, 5, 7
        -- </subquery2-1.2>
    })

test:do_execsql_test(
    "subquery2-1.11",
    [[
        SELECT a FROM t1
         WHERE +b IN (SELECT x+1 FROM (SELECT DISTINCT f/(a*a) AS x FROM t3 LIMIT 1));
    ]], {
        -- <subquery2-1.11>
        1
        -- </subquery2-1.11>
    })

test:do_execsql_test(
    "subquery2-1.12",
    [[
        SELECT a FROM t1
         WHERE b IN (SELECT x+1 FROM (SELECT DISTINCT f/(a*a) AS x FROM t3 LIMIT 1));
    ]], {
        -- <subquery2-1.12>
        1
        -- </subquery2-1.12>
    })

test:do_execsql_test(
    "subquery2-1.21",
    [[
        SELECT a FROM t1
         WHERE +b=(SELECT x+1 FROM 
                     (SELECT DISTINCT f/d AS x FROM t2 JOIN t3 ON d*a=f))
    ]], {
        -- <subquery2-1.21>
        1, 3, 5, 7
        -- </subquery2-1.21>
    })

test:do_execsql_test(
    "subquery2-1.22",
    [[
        SELECT a FROM t1
         WHERE b=(SELECT x+1 FROM 
                     (SELECT DISTINCT f/d AS x FROM t2 JOIN t3 ON d*a=f))
    ]], {
        -- <subquery2-1.22>
        1, 3, 5, 7
        -- </subquery2-1.22>
    })

---------------------------------------------------------------------------
-- Test that ticket d6b36be38a has been fixed.
test:do_execsql_test(
    2.1,
    [[
        CREATE TABLE t4(a  INT PRIMARY KEY, b INT );
        CREATE TABLE t5(a  INT PRIMARY KEY, b INT );
        INSERT INTO t5 VALUES(3, 5);

        INSERT INTO t4 VALUES(1, 1);
        INSERT INTO t4 VALUES(2, 3);
        INSERT INTO t4 VALUES(3, 6);
        INSERT INTO t4 VALUES(4, 10);
        INSERT INTO t4 VALUES(5, 15);
    ]])

test:do_execsql_test(
    2.2,
    [[
        SELECT * 
        FROM (SELECT * FROM t4 ORDER BY a LIMIT 1000000 OFFSET 1)
        LIMIT (SELECT a FROM t5)
    ]], {
        -- <2.2>
        2, 3, 3, 6, 4, 10
        -- </2.2>
    })

-----------------------------------------------------------------------------
-- Ticket http://www.sql.org/src/info/d11a6e908f (2014-09-20)
-- Query planner fault on three-way nested join with compound inner SELECT 
--
test:do_execsql_test(
    3.0,
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t1 (id INTEGER PRIMARY KEY, data TEXT);
        INSERT INTO t1(id,data) VALUES(9,'nine-a');
        INSERT INTO t1(id,data) VALUES(10,'ten-a');
        INSERT INTO t1(id,data) VALUES(11,'eleven-a');
        CREATE TABLE t2 (id INTEGER PRIMARY KEY, data TEXT);
        INSERT INTO t2(id,data) VALUES(9,'nine-b');
        INSERT INTO t2(id,data) VALUES(10,'ten-b');
        INSERT INTO t2(id,data) VALUES(11,'eleven-b');

        SELECT id FROM (
          SELECT id,data FROM (
             SELECT * FROM t1 UNION ALL SELECT * FROM t2
          )
          WHERE id=10 ORDER BY data
        );
    ]], {
        -- <3.0>
        10, 10
        -- </3.0>
    })

test:do_execsql_test(
    3.1,
    [[
        SELECT data FROM (
           SELECT 'dummy', data FROM (
             SELECT data FROM t1 UNION ALL SELECT data FROM t1
           ) ORDER BY data
        );
    ]], {
        -- <3.1>
        "eleven-a", "eleven-a", "nine-a", "nine-a", "ten-a", "ten-a"
        -- </3.1>
    })

test:do_execsql_test(
    3.2,
    [[
        DROP TABLE IF EXISTS t3;
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t3(id INTEGER, data TEXT PRIMARY KEY);
        CREATE TABLE t4(id INTEGER, data TEXT PRIMARY KEY);
        INSERT INTO t3 VALUES(4, 'a'),(2,'c');
        INSERT INTO t4 VALUES(3, 'b'),(1,'d');

        SELECT data, id FROM (
          SELECT id, data FROM (
             SELECT * FROM t3 UNION ALL SELECT * FROM t4
          ) ORDER BY data
        );
    ]], {
        -- <3.2>
        "a", 4, "b", 3, "c", 2, "d", 1
        -- </3.2>
    })



test:finish_test()
