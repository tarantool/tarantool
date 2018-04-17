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
-- focus of this file is testing the IN and BETWEEN operator.
--
-- $Id: in.test,v 1.22 2008/08/04 03:51:24 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Generate the test data we will need for the first squences of tests.
--
test:do_test(
    "in-1.0",
    function()
        test:execsql [[
            CREATE TABLE t1(a PRIMARY KEY, b);
            BEGIN;
        ]]
        -- for _ in X(0, "X!for", [=[["set i 1","$i<=10","incr i"]]=]) do
        local j = 1
        for i=1,10 do
            j = j * 2
            -- test:execsql(string.format("INSERT INTO t1 VALUES(%s,%s)", i, X(0, "X!expr", [=[["<<",1,["i"]]]=])))
            test:execsql(string.format("INSERT INTO t1 VALUES(%s,%s)", i, j))
        end
        return test:execsql [[
            COMMIT;
            SELECT count(*) FROM t1;
        ]]
    end, {
        -- <in-1.0>
        10
        -- </in-1.0>
    })

-- Do basic testing of BETWEEN.
--
test:do_execsql_test(
    "in-1.1",
    [[
        SELECT a FROM t1 WHERE b BETWEEN 10 AND 50 ORDER BY a
    ]], {
        -- <in-1.1>
        4, 5
        -- </in-1.1>
    })

test:do_execsql_test(
    "in-1.2",
    [[
        SELECT a FROM t1 WHERE b NOT BETWEEN 10 AND 50 ORDER BY a
    ]], {
        -- <in-1.2>
        1, 2, 3, 6, 7, 8, 9, 10
        -- </in-1.2>
    })

test:do_execsql_test(
    "in-1.3",
    [[
        SELECT a FROM t1 WHERE b BETWEEN a AND a*5 ORDER BY a
    ]], {
        -- <in-1.3>
        1, 2, 3, 4
        -- </in-1.3>
    })

test:do_execsql_test(
    "in-1.4",
    [[
        SELECT a FROM t1 WHERE b NOT BETWEEN a AND a*5 ORDER BY a
    ]], {
        -- <in-1.4>
        5, 6, 7, 8, 9, 10
        -- </in-1.4>
    })

test:do_execsql_test(
    "in-1.6",
    [[
        SELECT a FROM t1 WHERE b BETWEEN a AND a*5 OR b=512 ORDER BY a
    ]], {
        -- <in-1.6>
        1, 2, 3, 4, 9
        -- </in-1.6>
    })

test:do_execsql_test(
    "in-1.7",
    [[
        SELECT a+ 100*(a BETWEEN 1 and 3) FROM t1 ORDER BY b
    ]], {
        -- <in-1.7>
        101, 102, 103, 4, 5, 6, 7, 8, 9, 10
        -- </in-1.7>
    })

--
-- The rest of this file concentrates on testing the IN operator.
--


-- Testing of the IN operator using static lists on the right-hand side.
--
test:do_execsql_test(
    "in-2.1",
    [[
        SELECT a FROM t1 WHERE b IN (8,12,16,24,32) ORDER BY a
    ]], {
        -- <in-2.1>
        3, 4, 5
        -- </in-2.1>
    })

test:do_execsql_test(
    "in-2.2",
    [[
        SELECT a FROM t1 WHERE b NOT IN (8,12,16,24,32) ORDER BY a
    ]], {
        -- <in-2.2>
        1, 2, 6, 7, 8, 9, 10
        -- </in-2.2>
    })

test:do_execsql_test(
    "in-2.3",
    [[
        SELECT a FROM t1 WHERE b IN (8,12,16,24,32) OR b=512 ORDER BY a
    ]], {
        -- <in-2.3>
        3, 4, 5, 9
        -- </in-2.3>
    })

test:do_execsql_test(
    "in-2.4",
    [[
        SELECT a FROM t1 WHERE b NOT IN (8,12,16,24,32) OR b=512 ORDER BY a
    ]], {
        -- <in-2.4>
        1, 2, 6, 7, 8, 9, 10
        -- </in-2.4>
    })

