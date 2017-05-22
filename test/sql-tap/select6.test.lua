#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(83)

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
-- focus of this file is testing SELECT statements that contain
-- subqueries in their FROM clause.
--
-- $Id: select6.test,v 1.29 2009/01/09 01:12:28 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Omit this whole file if the library is build without subquery support.


testprefix = "select6"
test:do_test(
    "select6-1.0",
    function()
        -- MUST_WORK_TEST
        -- CREATE TABLE t1(x, y);
        return test:execsql [[
            BEGIN;
            DROP TABLE IF EXISTS t1;
            CREATE TABLE t1 (x int PRIMARY KEY, y int);
            INSERT INTO t1 VALUES(1,1);
            INSERT INTO t1 VALUES(2,2);
            INSERT INTO t1 VALUES(3,2);
            INSERT INTO t1 VALUES(4,3);
            INSERT INTO t1 VALUES(5,3);
            INSERT INTO t1 VALUES(6,3);
            INSERT INTO t1 VALUES(7,3);
            INSERT INTO t1 VALUES(8,4);
            INSERT INTO t1 VALUES(9,4);
            INSERT INTO t1 VALUES(10,4);
            INSERT INTO t1 VALUES(11,4);
            INSERT INTO t1 VALUES(12,4);
            INSERT INTO t1 VALUES(13,4);
            INSERT INTO t1 VALUES(14,4);
            INSERT INTO t1 VALUES(15,4);
            INSERT INTO t1 VALUES(16,5);
            INSERT INTO t1 VALUES(17,5);
            INSERT INTO t1 VALUES(18,5);
            INSERT INTO t1 VALUES(19,5);
            INSERT INTO t1 VALUES(20,5);
            COMMIT;
            SELECT DISTINCT y FROM t1 ORDER BY y;
        ]]
    end, {
        -- <select6-1.0>
        1, 2, 3, 4, 5
        -- </select6-1.0>
    })

test:do_execsql2_test(
    "select6-1.1",
    [[
        SELECT * FROM (SELECT x, y FROM t1 WHERE x<2)
    ]], {
        -- <select6-1.1>
        "x", 1, "y", 1
        -- </select6-1.1>
    })

test:do_execsql_test(
    "select6-1.2",
    [[
        SELECT count(*) FROM (SELECT y FROM t1)
    ]], {
        -- <select6-1.2>
        20
        -- </select6-1.2>
    })

test:do_execsql_test(
    "select6-1.3",
    [[
        SELECT count(*) FROM (SELECT DISTINCT y FROM t1)
    ]], {
        -- <select6-1.3>
        5
        -- </select6-1.3>
    })

test:do_execsql_test(
    "select6-1.4",
    [[
        SELECT count(*) FROM (SELECT DISTINCT * FROM (SELECT y FROM t1))
    ]], {
        -- <select6-1.4>
        5
        -- </select6-1.4>
    })

test:do_execsql_test(
    "select6-1.5",
    [[
        SELECT count(*) FROM (SELECT * FROM (SELECT DISTINCT y FROM t1))
    ]], {
        -- <select6-1.5>
        5
        -- </select6-1.5>
    })

test:do_execsql_test(
    "select6-1.6",
    [[
        SELECT a.cnt, a.y, b.y, b.mx
        FROM (SELECT count(*) AS cnt, y FROM t1 GROUP BY y) AS a,
             (SELECT max(x) AS mx, y FROM t1 GROUP BY y) as b
        WHERE a.y=b.y ORDER BY a.y
    ]], {
        -- <select6-1.6>
        1, 1, 1, 1, 2, 2, 2, 3, 4, 3, 3, 7, 8, 4, 4, 15, 5, 5, 5, 20
        -- </select6-1.6>
    })

test:do_execsql_test(
    "select6-1.7",
    [=[
        SELECT a.y, a.[count(*)], [max(x)], [count(*)]
        FROM (SELECT count(*),y FROM t1 GROUP BY y) AS a,
             (SELECT max(x),y FROM t1 GROUP BY y) as b
        WHERE a.y=b.y ORDER BY a.y
    ]=], {
        -- <select6-1.7>
        1, 1, 1, 1, 2, 2, 3, 2, 3, 4, 7, 4, 4, 8, 15, 8, 5, 5, 20, 5
        -- </select6-1.7>
    })

