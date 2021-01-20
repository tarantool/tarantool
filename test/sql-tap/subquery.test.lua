#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(69)

--!./tcltestrunner.lua
-- 2005 January 19
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
-- $Id: subquery.test,v 1.17 2009/01/09 01:12:28 drh Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_test(
    "subquery-1.1",
    function()
        test:execsql [[
            CREATE TABLE t1(a  INT PRIMARY KEY,b INT );
            CREATE TABLE t2(x  INT PRIMARY KEY,y INT );
            START TRANSACTION;
            INSERT INTO t1 VALUES(1,2);
            INSERT INTO t1 VALUES(3,4);
            INSERT INTO t1 VALUES(5,6);
            INSERT INTO t1 VALUES(7,8);
            INSERT INTO t2 VALUES(1,1);
            INSERT INTO t2 VALUES(3,9);
            INSERT INTO t2 VALUES(5,25);
            INSERT INTO t2 VALUES(7,49);
            COMMIT;
        ]]
        return test:execsql [[
            SELECT a, (SELECT y FROM t2 WHERE x=a) FROM t1 WHERE b<8
        ]]
    end, {
        -- <subquery-1.1>
        1, 1, 3, 9, 5, 25
        -- </subquery-1.1>
    })

test:do_execsql_test(
    "subquery-1.2",
    [[
        UPDATE t1 SET b=b+(SELECT y FROM t2 WHERE x=a);
        SELECT * FROM t1;
    ]], {
        -- <subquery-1.2>
        1, 3, 3, 13, 5, 31, 7, 57
        -- </subquery-1.2>
    })

test:do_execsql_test(
    "subquery-1.3",
    [[
        SELECT b FROM t1 WHERE EXISTS(SELECT * FROM t2 WHERE y=a)
    ]], {
        -- <subquery-1.3>
        3
        -- </subquery-1.3>
    })

test:do_execsql_test(
    "subquery-1.4",
    [[
        SELECT b FROM t1 WHERE NOT EXISTS(SELECT * FROM t2 WHERE y=a)
    ]], {
        -- <subquery-1.4>
        13, 31, 57
        -- </subquery-1.4>
    })

-- Simple tests to make sure correlated subqueries in WHERE clauses
-- are used by the query optimizer correctly.
test:do_execsql_test(
    "subquery-1.5",
    [[
        SELECT a, x FROM t1, t2 WHERE t1.a = (SELECT x);
    ]], {
        -- <subquery-1.5>
        1, 1, 3, 3, 5, 5, 7, 7
        -- </subquery-1.5>
    })

test:do_execsql_test(
    "subquery-1.6",
    [[
        CREATE INDEX i1 ON t1(a);
        SELECT a, x FROM t1, t2 WHERE t1.a = (SELECT x);
    ]], {
        -- <subquery-1.6>
        1, 1, 3, 3, 5, 5, 7, 7
        -- </subquery-1.6>
    })

test:do_execsql_test(
    "subquery-1.7",
    [[
        SELECT a, x FROM t2, t1 WHERE t1.a = (SELECT x);
    ]], {
        -- <subquery-1.7>
        1, 1, 3, 3, 5, 5, 7, 7
        -- </subquery-1.7>
    })

-- Try an aggregate in both the subquery and the parent query.
test:do_execsql_test(
    "subquery-1.8",
    [[
        SELECT count(*) FROM t1 WHERE a > (SELECT count(*) FROM t2);
    ]], {
        -- <subquery-1.8>
        2
        -- </subquery-1.8>
    })

-- Test a correlated subquery disables the "only open the index" optimization.
test:do_execsql_test(
    "subquery-1.9.1",
    [[
        SELECT (y*2)>b FROM t1, t2 WHERE a=x;
    ]], {
        -- <subquery-1.9.1>
        false, true, true, true
        -- </subquery-1.9.1>
    })