test:do_execsql_test(
    "in-2.5",
    [[
        SELECT a+100*(b IN (8,16,24)) FROM t1 ORDER BY b
    ]], {
        -- <in-2.5>
        1, 2, 103, 104, 5, 6, 7, 8, 9, 10
        -- </in-2.5>
    })

test:do_execsql_test(
    "in-2.6",
    [[
        SELECT a FROM t1 WHERE b IN (b+8,64)
    ]], {
        -- <in-2.6>
        6
        -- </in-2.6>
    })

test:do_execsql_test(
    "in-2.7",
    [[
        SELECT a FROM t1 WHERE b IN (max(5,10,b),20)
    ]], {
        -- <in-2.7>
        4, 5, 6, 7, 8, 9, 10
        -- </in-2.7>
    })

test:do_execsql_test(
    "in-2.8",
    [[
        SELECT a FROM t1 WHERE b IN (8*2,64/2) ORDER BY b
    ]], {
        -- <in-2.8>
        4, 5
        -- </in-2.8>
    })

test:do_execsql_test(
    "in-2.9",
    [[
        SELECT a FROM t1 WHERE b IN (max(5,10),20)
    ]], {
        -- <in-2.9>
        
        -- </in-2.9>
    })

test:do_execsql_test(
    "in-2.10",
    [[
        SELECT a FROM t1 WHERE min(0,b IN (a,30))
    ]], {
        -- <in-2.10>
        
        -- </in-2.10>
    })

test:do_catchsql_test(
    "in-2.11",
    [[
        SELECT a FROM t1 WHERE c IN (10,20)
    ]], {
        -- <in-2.11>
        1, "no such column: C"
        -- </in-2.11>
    })

-- Testing the IN operator where the right-hand side is a SELECT
--
test:do_execsql_test(
    "in-3.1",
    [[
        SELECT a FROM t1
        WHERE b IN (SELECT b FROM t1 WHERE a<5)
        ORDER BY a
    ]], {
        -- <in-3.1>
        1, 2, 3, 4
        -- </in-3.1>
    })

test:do_execsql_test(
    "in-3.2",
    [[
        SELECT a FROM t1
        WHERE b IN (SELECT b FROM t1 WHERE a<5) OR b==512
        ORDER BY a
    ]], {
        -- <in-3.2>
        1, 2, 3, 4, 9
        -- </in-3.2>
    })

test:do_execsql_test(
    "in-3.3",
    [[
        SELECT a + 100*(b IN (SELECT b FROM t1 WHERE a<5)) FROM t1 ORDER BY b
    ]], {
        -- <in-3.3>
        101, 102, 103, 104, 5, 6, 7, 8, 9, 10
        -- </in-3.3>
    })

-- Make sure the UPDATE and DELETE commands work with IN-SELECT
--
test:do_test(
    "in-4.1",
    function()
        test:execsql [[
            UPDATE t1 SET b=b*2 
            WHERE b IN (SELECT b FROM t1 WHERE a>8)
        ]]
        return test:execsql "SELECT b FROM t1 ORDER BY b"
    end, {
        -- <in-4.1>
        2, 4, 8, 16, 32, 64, 128, 256, 1024, 2048
        -- </in-4.1>
    })

test:do_test(
    "in-4.2",
    function()
        test:execsql [[
            DELETE FROM t1 WHERE b IN (SELECT b FROM t1 WHERE a>8)
        ]]
        return test:execsql "SELECT a FROM t1 ORDER BY a"
    end, {
        -- <in-4.2>
        1, 2, 3, 4, 5, 6, 7, 8
        -- </in-4.2>
    })

test:do_test(
    "in-4.3",
    function()
        test:execsql [[
            DELETE FROM t1 WHERE b NOT IN (SELECT b FROM t1 WHERE a>4)
        ]]
        return test:execsql "SELECT a FROM t1 ORDER BY a"
    end, {
        -- <in-4.3>
        5, 6, 7, 8
        -- </in-4.3>
    })

-- Do an IN with a constant RHS but where the RHS has many, many
-- elements.  We need to test that collisions in the hash table
-- are resolved properly.
--
test:do_execsql_test(
    "in-5.1",
    [[
        INSERT INTO t1 VALUES('hello', 'world');
        SELECT * FROM t1
        WHERE a IN (
           'Do','an','IN','with','a','constant','RHS','but','where','the',
           'has','many','elements','We','need','to','test','that',
           'collisions','hash','table','are','resolved','properly',
           'This','in-set','contains','thirty','one','entries','hello');
    ]], {
        -- <in-5.1>
        "hello", "world"
        -- </in-5.1>
    })