test:do_execsql_test(
    "select6-1.8",
    [[
        SELECT q, p, r
        FROM (SELECT count(*) as p , y as q FROM t1 GROUP BY y) AS a,
             (SELECT max(x) as r, y as s FROM t1 GROUP BY y) as b
        WHERE q=s ORDER BY s
    ]], {
        -- <select6-1.8>
        1, 1, 1, 2, 2, 3, 3, 4, 7, 4, 8, 15, 5, 5, 20
        -- </select6-1.8>
    })

test:do_execsql_test(
    "select6-1.9",
    [=[
        SELECT q, p, r, b.[min(x)+y]
        FROM (SELECT count(*) as p , y as q FROM t1 GROUP BY y) AS a,
             (SELECT max(x) as r, y as s, min(x)+y FROM t1 GROUP BY y) as b
        WHERE q=s ORDER BY s
    ]=], {
        -- <select6-1.9>
        1, 1, 1, 2, 2, 2, 3, 4, 3, 4, 7, 7, 4, 8, 15, 12, 5, 5, 20, 21
        -- </select6-1.9>
    })

test:do_test(
    "select6-2.0",
    function()
        -- MUST_WORK_TEST
        -- CREATE TABLE t2(a INTEGER PRIMARY KEY, b);
        return test:execsql [[
            DROP TABLE IF EXISTS t2;
            CREATE TABLE t2(a INTEGER PRIMARY KEY, b int);
            INSERT INTO t2 SELECT * FROM t1;
            SELECT DISTINCT b FROM t2 ORDER BY b;
        ]]
    end, {
        -- <select6-2.0>
        1, 2, 3, 4, 5
        -- </select6-2.0>
    })

test:do_execsql2_test(
    "select6-2.1",
    [[
        SELECT * FROM (SELECT a, b FROM t2 WHERE a<2)
    ]], {
        -- <select6-2.1>
        "a", 1, "b", 1
        -- </select6-2.1>
    })

test:do_execsql_test(
    "select6-2.2",
    [[
        SELECT count(*) FROM (SELECT b FROM t2)
    ]], {
        -- <select6-2.2>
        20
        -- </select6-2.2>
    })

test:do_execsql_test(
    "select6-2.3",
    [[
        SELECT count(*) FROM (SELECT DISTINCT b FROM t2)
    ]], {
        -- <select6-2.3>
        5
        -- </select6-2.3>
    })

test:do_execsql_test(
    "select6-2.4",
    [[
        SELECT count(*) FROM (SELECT DISTINCT * FROM (SELECT b FROM t2))
    ]], {
        -- <select6-2.4>
        5
        -- </select6-2.4>
    })

test:do_execsql_test(
    "select6-2.5",
    [[
        SELECT count(*) FROM (SELECT * FROM (SELECT DISTINCT b FROM t2))
    ]], {
        -- <select6-2.5>
        5
        -- </select6-2.5>
    })

test:do_execsql_test(
    "select6-2.6",
    [[
        SELECT a.cnt, a.b, b.b, b.mx
        FROM (SELECT count(*) AS cnt, b FROM t2 GROUP BY b) AS a,
             (SELECT max(a) AS mx, b FROM t2 GROUP BY b) as b
        WHERE a.b=b.b ORDER BY a.b
    ]], {
        -- <select6-2.6>
        1, 1, 1, 1, 2, 2, 2, 3, 4, 3, 3, 7, 8, 4, 4, 15, 5, 5, 5, 20
        -- </select6-2.6>
    })

test:do_execsql_test(
    "select6-2.7",
    [=[
        SELECT a.b, a.[count(*)], [max(a)], [count(*)]
        FROM (SELECT count(*),b FROM t2 GROUP BY b) AS a,
             (SELECT max(a),b FROM t2 GROUP BY b) as b
        WHERE a.b=b.b ORDER BY a.b
    ]=], {
        -- <select6-2.7>
        1, 1, 1, 1, 2, 2, 3, 2, 3, 4, 7, 4, 4, 8, 15, 8, 5, 5, 20, 5
        -- </select6-2.7>
    })