test:do_execsql_test(
    "subquery-1.9.2",
    [[
        SELECT a FROM t1 WHERE (SELECT (y*2)>b FROM t2 WHERE a=x);
    ]], {
        -- <subquery-1.9.2>
        3, 5, 7
        -- </subquery-1.9.2>
    })

-- Test that the flattening optimization works with subquery expressions.
test:do_execsql_test(
    "subquery-1.10.1",
    [[
        SELECT (SELECT a), b FROM t1;
    ]], {
        -- <subquery-1.10.1>
        1, 3, 3, 13, 5, 31, 7, 57
        -- </subquery-1.10.1>
    })

test:do_execsql_test(
    "subquery-1.10.2",
    [[
        SELECT * FROM (SELECT (SELECT a), b FROM t1);
    ]], {
        -- <subquery-1.10.2>
        1, 3, 3, 13, 5, 31, 7, 57
        -- </subquery-1.10.2>
    })

test:do_execsql_test(
    "subquery-1.10.3",
    [[
        SELECT * FROM (SELECT (SELECT sum(a) FROM t1));
    ]], {
        -- <subquery-1.10.3>
        16
        -- </subquery-1.10.3>
    })

test:do_execsql_test(
    "subquery-1.10.4",
    [[
        CREATE TABLE t5 (val int, period text PRIMARY KEY);
        INSERT INTO t5 VALUES(5, '2001-3');
        INSERT INTO t5 VALUES(10, '2001-4');
        INSERT INTO t5 VALUES(15, '2002-1');
        INSERT INTO t5 VALUES(5, '2002-2');
        INSERT INTO t5 VALUES(10, '2002-3');
        INSERT INTO t5 VALUES(15, '2002-4');
        INSERT INTO t5 VALUES(10, '2003-1');
        INSERT INTO t5 VALUES(5, '2003-2');
        INSERT INTO t5 VALUES(25, '2003-3');
        INSERT INTO t5 VALUES(5, '2003-4');

        SELECT period, vsum
        FROM (SELECT
          a.period,
          (select sum(val) from t5 where period between a.period and '2002-4') vsum
          FROM t5 a where a.period between '2002-1' and '2002-4')
        WHERE vsum < 45 ;
    ]], {
        -- <subquery-1.10.4>
        "2002-2", 30, "2002-3", 25, "2002-4", 15
        -- </subquery-1.10.4>
    })

test:do_execsql_test(
    "subquery-1.10.5",
    [[
        SELECT period, vsum from
          (select a.period,
          (select sum(val) from t5 where period between a.period and '2002-4') vsum
        FROM t5 a where a.period between '2002-1' and '2002-4')
        WHERE vsum < 45 ;
    ]], {
        -- <subquery-1.10.5>
        "2002-2", 30, "2002-3", 25, "2002-4", 15
        -- </subquery-1.10.5>
    })

test:do_execsql_test(
    "subquery-1.10.6",
    [[
        DROP TABLE t5;
    ]], {
        -- <subquery-1.10.6>

        -- </subquery-1.10.6>
    })

--------------------------------------------------------------------
-- The following test cases - subquery-2.* - are not logically
-- organized. They're here largely because they were failing during
-- one stage of development of sub-queries.
--
test:do_execsql_test(
    "subquery-2.1",
    [[
        SELECT (SELECT 10);
    ]], {
        -- <subquery-2.1>
        10
        -- </subquery-2.1>
    })

test:do_execsql_test(
    "subquery-2.2.1",
    [[
        CREATE TABLE t3(a  INT PRIMARY KEY, b INT );
        INSERT INTO t3 VALUES(1, 2);
        INSERT INTO t3 VALUES(3, 1);
    ]], {
        -- <subquery-2.2.1>

        -- </subquery-2.2.1>
    })

test:do_execsql_test(
    "subquery-2.2.2",
    [[
        SELECT * FROM t3 WHERE a IN (SELECT b FROM t3);
    ]], {
        -- <subquery-2.2.2>
        1, 2
        -- </subquery-2.2.2>
    })