-- Make sure the IN operator works with INTEGER PRIMARY KEY fields.
--
test:do_execsql_test(
    "in-6.1",
    [[
        CREATE TABLE ta(a INTEGER PRIMARY KEY, b);
        INSERT INTO ta VALUES(1,1);
        INSERT INTO ta VALUES(2,2);
        INSERT INTO ta VALUES(3,3);
        INSERT INTO ta VALUES(4,4);
        INSERT INTO ta VALUES(6,6);
        INSERT INTO ta VALUES(8,8);
        INSERT INTO ta VALUES(10,
           'This is a key that is long enough to require a malloc in the VDBE');
        SELECT * FROM ta WHERE a<10;
    ]], {
        -- <in-6.1>
        1, 1, 2, 2, 3, 3, 4, 4, 6, 6, 8, 8
        -- </in-6.1>
    })

test:do_execsql_test(
    "in-6.2",
    [[
        CREATE TABLE tb(a INTEGER PRIMARY KEY, b);
        INSERT INTO tb VALUES(1,1);
        INSERT INTO tb VALUES(2,2);
        INSERT INTO tb VALUES(3,3);
        INSERT INTO tb VALUES(5,5);
        INSERT INTO tb VALUES(7,7);
        INSERT INTO tb VALUES(9,9);
        INSERT INTO tb VALUES(11,
           'This is a key that is long enough to require a malloc in the VDBE');
        SELECT * FROM tb WHERE a<10;
    ]], {
        -- <in-6.2>
        1, 1, 2, 2, 3, 3, 5, 5, 7, 7, 9, 9
        -- </in-6.2>
    })

test:do_execsql_test(
    "in-6.3",
    [[
        SELECT a FROM ta WHERE b IN (SELECT a FROM tb);
    ]], {
        -- <in-6.3>
        1, 2, 3
        -- </in-6.3>
    })

test:do_execsql_test(
    "in-6.4",
    [[
        SELECT a FROM ta WHERE b NOT IN (SELECT a FROM tb);
    ]], {
        -- <in-6.4>
        4, 6, 8, 10
        -- </in-6.4>
    })

test:do_execsql_test(
    "in-6.5",
    [[
        SELECT a FROM ta WHERE b IN (SELECT b FROM tb);
    ]], {
        -- <in-6.5>
        1, 2, 3, 10
        -- </in-6.5>
    })

test:do_execsql_test(
    "in-6.6",
    [[
        SELECT a FROM ta WHERE b NOT IN (SELECT b FROM tb);
    ]], {
        -- <in-6.6>
        4, 6, 8
        -- </in-6.6>
    })

test:do_execsql_test(
    "in-6.7",
    [[
        SELECT a FROM ta WHERE a IN (SELECT a FROM tb);
    ]], {
        -- <in-6.7>
        1, 2, 3
        -- </in-6.7>
    })

test:do_execsql_test(
    "in-6.8",
    [[
        SELECT a FROM ta WHERE a NOT IN (SELECT a FROM tb);
    ]], {
        -- <in-6.8>
        4, 6, 8, 10
        -- </in-6.8>
    })

test:do_execsql_test(
    "in-6.9",
    [[
        SELECT a FROM ta WHERE a IN (SELECT b FROM tb);
    ]], {
        -- <in-6.9>
        1, 2, 3
        -- </in-6.9>
    })

test:do_execsql_test(
    "in-6.10",
    [[
        SELECT a FROM ta WHERE a NOT IN (SELECT b FROM tb);
    ]], {
        -- <in-6.10>
        4, 6, 8, 10
        -- </in-6.10>
    })