test:do_execsql_test(
    "select6-2.8",
    [[
        SELECT q, p, r
        FROM (SELECT count(*) as p , b as q FROM t2 GROUP BY b) AS a,
             (SELECT max(a) as r, b as s FROM t2 GROUP BY b) as b
        WHERE q=s ORDER BY s
    ]], {
        -- <select6-2.8>
        1, 1, 1, 2, 2, 3, 3, 4, 7, 4, 8, 15, 5, 5, 20
        -- </select6-2.8>
    })

test:do_execsql_test(
    "select6-2.9",
    [[
        SELECT a.q, a.p, b.r
        FROM (SELECT count(*) as p , b as q FROM t2 GROUP BY q) AS a,
             (SELECT max(a) as r, b as s FROM t2 GROUP BY s) as b
        WHERE a.q=b.s ORDER BY a.q
    ]], {
        -- <select6-2.9>
        1, 1, 1, 2, 2, 3, 3, 4, 7, 4, 8, 15, 5, 5, 20
        -- </select6-2.9>
    })

test:do_execsql2_test(
    "select6-3.1",
    [[
        SELECT * FROM (SELECT * FROM (SELECT * FROM t1 WHERE x=3));
    ]], {
        -- <select6-3.1>
        "x", 3, "y", 2
        -- </select6-3.1>
    })

test:do_execsql_test(
    "select6-3.2",
    [[
        SELECT * FROM
          (SELECT a.q, a.p, b.r
           FROM (SELECT count(*) as p , b as q FROM t2 GROUP BY q) AS a,
                (SELECT max(a) as r, b as s FROM t2 GROUP BY s) as b
           WHERE a.q=b.s ORDER BY a.q)
        ORDER BY "a.q"
    ]], {
        -- <select6-3.2>
        1, 1, 1, 2, 2, 3, 3, 4, 7, 4, 8, 15, 5, 5, 20
        -- </select6-3.2>
    })

test:do_execsql_test(
    "select6-3.3",
    [[
        SELECT a,b,a+b FROM (SELECT avg(x) as 'a', avg(y) as 'b' FROM t1)
    ]], {
        -- <select6-3.3>
        10.5, 3.7, 14.2
        -- </select6-3.3>
    })

test:do_execsql_test(
    "select6-3.4",
    [[
        SELECT a,b,a+b FROM (SELECT avg(x) as 'a', avg(y) as 'b' FROM t1 WHERE y=4)
    ]], {
        -- <select6-3.4>
        11.5, 4.0, 15.5
        -- </select6-3.4>
    })

test:do_execsql_test(
    "select6-3.5",
    [[
        SELECT x,y,x+y FROM (SELECT avg(a) as 'x', avg(b) as 'y' FROM t2 WHERE a=4)
    ]], {
        -- <select6-3.5>
        4.0, 3.0, 7.0
        -- </select6-3.5>
    })

test:do_execsql_test(
    "select6-3.6",
    [[
        SELECT a,b,a+b FROM (SELECT avg(x) as 'a', avg(y) as 'b' FROM t1)
        WHERE a>10
    ]], {
        -- <select6-3.6>
        10.5, 3.7, 14.2
        -- </select6-3.6>
    })

test:do_execsql_test(
    "select6-3.7",
    [[
        SELECT a,b,a+b FROM (SELECT avg(x) as 'a', avg(y) as 'b' FROM t1)
        WHERE a<10
    ]], {
        -- <select6-3.7>
        
        -- </select6-3.7>
    })

test:do_execsql_test(
    "select6-3.8",
    [[
        SELECT a,b,a+b FROM (SELECT avg(x) as 'a', avg(y) as 'b' FROM t1 WHERE y=4)
        WHERE a>10
    ]], {
        -- <select6-3.8>
        11.5, 4.0, 15.5
        -- </select6-3.8>
    })

test:do_execsql_test(
    "select6-3.9",
    [[
        SELECT a,b,a+b FROM (SELECT avg(x) as 'a', avg(y) as 'b' FROM t1 WHERE y=4)
        WHERE a<10
    ]], {
        -- <select6-3.9>
        
        -- </select6-3.9>
    })