test:do_execsql_test(
    "subquery-2.2.3",
    [[
        DROP TABLE t3;
    ]], {
        -- <subquery-2.2.3>

        -- </subquery-2.2.3>
    })

test:do_execsql_test(
    "subquery-2.3.1",
    [[
        CREATE TABLE t3(a TEXT PRIMARY KEY);
        INSERT INTO t3 VALUES('10');
    ]], {
        -- <subquery-2.3.1>

        -- </subquery-2.3.1>
    })

test:do_execsql_test(
    "subquery-2.3.2",
    [[
        SELECT a IN (10.0, 20) FROM t3;
    ]], {
        -- <subquery-2.3.2>
        false
        -- </subquery-2.3.2>
    })

test:do_execsql_test(
    "subquery-2.3.3",
    [[
        DROP TABLE t3;
    ]], {
        -- <subquery-2.3.3>

        -- </subquery-2.3.3>
    })

test:do_execsql_test(
    "subquery-2.4.1",
    [[
        CREATE TABLE t3(a TEXT PRIMARY KEY);
        INSERT INTO t3 VALUES('XX');
    ]], {
        -- <subquery-2.4.1>

        -- </subquery-2.4.1>
    })

test:do_execsql_test(
    "subquery-2.4.2",
    [[
        SELECT count(*) FROM t3 WHERE a IN (SELECT 'XX')
    ]], {
        -- <subquery-2.4.2>
        1
        -- </subquery-2.4.2>
    })

test:do_execsql_test(
    "subquery-2.4.3",
    [[
        DROP TABLE t3;
    ]], {
        -- <subquery-2.4.3>

        -- </subquery-2.4.3>
    })

--------------------------------------------------------------------
-- The following test cases - subquery-3.* - test tickets that
-- were raised during development of correlated subqueries.
--
-- Ticket 1083
test:do_test(
    "subquery-3.1",
    function()
        test:catchsql " DROP TABLE t1; "
        test:catchsql " DROP TABLE t2; "
        return test:execsql [[
            CREATE TABLE t1(a  INT PRIMARY KEY,b INT );
            INSERT INTO t1 VALUES(1,2);
            CREATE VIEW v1 AS SELECT b FROM t1 WHERE a>0;
            CREATE TABLE t2(p  INT PRIMARY KEY,q INT );
            INSERT INTO t2 VALUES(2,9);
            SELECT * FROM v1 WHERE EXISTS(SELECT * FROM t2 WHERE p=v1.b);
        ]]
    end, {
        -- <subquery-3.1>
        2
        -- </subquery-3.1>
    })

test:do_execsql_test(
    "subquery-3.1.1",
    [[
        SELECT * FROM v1 WHERE EXISTS(SELECT 1);
    ]], {
        -- <subquery-3.1.1>
        2
        -- </subquery-3.1.1>
    })



-- Ticket 1084
test:do_test(
    "subquery-3.2",
    function()
        test:catchsql [[
            CREATE TABLE t1(a  INT PRIMARY KEY,b INT );
            INSERT INTO t1 VALUES(1,2);
        ]]
        return test:execsql [[
            SELECT (SELECT t1.a) FROM t1;
        ]]
    end, {
        -- <subquery-3.2>
        1
        -- </subquery-3.2>
    })

-- Test Cases subquery-3.3.* test correlated subqueries where the
-- parent query is an aggregate query. Ticket #1105 is an example
-- of such a query.
--
test:do_execsql_test(
    "subquery-3.3.1",
    [[
        SELECT a, (SELECT b) FROM t1 GROUP BY a;
    ]], {
        -- <subquery-3.3.1>
        1, 2
        -- </subquery-3.3.1>
    })

test:do_test(
    "subquery-3.3.2",
    function()
        test:catchsql "DROP TABLE t2"
        return test:execsql [[
            CREATE TABLE t2(c  INT PRIMARY KEY, d TEXT);
            INSERT INTO t2 VALUES(1, 'one');
            INSERT INTO t2 VALUES(2, 'two');
            SELECT a, (SELECT d FROM t2 WHERE a=c) FROM t1 GROUP BY a;
        ]]
    end, {
        -- <subquery-3.3.2>
        1, "one"
        -- </subquery-3.3.2>
    })