-- Tests of IN operator against empty sets.  (Ticket #185)
--
test:do_execsql_test(
    "in-7.1",
    [[
        SELECT a FROM t1 WHERE a IN ();
    ]], {
        -- <in-7.1>
        
        -- </in-7.1>
    })

test:do_execsql_test(
    "in-7.2",
    [[
        SELECT a FROM t1 WHERE a IN (5);
    ]], {
        -- <in-7.2>
        5
        -- </in-7.2>
    })

test:do_execsql_test(
    "in-7.3",
    [[
        SELECT a FROM t1 WHERE a NOT IN () ORDER BY a;
    ]], {
        -- <in-7.3>
        5, 6, 7, 8, "hello"
        -- </in-7.3>
    })

test:do_execsql_test(
    "in-7.4",
    [[
        SELECT a FROM t1 WHERE a IN (5) AND b IN ();
    ]], {
        -- <in-7.4>
        
        -- </in-7.4>
    })

test:do_execsql_test(
    "in-7.5",
    [[
        SELECT a FROM t1 WHERE a IN (5) AND b NOT IN ();
    ]], {
        -- <in-7.5>
        5
        -- </in-7.5>
    })

test:do_execsql_test(
    "in-7.6.1",
    [[
        SELECT a FROM ta WHERE a IN ();
    ]], {
        -- <in-7.6.1>
        
        -- </in-7.6.1>
    })

-- Tarantool: not supported yet
-- test:do_test(
--     "in-7.6.2",
--     function()
--         return db("status", "step")
--     end, {
--         -- <in-7.6.2>
--         0
--         -- </in-7.6.2>
--     })

test:do_execsql_test(
    "in-7.7",
    [[
        SELECT a FROM ta WHERE a NOT IN ();
    ]], {
        -- <in-7.7>
        1, 2, 3, 4, 6, 8, 10
        -- </in-7.7>
    })

test:do_execsql_test(
    "in-7.8.1",
    [[
        SELECT * FROM ta LEFT JOIN tb ON (ta.b=tb.b) WHERE ta.a IN ();
    ]], {
        -- <in-7.8.1>
        
        -- </in-7.8.1>
    })

-- Tarantool: not supported yet
-- test:do_test(
--     "in-7.8.2",
--     function()
--         return db("status", "step")
--     end, {
--         -- <in-7.8.2>
--         0
--         -- </in-7.8.2>
--     })

test:do_execsql_test(
    "in-8.1",
    [[
        SELECT b FROM t1 WHERE a IN ('hello','there')
    ]], {
        -- <in-8.1>
        "world"
        -- </in-8.1>
    })

-- Test constructs of the form:  expr IN tablename
--
test:do_execsql_test(
    "in-9.1",
    [[
        CREATE TABLE t4 (a INTEGER PRIMARY KEY);
        INSERT INTO t4 SELECT a FROM tb;
        SELECT * FROM t4;    
    ]], {
        -- <in-9.1>
        1, 2, 3, 5, 7, 9, 11
        -- </in-9.1>
    })

test:do_execsql_test(
    "in-9.2",
    [[
        SELECT b FROM t1 WHERE a IN t4;
    ]], {
        -- <in-9.2>
        32, 128
        -- </in-9.2>
    })

test:do_execsql_test(
    "in-9.3",
    [[
        SELECT b FROM t1 WHERE a NOT IN t4;
    ]], {
        -- <in-9.3>
        64, 256, "world"
        -- </in-9.3>
    })

test:do_catchsql_test(
    "in-9.4",
    [[
        SELECT b FROM t1 WHERE a NOT IN tb;
    ]], {
        -- <in-9.4>
        1, "sub-select returns 2 columns - expected 1"
        -- </in-9.4>
    })

-- MUST_WORK_TEST
-- # IN clauses in CHECK constraints.  Ticket #1645
-- #
-- do_test in-10.1 {
--   execsql {
--     CREATE TABLE t5(
--       a INTEGER,
--       CHECK( a IN (111,222,333) )
--     );
--     INSERT INTO t5 VALUES(111);
--     SELECT * FROM t5;
--   }
-- } {111}
-- do_test in-10.2 {
--   catchsql {
--     INSERT INTO t5 VALUES(4);
--   }
-- } {1 {CHECK constraint failed: t5}}
-- Ticket #1821
--
-- Type affinity applied to the right-hand side of an IN operator.
--
test:do_execsql_test(
    "in-11.1",
    [[
        CREATE TABLE t6(a PRIMARY KEY,b NUMERIC);
        INSERT INTO t6 VALUES(1,2);
        INSERT INTO t6 VALUES(2,3);
        SELECT * FROM t6 WHERE b IN (2);
    ]], {
        -- <in-11.1>
        1, 2
        -- </in-11.1>
    })

test:do_test(
    "in-11.2",
    function()
        -- The '2' should be coerced into 2 because t6.b is NUMERIC
        return test:execsql [[
            SELECT * FROM t6 WHERE b IN ('2');
        ]]
    end, {
        -- <in-11.2>
        1, 2
        -- </in-11.2>
    })

test:do_test(
    "in-11.3",
    function()
        -- No coercion should occur here because of the unary + before b.
        return test:execsql [[
            SELECT * FROM t6 WHERE +b IN ('2');
        ]]
    end, {
        -- <in-11.3>
        
        -- </in-11.3>
    })

test:do_test(
    "in-11.4",
    function()
        -- No coercion because column a as affinity NONE
        return test:execsql [[
            SELECT * FROM t6 WHERE a IN ('2');
        ]]
    end, {
        -- <in-11.4>
        
        -- </in-11.4>
    })

test:do_execsql_test(
    "in-11.5",
    [[
        SELECT * FROM t6 WHERE a IN (2);
    ]], {
        -- <in-11.5>
        2, 3
        -- </in-11.5>
    })

test:do_test(
    "in-11.6",
    function()
        -- No coercion because column a as affinity NONE
        return test:execsql [[
            SELECT * FROM t6 WHERE +a IN ('2');
        ]]
    end, {
        -- <in-11.6>
        
        -- </in-11.6>
    })

-- Test error conditions with expressions of the form IN(<compound select>).
--
test:do_execsql_test(
    "in-12.1",
    [[
        CREATE TABLE t2(a PRIMARY KEY, b, c);
        CREATE TABLE t3(a PRIMARY KEY, b, c);
    ]], {
        -- <in-12.1>
        
        -- </in-12.1>
    })

test:do_catchsql_test(
    "in-12.2",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a, b FROM t3 UNION ALL SELECT a, b FROM t2
        );
    ]], {
        -- <in-12.2>
        1, "sub-select returns 2 columns - expected 1"
        -- </in-12.2>
    })