test:do_execsql_test(
    "select6-3.10",
    [[
        SELECT a,b,a+b FROM (SELECT avg(x) as 'a', y as 'b' FROM t1 GROUP BY b)
        ORDER BY a
    ]], {
        -- <select6-3.10>
        1.0, 1, 2.0, 2.5, 2, 4.5, 5.5, 3, 8.5, 11.5, 4, 15.5, 18.0, 5, 23.0
        -- </select6-3.10>
    })

test:do_execsql_test(
    "select6-3.11",
    [[
        SELECT a,b,a+b FROM 
           (SELECT avg(x) as 'a', y as 'b' FROM t1 GROUP BY b)
        WHERE b<4 ORDER BY a
    ]], {
        -- <select6-3.11>
        1.0, 1, 2.0, 2.5, 2, 4.5, 5.5, 3, 8.5
        -- </select6-3.11>
    })

test:do_execsql_test(
    "select6-3.12",
    [[
        SELECT a,b,a+b FROM 
           (SELECT avg(x) as 'a', y as 'b' FROM t1 GROUP BY b HAVING a>1)
        WHERE b<4 ORDER BY a
    ]], {
        -- <select6-3.12>
        2.5, 2, 4.5, 5.5, 3, 8.5
        -- </select6-3.12>
    })

test:do_execsql_test(
    "select6-3.13",
    [[
        SELECT a,b,a+b FROM 
           (SELECT avg(x) as 'a', y as 'b' FROM t1 GROUP BY b HAVING a>1)
        ORDER BY a
    ]], {
        -- <select6-3.13>
        2.5, 2, 4.5, 5.5, 3, 8.5, 11.5, 4, 15.5, 18.0, 5, 23.0
        -- </select6-3.13>
    })

test:do_execsql_test(
    "select6-3.14",
    [=[
        SELECT [count(*)],y FROM (SELECT count(*), y FROM t1 GROUP BY y)
        ORDER BY [count(*)]
    ]=], {
        -- <select6-3.14>
        1, 1, 2, 2, 4, 3, 5, 5, 8, 4
        -- </select6-3.14>
    })

test:do_execsql_test(
    "select6-3.15",
    [=[
        SELECT [count(*)],y FROM (SELECT count(*), y FROM t1 GROUP BY y)
        ORDER BY y
    ]=], {
        -- <select6-3.15>
        1, 1, 2, 2, 4, 3, 8, 4, 5, 5
        -- </select6-3.15>
    })

test:do_execsql_test(
    "select6-4.1",
    [[
        SELECT a,b,c FROM 
          (SELECT x AS 'a', y AS 'b', x+y AS 'c' FROM t1 WHERE y=4)
        WHERE a<10 ORDER BY a;
    ]], {
        -- <select6-4.1>
        8, 4, 12, 9, 4, 13
        -- </select6-4.1>
    })

test:do_execsql_test(
    "select6-4.2",
    [[
        SELECT y FROM (SELECT DISTINCT y FROM t1) WHERE y<5 ORDER BY y
    ]], {
        -- <select6-4.2>
        1, 2, 3, 4
        -- </select6-4.2>
    })

test:do_execsql_test(
    "select6-4.3",
    [[
        SELECT DISTINCT y FROM (SELECT y FROM t1) WHERE y<5 ORDER BY y
    ]], {
        -- <select6-4.3>
        1, 2, 3, 4
        -- </select6-4.3>
    })

test:do_execsql_test(
    "select6-4.4",
    [[
        SELECT avg(y) FROM (SELECT DISTINCT y FROM t1) WHERE y<5 ORDER BY y
    ]], {
        -- <select6-4.4>
        2.5
        -- </select6-4.4>
    })

test:do_execsql_test(
    "select6-4.5",
    [[
        SELECT avg(y) FROM (SELECT DISTINCT y FROM t1 WHERE y<5) ORDER BY y
    ]], {
        -- <select6-4.5>
        2.5
        -- </select6-4.5>
    })

test:do_execsql_test(
    "select6-5.1",
    [[
        SELECT a,x,b FROM
          (SELECT x+3 AS 'a', x FROM t1 WHERE y=3) AS 'p',
          (SELECT x AS 'b' FROM t1 WHERE y=4) AS 'q'
        WHERE a=b
        ORDER BY a
    ]], {
        -- <select6-5.1>
        8, 5, 8, 9, 6, 9, 10, 7, 10
        -- </select6-5.1>
    })

