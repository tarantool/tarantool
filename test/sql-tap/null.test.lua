#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(45)

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
-- This file implements regression tests for SQLite library.
--
-- This file implements tests for proper treatment of the special
-- value NULL.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Create a table and some data to work with.
--
test:do_execsql_test(
    "null-1.0",
    [[
        create table t1(a primary key,b,c);
        begin;
        insert into t1 values(1,0,0);
        insert into t1 values(2,0,1);
        insert into t1 values(3,1,0);
        insert into t1 values(4,1,1);
        insert into t1 values(5,null,0);
        insert into t1 values(6,null,1);
        insert into t1 values(7,null,null);
        commit;
        select * from t1;
    ]], {
        -- <null-1.0>
        1, 0, 0, 2, 0, 1, 3, 1, 0, 4, 1, 1, 5, "", 0, 6, "", 1, 7, "", ""
        -- </null-1.0>
    })

-- Check for how arithmetic expressions handle NULL
--
test:do_execsql_test(
    "null-1.1",
    [[
        select ifnull(a+b,99) from t1;
    ]], {
        -- <null-1.1>
        1, 2, 4, 5, 99, 99, 99
        -- </null-1.1>
    })

test:do_execsql_test(
    "null-1.2",
    [[
        select ifnull(b*c,99) from t1;
    ]], {
        -- <null-1.2>
        0, 0, 0, 1, 99, 99, 99
        -- </null-1.2>
    })

-- Check to see how the CASE expression handles NULL values.  The
-- first WHEN for which the test expression is TRUE is selected.
-- FALSE and UNKNOWN test expressions are skipped.
--
test:do_execsql_test(
    "null-2.1",
    [[
        select ifnull(case when b<>0 then 1 else 0 end, 99) from t1;
    ]], {
        -- <null-2.1>
        0, 0, 1, 1, 0, 0, 0
        -- </null-2.1>
    })

test:do_execsql_test(
    "null-2.2",
    [[
        select ifnull(case when not b<>0 then 1 else 0 end, 99) from t1;
    ]], {
        -- <null-2.2>
        1, 1, 0, 0, 0, 0, 0
        -- </null-2.2>
    })

test:do_execsql_test(
    "null-2.3",
    [[
        select ifnull(case when b<>0 and c<>0 then 1 else 0 end, 99) from t1;
    ]], {
        -- <null-2.3>
        0, 0, 0, 1, 0, 0, 0
        -- </null-2.3>
    })

test:do_execsql_test(
    "null-2.4",
    [[
        select ifnull(case when not (b<>0 and c<>0) then 1 else 0 end, 99) from t1;
    ]], {
        -- <null-2.4>
        1, 1, 1, 0, 1, 0, 0
        -- </null-2.4>
    })

test:do_execsql_test(
    "null-2.5",
    [[
        select ifnull(case when b<>0 or c<>0 then 1 else 0 end, 99) from t1;
    ]], {
        -- <null-2.5>
        0, 1, 1, 1, 0, 1, 0
        -- </null-2.5>
    })

test:do_execsql_test(
    "null-2.6",
    [[
        select ifnull(case when not (b<>0 or c<>0) then 1 else 0 end, 99) from t1;
    ]], {
        -- <null-2.6>
        1, 0, 0, 0, 0, 0, 0
        -- </null-2.6>
    })

test:do_execsql_test(
    "null-2.7",
    [[
        select ifnull(case b when c then 1 else 0 end, 99) from t1;
    ]], {
        -- <null-2.7>
        1, 0, 0, 1, 0, 0, 0
        -- </null-2.7>
    })

test:do_execsql_test(
    "null-2.8",
    [[
        select ifnull(case c when b then 1 else 0 end, 99) from t1;
    ]], {
        -- <null-2.8>
        1, 0, 0, 1, 0, 0, 0
        -- </null-2.8>
    })

-- Check to see that NULL values are ignored in aggregate functions.
--
test:do_execsql_test(
    "null-3.1",
    [[
        select count(*), count(b), count(c), sum(b), sum(c), 
               avg(b), avg(c), min(b), max(b) from t1;
    ]], {
        -- <null-3.1>
        7, 4, 6, 2, 3, 0.5, 0.5, 0, 1
        -- </null-3.1>
    })

-- The sum of zero entries is a NULL, but the total of zero entries is 0.
--
test:do_execsql_test(
    "null-3.2",
    [[
        SELECT sum(b), total(b) FROM t1 WHERE b<0
    ]], {
        -- <null-3.2>
        "", 0.0
        -- </null-3.2>
    })