test:do_catchsql_test(
    "in-12.3",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a, b FROM t3 UNION SELECT a, b FROM t2
        );
    ]], {
        -- <in-12.3>
        1, "sub-select returns 2 columns - expected 1"
        -- </in-12.3>
    })

test:do_catchsql_test(
    "in-12.4",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a, b FROM t3 EXCEPT SELECT a, b FROM t2
        );
    ]], {
        -- <in-12.4>
        1, "sub-select returns 2 columns - expected 1"
        -- </in-12.4>
    })

test:do_catchsql_test(
    "in-12.5",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a, b FROM t3 INTERSECT SELECT a, b FROM t2
        );
    ]], {
        -- <in-12.5>
        1, "sub-select returns 2 columns - expected 1"
        -- </in-12.5>
    })

test:do_catchsql_test(
    "in-12.6",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a, b FROM t3 UNION ALL SELECT a FROM t2
        );
    ]], {
        -- <in-12.6>
        1, "SELECTs to the left and right of UNION ALL do not have the same number of result columns"
        -- </in-12.6>
    })

test:do_catchsql_test(
    "in-12.7",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a, b FROM t3 UNION SELECT a FROM t2
        );
    ]], {
        -- <in-12.7>
        1, "SELECTs to the left and right of UNION do not have the same number of result columns"
        -- </in-12.7>
    })

test:do_catchsql_test(
    "in-12.8",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a, b FROM t3 EXCEPT SELECT a FROM t2
        );
    ]], {
        -- <in-12.8>
        1, "SELECTs to the left and right of EXCEPT do not have the same number of result columns"
        -- </in-12.8>
    })

test:do_catchsql_test(
    "in-12.9",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a, b FROM t3 INTERSECT SELECT a FROM t2
        );
    ]], {
        -- <in-12.9>
        1, "SELECTs to the left and right of INTERSECT do not have the same number of result columns"
        -- </in-12.9>
    })