test:do_execsql_test(
    "select6-5.2",
    [[
        SELECT a,x,b FROM
          (SELECT x+3 AS 'a', x FROM t1 WHERE y=3),
          (SELECT x AS 'b' FROM t1 WHERE y=4)
        WHERE a=b
        ORDER BY a
    ]], {
        -- <select6-5.2>
        8, 5, 8, 9, 6, 9, 10, 7, 10
        -- </select6-5.2>
    })

-- Tests of compound sub-selects
--
test:do_execsql_test(
    "select6-6.1",
    [[
        DELETE FROM t1 WHERE x>4;
        SELECT * FROM t1
    ]], {
        -- <select6-6.1>
        1, 1, 2, 2, 3, 2, 4, 3
        -- </select6-6.1>
    })

test:do_execsql_test(
    "select6-6.2",
    [[
        SELECT * FROM (
          SELECT x AS 'a' FROM t1 UNION ALL SELECT x+10 AS 'a' FROM t1
        ) ORDER BY a;
    ]], {
        -- <select6-6.2>
        1, 2, 3, 4, 11, 12, 13, 14
        -- </select6-6.2>
    })

test:do_execsql_test(
    "select6-6.3",
    [[
        SELECT * FROM (
          SELECT x AS 'a' FROM t1 UNION ALL SELECT x+1 AS 'a' FROM t1
        ) ORDER BY a;
    ]], {
        -- <select6-6.3>
        1, 2, 2, 3, 3, 4, 4, 5
        -- </select6-6.3>
    })

test:do_execsql_test(
    "select6-6.4",
    [[
        SELECT * FROM (
          SELECT x AS 'a' FROM t1 UNION SELECT x+1 AS 'a' FROM t1
        ) ORDER BY a;
    ]], {
        -- <select6-6.4>
        1, 2, 3, 4, 5
        -- </select6-6.4>
    })

test:do_execsql_test(
    "select6-6.5",
    [[
        SELECT * FROM (
          SELECT x AS 'a' FROM t1 INTERSECT SELECT x+1 AS 'a' FROM t1
        ) ORDER BY a;
    ]], {
        -- <select6-6.5>
        2, 3, 4
        -- </select6-6.5>
    })

test:do_execsql_test(
    "select6-6.6",
    [[
        SELECT * FROM (
          SELECT x AS 'a' FROM t1 EXCEPT SELECT x*2 AS 'a' FROM t1
        ) ORDER BY a;
    ]], {
        -- <select6-6.6>
        1, 3
        -- </select6-6.6>
    })



-- ifcapable compound
-- Subselects with no FROM clause
--
test:do_execsql_test(
    "select6-7.1",
    [[
        SELECT * FROM (SELECT 1)
    ]], {
        -- <select6-7.1>
        1
        -- </select6-7.1>
    })

test:do_execsql_test(
    "select6-7.2",
    [[
        SELECT c,b,a,* FROM (SELECT 1 AS 'a', 2 AS 'b', 'abc' AS 'c')
    ]], {
        -- <select6-7.2>
        "abc", 2, 1, 1, 2, "abc"
        -- </select6-7.2>
    })

test:do_execsql_test(
    "select6-7.3",
    [[
        SELECT c,b,a,* FROM (SELECT 1 AS 'a', 2 AS 'b', 'abc' AS 'c' WHERE 0)
    ]], {
        -- <select6-7.3>
        
        -- </select6-7.3>
    })

test:do_execsql2_test(
    "select6-7.4",
    [[
        SELECT c,b,a,* FROM (SELECT 1 AS 'a', 2 AS 'b', 'abc' AS 'c' WHERE 1)
    ]], {
        -- <select6-7.4>
        "c", "abc", "b", 2, "a", 1, "a", 1, "b", 2, "c", "abc"
        -- </select6-7.4>
    })

-- The remaining tests in this file depend on the EXPLAIN keyword.
-- Skip these tests if EXPLAIN is disabled in the current build.
--