-- Check to see how WHERE clauses handle NULL values.  A NULL value
-- is the same as UNKNOWN.  The WHERE clause should only select those
-- rows that are TRUE.  FALSE and UNKNOWN rows are rejected.
--
test:do_execsql_test(
    "null-4.1",
    [[
        select a from t1 where b<10
    ]], {
        -- <null-4.1>
        1, 2, 3, 4
        -- </null-4.1>
    })

test:do_execsql_test(
    "null-4.2",
    [[
        select a from t1 where not b>10
    ]], {
        -- <null-4.2>
        1, 2, 3, 4
        -- </null-4.2>
    })

test:do_execsql_test(
    "null-4.3",
    [[
        select a from t1 where b<10 or c=1;
    ]], {
        -- <null-4.3>
        1, 2, 3, 4, 6
        -- </null-4.3>
    })

test:do_execsql_test(
    "null-4.4",
    [[
        select a from t1 where b<10 and c=1;
    ]], {
        -- <null-4.4>
        2, 4
        -- </null-4.4>
    })

test:do_execsql_test(
    "null-4.5",
    [[
        select a from t1 where not (b<10 and c=1);
    ]], {
        -- <null-4.5>
        1, 3, 5
        -- </null-4.5>
    })

-- The DISTINCT keyword on a SELECT statement should treat NULL values
-- as distinct
--
test:do_execsql_test(
    "null-5.1",
    [[
        select distinct b from t1 order by b;
    ]], {
        -- <null-5.1>
        "", 0, 1
        -- </null-5.1>
    })

-- A UNION to two queries should treat NULL values
-- as distinct.
--
-- (Later:)  We also take this opportunity to test the ability
-- of an ORDER BY clause to bind to either SELECT of a UNION.
-- The left-most SELECT is preferred.  In standard SQL, only
-- the left SELECT can be used.  The ability to match an ORDER
-- BY term to the right SELECT is an SQLite extension.
--
test:do_execsql_test(
    "null-6.1",
    [[
        select b from t1 union select c from t1 order by b;
    ]], {
        -- <null-6.1>
        "", 0, 1
        -- </null-6.1>
    })

test:do_execsql_test(
    "null-6.2",
    [[
        select b from t1 union select c from t1 order by 1;
    ]], {
        -- <null-6.2>
        "", 0, 1
        -- </null-6.2>
    })

test:do_execsql_test(
    "null-6.3",
    [[
        select b from t1 union select c from t1 order by t1.b;
    ]], {
        -- <null-6.3>
        "", 0, 1
        -- </null-6.3>
    })

test:do_execsql_test(
    "null-6.4",
    [[
        select b from t1 union select c from t1 order by t1.b;
    ]], {
        -- <null-6.4>
        "", 0, 1
        -- </null-6.4>
    })

test:do_catchsql_test(
    "null-6.5",
    [[
        select b from t1 union select c from t1 order by t1.a;
    ]], {
        -- <null-6.5>
        1, "1st ORDER BY term does not match any column in the result set"
        -- </null-6.5>
    })

test:do_catchsql_test(
    "null-6.6",
    [[
        select b from t1 union select c from t1 order by t1.a;
    ]], {
        -- <null-6.6>
        1, "1st ORDER BY term does not match any column in the result set"
        -- </null-6.6>
    })



-- The UNIQUE constraint only applies to non-null values
--
test:do_execsql_test(
    "null-7.1",
    [[
        create table t2(a primary key, b unique on conflict ignore);
        insert into t2 values(1,1);
        insert into t2 values(2,null);
        insert into t2 values(3,null);
        insert into t2 values(4,1);
        select a from t2 order by a;
    ]], {
        -- <null-7.1>
        1, 2, 3
        -- </null-7.1>
    })

test:do_execsql_test(
    "null-7.2",
    [[
        create table t3(a primary key, b, c, unique(b,c) on conflict ignore);
        insert into t3 values(1,1,1);
        insert into t3 values(2,null,1);
        insert into t3 values(3,null,1);
        insert into t3 values(4,1,1);
        select a from t3 order by a;
    ]], {
        -- <null-7.2>
        1, 2, 3
        -- </null-7.2>
    })

-- Ticket #461 - Make sure nulls are handled correctly when doing a
-- lookup using an index.

test:do_execsql_test(
    "null-8.1",
    [[
        CREATE TABLE t4(x primary key,y);
        INSERT INTO t4 VALUES(1,11);
        INSERT INTO t4 VALUES(2,NULL);
        SELECT x FROM t4 WHERE y=NULL;
    ]], {
        -- <null-8.1>
        
        -- </null-8.1>
    })