test:do_execsql_test(
    "subquery-3.3.3",
    [[
        INSERT INTO t1 VALUES(2, 4);
        SELECT max(a), (SELECT d FROM t2 WHERE a=c) FROM t1;
    ]], {
        -- <subquery-3.3.3>
        2, "two"
        -- </subquery-3.3.3>
    })

test:do_execsql_test(
    "subquery-3.3.4",
    [[
        SELECT a, (SELECT (SELECT d FROM t2 WHERE a=c)) FROM t1 GROUP BY a;
    ]], {
        -- <subquery-3.3.4>
        1, "one", 2, "two"
        -- </subquery-3.3.4>
    })

test:do_execsql_test(
    "subquery-3.3.5",
    [[
        SELECT a, (SELECT count(*) FROM t2 WHERE a=c) FROM t1;
    ]], {
        -- <subquery-3.3.5>
        1, 1, 2, 1
        -- </subquery-3.3.5>
    })

-- The following tests check for aggregate subqueries in an aggregate
-- query.
--
test:do_execsql_test(
    "subquery-3.4.1",
    [[
        CREATE TABLE t34(id  INT primary key, x INT ,y INT );
        INSERT INTO t34 VALUES(1, 106,4), (2, 107,3), (3, 106,5), (4, 107,5);
        SELECT a.x, avg(a.y)
          FROM t34 AS a
         GROUP BY a.x
         HAVING NOT EXISTS( SELECT b.x, avg(b.y)
                              FROM t34 AS b
                             GROUP BY b.x
                             HAVING avg(a.y) > avg(b.y));
    ]], {
        -- <subquery-3.4.1>
        107, 4.0
        -- </subquery-3.4.1>
    })

test:do_execsql_test(
    "subquery-3.4.2",
    [[
        SELECT a.x, avg(a.y) AS avg1
          FROM t34 AS a
         GROUP BY a.x
         HAVING NOT EXISTS( SELECT b.x, avg(b.y) AS avg2
                              FROM t34 AS b
                             GROUP BY b.x
                             HAVING avg1 > avg2);
    ]], {
        -- <subquery-3.4.2>
        107, 4.0
        -- </subquery-3.4.2>
    })

test:do_execsql_test(
    "subquery-3.4.3",
    [[
        SELECT
           a.x,
           avg(a.y),
           NOT EXISTS ( SELECT b.x, avg(b.y)
                          FROM t34 AS b
                          GROUP BY b.x
                         HAVING avg(a.y) > avg(b.y)),
           EXISTS ( SELECT c.x, avg(c.y)
                      FROM t34 AS c
                      GROUP BY c.x
                     HAVING avg(a.y) > avg(c.y))
          FROM t34 AS a
         GROUP BY a.x
         ORDER BY a.x;
    ]], {
        -- <subquery-3.4.3>
        106, 4.5, false, true, 107, 4.0, true, false
        -- </subquery-3.4.3>
    })

test:do_execsql_test(
    "subquery-3.5.1",
    [[
        CREATE TABLE t35a(x  INT PRIMARY KEY); INSERT INTO t35a VALUES(1),(2),(3);
        CREATE TABLE t35b(y  INT PRIMARY KEY); INSERT INTO t35b VALUES(98), (99);
        SELECT max((SELECT avg(y) FROM t35b)) FROM t35a;
    ]], {
        -- <subquery-3.5.1>
        98.5
        -- </subquery-3.5.1>
    })

test:do_execsql_test(
    "subquery-3.5.2",
    [[
        SELECT max((SELECT count(y) FROM t35b)) FROM t35a;
    ]], {
        -- <subquery-3.5.2>
        2
        -- </subquery-3.5.2>
    })