-- The following procedure compiles the SQL given as an argument and returns
-- TRUE if that SQL uses any transient tables and returns FALSE if no
-- transient tables are used.  This is used to make sure that the
-- sqliteFlattenSubquery() routine in select.c is doing its job.
--


local json = require('json')
local function is_flat(sql)
    local r = test:execsql("EXPLAIN "..sql)
    r = json.encode(r)
    local i, j = string.find(r,"OpenEphemeral")
    -- return 1 if there is no OpenEphemeral in r
    return i == nil and {1} or {0}
end

-- Check that the flattener works correctly for deeply nested subqueries
-- involving joins.
--
test:do_execsql_test(
    "select6-8.1",
    [[
        BEGIN;
        DROP TABLE IF EXISTS t3;
        CREATE TABLE t3 (p primary key, q);
        INSERT INTO t3 VALUES(1,11);
        INSERT INTO t3 VALUES(2,22);
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t4(q primary key, r);
        INSERT INTO t4 VALUES(11,111);
        INSERT INTO t4 VALUES(22,222);
        COMMIT;
        SELECT * FROM t3 NATURAL JOIN t4;
    ]], {
        -- <select6-8.1>
        1, 11, 111, 2, 22, 222
        -- </select6-8.1>
    })

test:do_execsql_test(
    "select6-8.2",
    [[
        SELECT y, p, q, r FROM
           (SELECT t1.y AS y, t2.b AS b FROM t1, t2 WHERE t1.x=t2.a) AS m,
           (SELECT t3.p AS p, t3.q AS q, t4.r AS r FROM t3 NATURAL JOIN t4) as n
        WHERE  y=p
    ]], {
        -- <select6-8.2>
        1, 1, 11, 111, 2, 2, 22, 222, 2, 2, 22, 222
        -- </select6-8.2>
    })

-- If view support is omitted from the build, then so is the query 
-- "flattener". So omit this test and test select6-8.6 in that case.
test:do_test(
    "select6-8.3",
    function()
        return is_flat([[
    SELECT y, p, q, r FROM
       (SELECT t1.y AS y, t2.b AS b FROM t1, t2 WHERE t1.x=t2.a) AS m,
       (SELECT t3.p AS p, t3.q AS q, t4.r AS r FROM t3 NATURAL JOIN t4) as n
    WHERE  y=p
  ]])
    end, {
        -- <select6-8.3>
        1
        -- </select6-8.3>
    })



-- ifcapable view
test:do_execsql_test(
    "select6-8.4",
    [[
        SELECT DISTINCT y, p, q, r FROM
           (SELECT t1.y AS y, t2.b AS b FROM t1, t2 WHERE t1.x=t2.a) AS m,
           (SELECT t3.p AS p, t3.q AS q, t4.r AS r FROM t3 NATURAL JOIN t4) as n
        WHERE  y=p
    ]], {
        -- <select6-8.4>
        1, 1, 11, 111, 2, 2, 22, 222
        -- </select6-8.4>
    })

test:do_execsql_test(
    "select6-8.5",
    [[
        SELECT * FROM 
          (SELECT y, p, q, r FROM
             (SELECT t1.y AS y, t2.b AS b FROM t1, t2 WHERE t1.x=t2.a) AS m,
             (SELECT t3.p AS p, t3.q AS q, t4.r AS r FROM t3 NATURAL JOIN t4) as n
          WHERE  y=p) AS e,
          (SELECT r AS z FROM t4 WHERE q=11) AS f
        WHERE e.r=f.z
    ]], {
        -- <select6-8.5>
        1, 1, 11, 111, 111
        -- </select6-8.5>
    })

test:do_test(
    "select6-8.6",
    function()
        return is_flat([[
    SELECT * FROM 
      (SELECT y, p, q, r FROM
         (SELECT t1.y AS y, t2.b AS b FROM t1, t2 WHERE t1.x=t2.a) AS m,
         (SELECT t3.p AS p, t3.q AS q, t4.r AS r FROM t3 NATURAL JOIN t4) as n
      WHERE  y=p) AS e,
      (SELECT r AS z FROM t4 WHERE q=11) AS f
    WHERE e.r=f.z
  ]])
    end, {
        -- <select6-8.6>
        1
        -- </select6-8.6>
    })



