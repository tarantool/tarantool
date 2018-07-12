#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(14)

--!./tcltestrunner.lua
-- 2009 December 9
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
-- This file implements tests for N-way joins (N>2) which make
-- use of USING or NATURAL JOIN.  For such joins, the USING and
-- NATURAL JOIN processing needs to search all tables to the left
-- of the join looking for a match.  See ticket [f74beaabde]
-- for additional information.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- The problem as initially reported on the mailing list:
--
test:do_execsql_test(
    "join6-1.1",
    [[
        CREATE TABLE t1(a INT primary key);
        CREATE TABLE t2(a INT primary key);
        CREATE TABLE t3(a INT primary key,b INT);
        INSERT INTO t1 VALUES(1);
        INSERT INTO t3 VALUES(1,2);

        SELECT * FROM t1 LEFT JOIN t2 USING(a) LEFT JOIN t3 USING(a);
    ]], {
        -- <join6-1.1>
        1, 2
        -- </join6-1.1>
    })

test:do_execsql_test(
    "join6-1.2",
    [[
        SELECT t1.a, t3.b 
          FROM t1 LEFT JOIN t2 ON t1.a=t2.a LEFT JOIN t3 ON t2.a=t3.a;
    ]], {
        -- <join6-1.2>
        1, ""
        -- </join6-1.2>
    })

test:do_execsql_test(
    "join6-1.3",
    [[
        SELECT t1.a, t3.b
          FROM t1 LEFT JOIN t2 ON t1.a=t2.a LEFT JOIN t3 ON t1.a=t3.a;
    ]], {
        -- <join6-1.3>
        1, 2
        -- </join6-1.3>
    })

test:do_execsql_test(
    "join6-2.1",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        DROP TABLE t3;

        CREATE TABLE t1(x INT primary key,y INT);
        CREATE TABLE t2(y INT primary key,z INT);
        CREATE TABLE t3(x INT primary key,z INT);

        INSERT INTO t1 VALUES(1,2);
        INSERT INTO t1 VALUES(3,4);

        INSERT INTO t2 VALUES(2,3);
        INSERT INTO t2 VALUES(4,5);

        INSERT INTO t3 VALUES(1,3);
        INSERT INTO t3 VALUES(3,5);

        SELECT * FROM t1 JOIN t2 USING (y) JOIN t3 USING(x);
    ]], {
        -- <join6-2.1>
        1, 2, 3, 3, 3, 4, 5, 5
        -- </join6-2.1>
    })

test:do_execsql_test(
    "join6-2.2",
    [[
        SELECT * FROM t1 NATURAL JOIN t2 NATURAL JOIN t3;
    ]], {
        -- <join6-2.2>
        1, 2, 3, 3, 4, 5
        -- </join6-2.2>
    })

test:do_execsql_test(
    "join6-3.1",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        DROP TABLE t3;

        CREATE TABLE t1(a INT primary key,x INT,y INT);
        INSERT INTO t1 VALUES(1,91,92);
        INSERT INTO t1 VALUES(2,93,94);

        CREATE TABLE t2(b INT primary key,y INT,z INT);
        INSERT INTO t2 VALUES(3,92,93);
        INSERT INTO t2 VALUES(4,94,95);

        CREATE TABLE t3(c INT primary key,x INT,z INT);
        INSERT INTO t3 VALUES(5,91,93);
        INSERT INTO t3 VALUES(6,99,95);

        SELECT * FROM t1 NATURAL JOIN t2 NATURAL JOIN t3;
    ]], {
        -- <join6-3.1>
        1, 91, 92, 3, 93, 5
        -- </join6-3.1>
    })

test:do_execsql_test(
    "join6-3.2",
    [[
        SELECT * FROM t1 JOIN t2 NATURAL JOIN t3;
    ]], {
        -- <join6-3.2>
        1, 91, 92, 3, 92, 93, 5
        -- </join6-3.2>
    })

test:do_execsql_test(
    "join6-3.3",
    [[
        SELECT * FROM t1 JOIN t2 USING(y) NATURAL JOIN t3;
    ]], {
        -- <join6-3.3>
        1, 91, 92, 3, 93, 5
        -- </join6-3.3>
    })

test:do_execsql_test(
    "join6-3.4",
    [[
        SELECT * FROM t1 NATURAL JOIN t2 JOIN t3 USING(x,z);
    ]], {
        -- <join6-3.4>
        1, 91, 92, 3, 93, 5
        -- </join6-3.4>
    })

test:do_execsql_test(
    "join6-3.5",
    [[
        SELECT * FROM t1 NATURAL JOIN t2 JOIN t3 USING(x);
    ]], {
        -- <join6-3.5>
        1, 91, 92, 3, 93, 5, 93
        -- </join6-3.5>
    })

test:do_execsql_test(
    "join6-3.6",
    [[
        SELECT * FROM t1 NATURAL JOIN t2 JOIN t3 USING(z);
    ]], {
        -- <join6-3.6>
        1, 91, 92, 3, 93, 5, 91, 2, 93, 94, 4, 95, 6, 99
        -- </join6-3.6>
    })

test:do_execsql_test(
    "join6-4.1",
    [[
        SELECT * FROM
           (SELECT 1 AS a, 91 AS x, 92 AS y UNION SELECT 2, 93, 94)
           NATURAL JOIN t2 NATURAL JOIN t3
    ]], {
        -- <join6-4.1>
        1, 91, 92, 3, 93, 5
        -- </join6-4.1>
    })

test:do_execsql_test(
    "join6-4.2",
    [[
        SELECT * FROM t1 NATURAL JOIN
           (SELECT 3 AS b, 92 AS y, 93 AS z UNION SELECT 4, 94, 95)
           NATURAL JOIN t3
    ]], {
        -- <join6-4.2>
        1, 91, 92, 3, 93, 5
        -- </join6-4.2>
    })

test:do_execsql_test(
    "join6-4.3",
    [[
        SELECT * FROM t1 NATURAL JOIN t2 NATURAL JOIN
           (SELECT 5 AS c, 91 AS x, 93 AS z UNION SELECT 6, 99, 95)
    ]], {
        -- <join6-4.3>
        1, 91, 92, 3, 93, 5
        -- </join6-4.3>
    })



test:finish_test()