test:do_execsql_test(
    "subquery-3.5.3",
    [[
        SELECT max((SELECT count() FROM t35b)) FROM t35a;
    ]], {
        -- <subquery-3.5.3>
        2
        -- </subquery-3.5.3>
    })

test:do_catchsql_test(
    "subquery-3.5.4",
    [[
        SELECT max((SELECT count(x) FROM t35b)) FROM t35a;
    ]], {
        -- <subquery-3.5.4>
        1, "misuse of aggregate: COUNT()"
        -- </subquery-3.5.4>
    })

test:do_catchsql_test(
    "subquery-3.5.5",
    [[
        SELECT max((SELECT count(x) FROM t35b)) FROM t35a;
    ]], {
        -- <subquery-3.5.5>
        1, "misuse of aggregate: COUNT()"
        -- </subquery-3.5.5>
    })

test:do_catchsql_test(
    "subquery-3.5.6",
    [[
        SELECT max((SELECT a FROM (SELECT count(x) AS a FROM t35b))) FROM t35a;
    ]], {
        -- <subquery-3.5.6>
        1, "misuse of aggregate: COUNT()"
        -- </subquery-3.5.6>
    })

test:do_execsql_test(
    "subquery-3.5.7",
    [[
        SELECT max((SELECT a FROM (SELECT count(y) AS a FROM t35b))) FROM t35a;
    ]], {
        -- <subquery-3.5.7>
        2
        -- </subquery-3.5.7>
    })

--------------------------------------------------------------------
-- These tests - subquery-4.* - use the TCL statement cache to try
-- and expose bugs to do with re-using statements that have been
-- passed to sql_reset().
--
-- One problem was that VDBE memory cells were not being initialized
-- to NULL on the second and subsequent executions.
--
test:do_execsql_test(
    "subquery-4.1.1",
    [[
        SELECT (SELECT a FROM t1 LIMIT 1);
    ]], {
        -- <subquery-4.1.1>
        1
        -- </subquery-4.1.1>
    })

test:do_execsql_test(
    "subquery-4.2",
    [[
        DELETE FROM t1;
        SELECT (SELECT a FROM t1);
    ]], {
        -- <subquery-4.2>
        ""
        -- </subquery-4.2>
    })

test:do_test(
    "subquery-4.2.1",
    function()
        test:execsql [[
            CREATE TABLE t3(a  INT PRIMARY KEY);
            INSERT INTO t3 VALUES(10);
        ]]
        return test:execsql "INSERT INTO t3 VALUES((SELECT max(a) FROM t3)+1)"
    end, {
        -- <subquery-4.2.1>

        -- </subquery-4.2.1>
    })

test:do_execsql_test(
    "subquery-4.2.2",
    [[
        INSERT INTO t3 VALUES((SELECT max(a) FROM t3)+1)
    ]], {
        -- <subquery-4.2.2>

        -- </subquery-4.2.2>
    })

--------------------------------------------------------------------
-- The subquery-5.* tests make sure string literals in double-quotes
-- are handled efficiently.  Double-quote literals are first checked
-- to see if they match any column names.  If there is not column name
-- match then those literals are used a string constants.  When a
-- double-quoted string appears, we want to make sure that the search
-- for a matching column name did not cause an otherwise static subquery
-- to become a dynamic (correlated) subquery.
--

_G.callcnt = 0
test:do_test(
    "subquery-5.1",
    function()
        box.schema.func.create('CALLCNT', {language = 'Lua',
                               is_deterministic = true,
                               param_list = {'integer'}, returns = 'integer',
                               exports = {'LUA', 'SQL'},
                               body = [[
                                   function(n)
                                           callcnt = callcnt + 1
                                           return n
                                   end
                               ]]})
        return test:execsql [[
            CREATE TABLE t4(x TEXT,y  INT PRIMARY KEY);
            INSERT INTO t4 VALUES('one',1);
            INSERT INTO t4 VALUES('two',2);
            INSERT INTO t4 VALUES('three',3);
            INSERT INTO t4 VALUES('four',4);
            CREATE TABLE t5(a  INT PRIMARY KEY,b INT );
            INSERT INTO t5 VALUES(1,11);
            INSERT INTO t5 VALUES(2,22);
            INSERT INTO t5 VALUES(3,33);
            INSERT INTO t5 VALUES(4,44);
            SELECT b FROM t5 WHERE a IN
               (SELECT callcnt(y)+0 FROM t4 WHERE x='two')
        ]]
    end, {
        -- <subquery-5.1>
        22
        -- </subquery-5.1>
    })