-- ifcapable view
-- Ticket #1634
--
test:do_execsql_test(
    "select6-9.1",
    [[
        SELECT a.x, b.x FROM t1 AS a, (SELECT x FROM t1 LIMIT 2) AS b
         ORDER BY 1, 2
    ]], {
        -- <select6-9.1>
        1, 1, 1, 2, 2, 1, 2, 2, 3, 1, 3, 2, 4, 1, 4, 2
        -- </select6-9.1>
    })

test:do_execsql_test(
    "select6-9.2",
    [[
        SELECT x FROM (SELECT x FROM t1 LIMIT 2);
    ]], {
        -- <select6-9.2>
        1, 2
        -- </select6-9.2>
    })

test:do_execsql_test(
    "select6-9.3",
    [[
        SELECT x FROM (SELECT x FROM t1 LIMIT 2 OFFSET 1);
    ]], {
        -- <select6-9.3>
        2, 3
        -- </select6-9.3>
    })

test:do_execsql_test(
    "select6-9.4",
    [[
        SELECT x FROM (SELECT x FROM t1) LIMIT 2;
    ]], {
        -- <select6-9.4>
        1, 2
        -- </select6-9.4>
    })

test:do_execsql_test(
    "select6-9.5",
    [[
        SELECT x FROM (SELECT x FROM t1) LIMIT 2 OFFSET 1;
    ]], {
        -- <select6-9.5>
        2, 3
        -- </select6-9.5>
    })

test:do_execsql_test(
    "select6-9.6",
    [[
        SELECT x FROM (SELECT x FROM t1 LIMIT 2) LIMIT 3;
    ]], {
        -- <select6-9.6>
        1, 2
        -- </select6-9.6>
    })

test:do_execsql_test(
    "select6-9.7",
    [[
        SELECT x FROM (SELECT x FROM t1 LIMIT -1) LIMIT 3;
    ]], {
        -- <select6-9.7>
        1, 2, 3
        -- </select6-9.7>
    })

test:do_execsql_test(
    "select6-9.8",
    [[
        SELECT x FROM (SELECT x FROM t1 LIMIT -1);
    ]], {
        -- <select6-9.8>
        1, 2, 3, 4
        -- </select6-9.8>
    })

test:do_execsql_test(
    "select6-9.9",
    [[
        SELECT x FROM (SELECT x FROM t1 LIMIT -1 OFFSET 1);
    ]], {
        -- <select6-9.9>
        2, 3, 4
        -- </select6-9.9>
    })

test:do_execsql_test(
    "select6-9.10",
    [[
        SELECT x, y FROM (SELECT x, (SELECT 10+x) y FROM t1 LIMIT -1 OFFSET 1);
    ]], {
        -- <select6-9.10>
        2, 12, 3, 13, 4, 14
        -- </select6-9.10>
    })

test:do_execsql_test(
    "select6-9.11",
    [[
        SELECT x, y FROM (SELECT x, (SELECT 10)+x y FROM t1 LIMIT -1 OFFSET 1);
    ]], {
        -- <select6-9.11>
        2, 12, 3, 13, 4, 14
        -- </select6-9.11>
    })

---------------------------------------------------------------------------
-- Test that if a UNION ALL sub-query that would otherwise be eligible for
-- flattening consists of two or more SELECT statements that do not all 
-- return the same number of result columns, the error is detected.
--
test:do_execsql_test(
    10.1,
    [[
        DROP TABLE IF EXISTS t;
        DROP TABLE IF EXISTS j;
        DROP TABLE IF EXISTS k;
        CREATE TABLE t(i primary key,j,k);
        CREATE TABLE j(l primary key,m);
        CREATE TABLE k(o primary key);
    ]])

err = { 1, "SELECTs to the left and right of UNION ALL do not have the same number of result columns" }
test:do_execsql_test(
    10.2,
    [[
        SELECT * FROM (SELECT * FROM t), j;
    ]])

test:do_catchsql_test(
    10.3,
    [[
        SELECT * FROM t UNION ALL SELECT * FROM j
    ]], err)

test:do_catchsql_test(
    10.4,
    [[
        SELECT * FROM (SELECT i FROM t UNION ALL SELECT l, m FROM j)
    ]], err)