test:do_catchsql_test(
    "in-12.10",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a FROM t3 UNION ALL SELECT a, b FROM t2
        );
    ]], {
        -- <in-12.10>
        1, "SELECTs to the left and right of UNION ALL do not have the same number of result columns"
        -- </in-12.10>
    })

test:do_catchsql_test(
    "in-12.11",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a FROM t3 UNION SELECT a, b FROM t2
        );
    ]], {
        -- <in-12.11>
        1, "SELECTs to the left and right of UNION do not have the same number of result columns"
        -- </in-12.11>
    })

test:do_catchsql_test(
    "in-12.12",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a FROM t3 EXCEPT SELECT a, b FROM t2
        );
    ]], {
        -- <in-12.12>
        1, "SELECTs to the left and right of EXCEPT do not have the same number of result columns"
        -- </in-12.12>
    })

test:do_catchsql_test(
    "in-12.13",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a FROM t3 INTERSECT SELECT a, b FROM t2
        );
    ]], {
        -- <in-12.13>
        1, "SELECTs to the left and right of INTERSECT do not have the same number of result columns"
        -- </in-12.13>
    })

test:do_catchsql_test(
    "in-12.14",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a, b FROM t3 UNION ALL SELECT a, b FROM t2
        );
    ]], {
        -- <in-12.14>
        1, "sub-select returns 2 columns - expected 1"
        -- </in-12.14>
    })

test:do_catchsql_test(
    "in-12.15",
    [[
        SELECT * FROM t2 WHERE a IN (
          SELECT a, b FROM t3 UNION ALL SELECT a FROM t2
        );
    ]], {
        -- <in-12.15>
        1, "SELECTs to the left and right of UNION ALL do not have the same number of result columns"
        -- </in-12.15>
    })



--ifcapable compound
--------------------------------------------------------------------------
-- The following tests check that NULL is handled correctly when it 
-- appears as part of a set of values on the right-hand side of an
-- IN or NOT IN operator.
--
-- When it appears in such a set, NULL is handled as an "unknown value".
-- If, because of the unknown value in the set, the result of the expression 
-- cannot be determined, then it itself evaluates to NULL.
--
-- Warm body test to demonstrate the principles being tested:
--
test:do_test(
    "in-13.1",
    function()
        --  db("nullvalue", '')
        return test:execsql [[ SELECT 
    1 IN (NULL, 1, 2),     -- The value 1 is a member of the set, return true.
    3 IN (NULL, 1, 2),     -- Ambiguous, return NULL.
    1 NOT IN (NULL, 1, 2), -- The value 1 is a member of the set, return false.
    3 NOT IN (NULL, 1, 2)  -- Ambiguous, return NULL.
  ]]
    end, {
        -- <in-13.1>
        1, '', 0, ''
        -- </in-13.1>
    })

test:do_execsql_test(
    "in-13.2",
    [[
        CREATE TABLE t7(id primary key, a, b, c NOT NULL);
        INSERT INTO t7 VALUES(1, 1,    1, 1);
        INSERT INTO t7 VALUES(2, 2,    2, 2);
        INSERT INTO t7 VALUES(3, 3,    3, 3);
        INSERT INTO t7 VALUES(4, NULL, 4, 4);
        INSERT INTO t7 VALUES(5, NULL, 5, 5);
    ]], {
        -- <in-13.2>
        
        -- </in-13.2>
    })

test:do_execsql_test(
    "in-13.3",
    [[
        SELECT 2 IN (SELECT a FROM t7) 
    ]], {
        -- <in-13.3>
        1
        -- </in-13.3>
    })

test:do_execsql_test(
    "in-13.4",
    [[
        SELECT 6 IN (SELECT a FROM t7) 
    ]], {
        -- <in-13.4>
        ''
        -- </in-13.4>
    })

test:do_execsql_test(
    "in-13.5",
    [[
        SELECT 2 IN (SELECT b FROM t7) 
    ]], {
        -- <in-13.5>
        1
        -- </in-13.5>
    })

test:do_execsql_test(
    "in-13.6",
    [[
        SELECT 6 IN (SELECT b FROM t7) 
    ]], {
        -- <in-13.6>
        0
        -- </in-13.6>
    })

test:do_execsql_test(
    "in-13.7",
    [[
        SELECT 2 IN (SELECT c FROM t7) 
    ]], {
        -- <in-13.7>
        1
        -- </in-13.7>
    })