test:do_test(
    "subquery-5.2",
    function()
        -- This is the key test.  The subquery should have only run once.  If
        -- The double-quoted identifier "two" were causing the subquery to be
        -- processed as a correlated subquery, then it would have run 4 times.

        return _G.callcnt
    end, 1)

-- Ticket #1380.  Make sure correlated subqueries on an IN clause work
-- correctly when the left-hand side of the IN operator is constant.
--
test:do_test(
    "subquery-6.1",
    function()
        _G.callcnt = 0
        return test:execsql [[
            SELECT x FROM t4 WHERE 1 IN (SELECT callcnt(count(*)) FROM t5 WHERE a=y)
        ]]
    end, {
        -- <subquery-6.1>
        "one", "two", "three", "four"
        -- </subquery-6.1>
    })

test:do_test(
    "subquery-6.2",
    function()
        return _G.callcnt

    end, 4)

test:do_test(
    "subquery-6.3",
    function()
        _G.callcnt = 0
        return test:execsql [[
            SELECT x FROM t4 WHERE 1 IN (SELECT callcnt(count(*)) FROM t5 WHERE a=1)
        ]]
    end, {
        -- <subquery-6.3>
        "one", "two", "three", "four"
        -- </subquery-6.3>
    })

test:do_test(
    "subquery-6.4",
    function()
        return _G.callcnt
    end, 1)

box.func.CALLCNT:drop()

--############  was disable until we get #2652 fixed
-- Ticket #2652.  Allow aggregate functions of outer queries inside
-- a non-aggregate subquery.
--
test:do_execsql_test(
    "subquery-7.1",
    [[
        CREATE TABLE t7(c7  INT PRIMARY KEY);
        INSERT INTO t7 VALUES(1);
        INSERT INTO t7 VALUES(2);
        INSERT INTO t7 VALUES(3);
        CREATE TABLE t8(c8  INT PRIMARY KEY);
        INSERT INTO t8 VALUES(100);
        INSERT INTO t8 VALUES(200);
        INSERT INTO t8 VALUES(300);
        CREATE TABLE t9(c9  INT PRIMARY KEY);
        INSERT INTO t9 VALUES(10000);
        INSERT INTO t9 VALUES(20000);
        INSERT INTO t9 VALUES(30000);

        SELECT (SELECT c7+c8 FROM t7 LIMIT 1) FROM t8;
    ]], {
        -- <subquery-7.1>
        101, 201, 301
        -- </subquery-7.1>
    })

test:do_execsql_test(
    "subquery-7.2",
    [[
        SELECT (SELECT max(c7)+c8 FROM t7) FROM t8;
    ]], {
        -- <subquery-7.2>
        103, 203, 303
        -- </subquery-7.2>
    })

test:do_execsql_test(
    "subquery-7.3",
    [[
        SELECT (SELECT c7+max(c8) FROM t8) FROM t7
    ]], {
        -- <subquery-7.3>
        301,302,303
        -- </subquery-7.3>
    })

test:do_execsql_test(
    "subquery-7.4",
    [[
        SELECT (SELECT max(c7)+max(c8) FROM t8) FROM t7
    ]], {
        -- <subquery-7.4>
        303
        -- </subquery-7.4>
    })