test:do_catchsql_test(
    10.5,
    [[
        SELECT * FROM (SELECT j FROM t UNION ALL SELECT * FROM j)
    ]], err)

test:do_catchsql_test(
    10.6,
    [[
        SELECT * FROM (SELECT * FROM t UNION ALL SELECT * FROM j)
    ]], err)

test:do_catchsql_test(
    10.7,
    [[
        SELECT * FROM (
          SELECT * FROM t UNION ALL 
          SELECT l,m,l FROM j UNION ALL
          SELECT * FROM k
        )
    ]], err)

test:do_catchsql_test(
    10.8,
    [[
        SELECT * FROM (
          SELECT * FROM k UNION ALL
          SELECT * FROM t UNION ALL 
          SELECT l,m,l FROM j 
        )
    ]], err)

-- 2015-02-09 Ticket [2f7170d73bf9abf80339187aa3677dce3dbcd5ca]
-- "misuse of aggregate" error if aggregate column from FROM
-- subquery is used in correlated subquery 
--
test:do_execsql_test(
    11.1,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(w INT, x INT primary key);
        INSERT INTO t1(w,x)
         VALUES(1,10),(2,20),(3,30),
               (2,21),(3,31),
               (3,32);
        CREATE INDEX t1wx ON t1(w,x);

        DROP TABLE IF EXISTS t2;
        CREATE TABLE t2(w INT primary key, y VARCHAR(8));
        INSERT INTO t2(w,y) VALUES(1,'one'),(2,'two'),(3,'three'),(4,'four');
        CREATE INDEX t2wy ON t2(w,y);

        SELECT cnt, xyz, (SELECT y FROM t2 WHERE w=cnt), '|'
          FROM (SELECT count(*) AS cnt, w AS xyz FROM t1 GROUP BY 2)
         ORDER BY cnt, xyz;
    ]], {
        -- <11.1>
        1, 1, "one", "|", 2, 2, "two", "|", 3, 3, "three", "|"
        -- </11.1>
    })

test:do_execsql_test(
    11.2,
    [[
        SELECT cnt, xyz, lower((SELECT y FROM t2 WHERE w=cnt)), '|'
          FROM (SELECT count(*) AS cnt, w AS xyz FROM t1 GROUP BY 2)
         ORDER BY cnt, xyz;
    ]], {
        -- <11.2>
        1, 1, "one", "|", 2, 2, "two", "|", 3, 3, "three", "|"
        -- </11.2>
    })

test:do_execsql_test(
    11.3,
    [[
        SELECT cnt, xyz, '|'
          FROM (SELECT count(*) AS cnt, w AS xyz FROM t1 GROUP BY 2)
         WHERE (SELECT y FROM t2 WHERE w=cnt)!='two'
         ORDER BY cnt, xyz;
    ]], {
        -- <11.3>
        1, 1, "|", 3, 3, "|"
        -- </11.3>
    })

test:do_execsql_test(
    11.4,
    [[
        SELECT cnt, xyz, '|'
          FROM (SELECT count(*) AS cnt, w AS xyz FROM t1 GROUP BY 2)
         ORDER BY lower((SELECT y FROM t2 WHERE w=cnt));
    ]], {
        -- <11.4>
        1, 1, "|", 3, 3, "|", 2, 2, "|"
        -- </11.4>
    })

test:do_execsql_test(
    11.5,
    [[
        SELECT cnt, xyz, 
               CASE WHEN (SELECT y FROM t2 WHERE w=cnt)=='two'
                    THEN 'aaa' ELSE 'bbb'
                END, '|'
          FROM (SELECT count(*) AS cnt, w AS xyz FROM t1 GROUP BY 2)
         ORDER BY +cnt;
    ]], {
        -- <11.5>
        1, 1, "bbb", "|", 2, 2, "aaa", "|", 3, 3, "bbb", "|"
        -- </11.5>
    })

test:do_execsql_test(
    11.100,
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(x primary key);
        CREATE TABLE t2(y primary key, z);
        SELECT ( SELECT y FROM t2 WHERE z = cnt )
          FROM ( SELECT count(*) AS cnt FROM t1 );
    ]], {
        -- <11.100>
        ""
        -- </11.100>
    })

test:finish_test()