test:do_execsql_test(
    "in-13.8",
    [[
        SELECT 6 IN (SELECT c FROM t7) 
    ]], {
        -- <in-13.8>
        0
        -- </in-13.8>
    })

test:do_execsql_test(
    "in-13.9",
    [[
        SELECT
          2 NOT IN (SELECT a FROM t7),
          6 NOT IN (SELECT a FROM t7),
          2 NOT IN (SELECT b FROM t7),
          6 NOT IN (SELECT b FROM t7),
          2 NOT IN (SELECT c FROM t7),
          6 NOT IN (SELECT c FROM t7)
    ]], {
        -- <in-13.9>
        0, '', 0, 1, 0, 1
        -- </in-13.9>
    })

test:do_execsql_test(
    "in-13.10",
    [[
        SELECT b IN (
          SELECT inside.a 
          FROM t7 AS inside 
          WHERE inside.b BETWEEN outside.b+1 AND outside.b+2
        )
        FROM t7 AS outside ORDER BY b;
    ]], {
        -- <in-13.10>
        0, '', '', '', 0
        -- </in-13.10>
    })

test:do_execsql_test(
    "in-13.11",
    [[
        SELECT b NOT IN (
          SELECT inside.a 
          FROM t7 AS inside 
          WHERE inside.b BETWEEN outside.b+1 AND outside.b+2
        )
        FROM t7 AS outside ORDER BY b;
    ]], {
        -- <in-13.11>
        1, '', '', '', 1
        -- </in-13.11>
    })

test:do_test(
    "in-13.12",
    function()
        test:execsql [[
            CREATE INDEX i1 ON t7(a);
            CREATE INDEX i2 ON t7(b);
            CREATE INDEX i3 ON t7(c);
        ]]
        return test:execsql [[
            SELECT
              2 IN (SELECT a FROM t7),
              6 IN (SELECT a FROM t7),
              2 IN (SELECT b FROM t7),
              6 IN (SELECT b FROM t7),
              2 IN (SELECT c FROM t7),
              6 IN (SELECT c FROM t7)
        ]]
    end, {
        -- <in-13.12>
        1, '', 1, 0, 1, 0
        -- </in-13.12>
    })

test:do_execsql_test(
    "in-13.13",
    [[
        SELECT
          2 NOT IN (SELECT a FROM t7),
          6 NOT IN (SELECT a FROM t7),
          2 NOT IN (SELECT b FROM t7),
          6 NOT IN (SELECT b FROM t7),
          2 NOT IN (SELECT c FROM t7),
          6 NOT IN (SELECT c FROM t7)
    ]], {
        -- <in-13.13>
        0, '', 0, 1, 0, 1
        -- </in-13.13>
    })

-- MUST_WORK_TEST
-- do_test in-13.14 {
--   execsql {
--     BEGIN TRANSACTION;
--     CREATE TABLE a(id INTEGER);
--     INSERT INTO a VALUES(1);
--     INSERT INTO a VALUES(2);
--     INSERT INTO a VALUES(3);
--     CREATE TABLE b(id INTEGER);
--     INSERT INTO b VALUES(NULL);
--     INSERT INTO b VALUES(3);
--     INSERT INTO b VALUES(4);
--     INSERT INTO b VALUES(5);
--     COMMIT;
--     SELECT * FROM a WHERE id NOT IN (SELECT id FROM b);
--   }
-- } {}
-- do_test in-13.14 {
--   execsql {
--     CREATE INDEX i5 ON b(id);
--     SELECT * FROM a WHERE id NOT IN (SELECT id FROM b);
--   }
-- } {}
test:do_catchsql_test(
    "in-13.15",
    [[
        SELECT 0 WHERE (SELECT 0,0) OR (0 IN (1,2));
    ]], {
        -- <in-13.15>
        1, "sub-select returns 2 columns - expected 1"
        -- </in-13.15>
    })

-- Tarantool: no sense here
-- test:do_test(
--     "in-13.X",
--     function()
--         return db("nullvalue", "")
--     end, {
--         -- <in-13.X>
        
--         -- </in-13.X>
--     })

test:finish_test()