test:do_execsql_test(
    "null-8.2",
    [[
        SELECT x FROM t4 WHERE y IN (33,NULL);
    ]], {
        -- <null-8.2>
        
        -- </null-8.2>
    })



test:do_execsql_test(
    "null-8.3",
    [[
        SELECT x FROM t4 WHERE y<33 ORDER BY x;
    ]], {
        -- <null-8.3>
        1
        -- </null-8.3>
    })

test:do_execsql_test(
    "null-8.4",
    [[
        SELECT x FROM t4 WHERE y>6 ORDER BY x;
    ]], {
        -- <null-8.4>
        1
        -- </null-8.4>
    })

test:do_execsql_test(
    "null-8.5",
    [[
        SELECT x FROM t4 WHERE y!=33 ORDER BY x;
    ]], {
        -- <null-8.5>
        1
        -- </null-8.5>
    })

test:do_execsql_test(
    "null-8.11",
    [[
        CREATE INDEX t4i1 ON t4(y);
        SELECT x FROM t4 WHERE y=NULL;
    ]], {
        -- <null-8.11>
        
        -- </null-8.11>
    })

test:do_execsql_test(
    "null-8.12",
    [[
        SELECT x FROM t4 WHERE y IN (33,NULL);
    ]], {
        -- <null-8.12>
        
        -- </null-8.12>
    })



test:do_execsql_test(
    "null-8.13",
    [[
        SELECT x FROM t4 WHERE y<33 ORDER BY x;
    ]], {
        -- <null-8.13>
        1
        -- </null-8.13>
    })

test:do_execsql_test(
    "null-8.14",
    [[
        SELECT x FROM t4 WHERE y>6 ORDER BY x;
    ]], {
        -- <null-8.14>
        1
        -- </null-8.14>
    })

test:do_execsql_test(
    "null-8.15",
    [[
        SELECT x FROM t4 WHERE y!=33 ORDER BY x;
    ]], {
        -- <null-8.15>
        1
        -- </null-8.15>
    })

-- do_execsql_test null-9.1 {
--   CREATE TABLE t5(a, b, c);
--   CREATE UNIQUE INDEX t5ab ON t5(a, b);
--   INSERT INTO t5 VALUES(1, NULL, 'one');
--   INSERT INTO t5 VALUES(1, NULL, 'i');
--   INSERT INTO t5 VALUES(NULL, 'x', 'two');
--   INSERT INTO t5 VALUES(NULL, 'x', 'ii');
-- }
-- do_execsql_test null-9.2 {
--   SELECT * FROM t5 WHERE a = 1 AND b IS NULL;
-- } {1 {} one 1 {} i}
-- do_execsql_test null-9.3 {
--   SELECT * FROM t5 WHERE a IS NULL AND b = 'x';
-- } {{} x two {} x ii}


-- gh-2136: "IS" is only applicable when dealing with NULL

test:do_execsql_test(
    "null-10.1",
    [[
        SELECT 1 WHERE 1 IS NULL;
    ]], {
        -- <null-8.15>

        -- </null-8.15>
    })

test:do_execsql_test(
    "null-10.2",
    [[
        SELECT 1 WHERE 1 IS NOT NULL;
    ]], {
        -- <null-8.15>
        1
        -- </null-8.15>
    })

test:do_execsql_test(
    "null-10.3",
    [[
        SELECT 1 WHERE NULL IS NULL;
    ]], {
        -- <null-8.15>
        1
        -- </null-8.15>
    })

test:do_execsql_test(
    "null-10.4",
    [[
        SELECT 1 WHERE NULL IS NOT NULL;
    ]], {
        -- <null-8.15>

        -- </null-8.15>
    })

test:do_catchsql_test(
    "null-10.5",
    [[
        SELECT 1 WHERE 1 IS 1;
    ]],
    {
    -- <index-1.3>
    1, "near \"1\": syntax error"
    -- <index-1.3>
    })

test:do_catchsql_test(
    "null-10.6",
    [[
        SELECT 1 WHERE 1 IS NOT 1;
    ]],
    {
    -- <index-1.3>
    1, "near \"1\": syntax error"
    -- <index-1.3>
    })

test:do_catchsql_test(
    "null-10.7",
    [[
        SELECT 1 WHERE NULL IS 1;
    ]],
    {
    -- <index-1.3>
    1, "near \"1\": syntax error"
    -- <index-1.3>
    })

test:do_catchsql_test(
    "null-10.8",
    [[
        SELECT 1 WHERE NULL IS NOT 1;
    ]],
    {
    -- <index-1.3>
    1, "near \"1\": syntax error"
    -- <index-1.3>
    })


test:finish_test()