test:do_execsql_test(
    "subquery-7.6",
    [[
        SELECT (SELECT (SELECT max(c7+c8+c9) FROM t9 LIMIT 1) FROM t8 LIMIT 1) FROM t7
    ]], {
        -- <subquery-7.6>
        30101, 30102, 30103
        -- </subquery-7.6>
    })

test:do_execsql_test(
    "subquery-7.7",
    [[
        SELECT (SELECT (SELECT c7+max(c8+c9) FROM t9 LIMIT 1) FROM t8 LIMIT 1) FROM t7
    ]], {
        -- <subquery-7.7>
        30101, 30102, 30103
        -- </subquery-7.7>
    })

test:do_execsql_test(
    "subquery-7.8",
    [[
        SELECT (SELECT (SELECT max(c7)+c8+c9 FROM t9 LIMIT 1) FROM t8 LIMIT 1) FROM t7
    ]], {
        -- <subquery-7.8>
        10103
        -- </subquery-7.8>
    })

test:do_execsql_test(
    "subquery-7.9",
    [[
        SELECT (SELECT (SELECT c7+max(c8)+c9 FROM t9 LIMIT 1) FROM t8 LIMIT 1) FROM t7
    ]], {
        -- <subquery-7.9>
        10301, 10302, 10303
        -- </subquery-7.9>
    })

test:do_execsql_test(
    "subquery-7.10",
    [[
        SELECT (SELECT (SELECT c7+c8+max(c9) FROM t9 LIMIT 1) FROM t8 LIMIT 1) FROM t7
    ]], {
        -- <subquery-7.10>
        30101, 30102, 30103
        -- </subquery-7.10>
    })

test:do_execsql_test(
    "subquery-7.11",
    [[
        SELECT (SELECT (SELECT max(c7)+max(c8)+max(c9) FROM t9) FROM t8) FROM t7
    ]], {
        -- <subquery-7.11>
        30303
        -- </subquery-7.11>
    })

--############ Disabled
-- 2015-04-21.
-- Verify that a memory leak in the table column type and collation analysis
-- is plugged.
--

test:do_execsql_test(
    "subquery-8.1",
    [[
        SELECT (SELECT 0 FROM (SELECT * FROM t1)) AS x WHERE x <> 0;
        SELECT (SELECT 0 FROM (SELECT * FROM (SELECT 0))) AS x WHERE x <> 0;
    ]], {
        -- <subquery-8.1>

        -- </subquery-8.1>
    })

test:do_catchsql_test(
	"subquery-9.0",
	[[
		DROP TABLE IF EXISTS table1;
		CREATE TABLE table1 (id VARCHAR(100) PRIMARY KEY);
		INSERT INTO table1 VALUES ('abc'), ('abd');
	]], {
		-- <subquery-9.0>
		0
		-- <subquery-9.0>
	})

test:do_catchsql_test(
	"subquery-9.1",
	[[
		SELECT * FROM (SELECT * FROM table1 UNION ALL
                               SELECT * FROM table1 ORDER BY 1 UNION ALL
			       SELECT * FROM table1);
	]], {
		-- <subquery-9.1>
		1, 'ORDER BY clause should come after UNION ALL not before'
		-- <subquery-9.1>
	})

test:do_execsql_test(
	"subquery-9.2",
	[[
		SELECT * FROM (SELECT * FROM table1 UNION ALL
                                   SELECT * FROM table1 UNION ALL
				   SELECT * FROM table1 ORDER BY 1);
	]], {
		-- <subquery-9.2>
		'abc', 'abc', 'abc', 'abd', 'abd', 'abd'
		-- <subquery-9.2>
	})

test:do_catchsql_test(
	"subquery-9.3",
	[[
		SELECT * FROM (SELECT * FROM table1 ORDER BY 1 UNION ALL
                               SELECT * FROM table1 UNION ALL
			       SELECT * FROM table1);
	]], {
             -- <subquery-9.3>
             1, 'ORDER BY clause should come after UNION ALL not before'
             -- <subquery-9.3>
	})

test:finish_test()
