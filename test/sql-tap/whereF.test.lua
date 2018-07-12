#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(13)

--!./tcltestrunner.lua
-- 2012 November 9
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- 
-- Test cases for query planning decisions.
--
-- The tests in this file demonstrate the behaviour of the query planner
-- in determining the order in which joined tables are scanned.
--
-- Assume there are two tables being joined - t1 and t2. Each has a cost
-- if it is the outer loop, and a cost if it is the inner loop. As follows:
--
--   t1(outer) - cost of scanning t1 as the outer loop.
--   t1(inner) - cost of scanning t1 as the inner loop.
--   t2(outer) - cost of scanning t2 as the outer loop.
--   t2(inner) - cost of scanning t2 as the inner loop.
--
-- Depending on the order in which the planner nests the scans, the total
-- cost of the join query is one of:
--
--   t1(outer) * t2(inner)
--   t2(outer) * t1(inner)
--
-- The tests in this file attempt to verify that the planner nests joins in
-- the correct order when the following are true:
--
--   + (t1(outer) * t2(inner)) > (t1(inner) * t2(outer)
--   +  t1(outer) < t2(outer)
--
-- In other words, when the best overall query plan has t2 as the outer loop,
-- but when the outer loop is considered independent of the inner, t1 is the
-- most efficient choice.
--
-- In order to make them more predictable, automatic indexes are turned off for
-- the tests in this file.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
testprefix = "whereF"
test:do_execsql_test(
    1.0,
    [[
        CREATE TABLE t1(a INT PRIMARY KEY, b INT, c INT);
        CREATE TABLE t2(d INT PRIMARY KEY, e INT, f INT);
    ]], {
        -- <1.0>
        
        -- </1.0>
    })

for tn, sql in ipairs({"SELECT * FROM t1,           t2 WHERE t1.a=t2.e AND t2.d<t1.b AND t1.c!=10",
                       "SELECT * FROM t2,           t1 WHERE t1.a=t2.e AND t2.d<t1.b AND t1.c!=10",
                       "SELECT * FROM t2 CROSS JOIN t1 WHERE t1.a=t2.e AND t2.d<t1.b AND t1.c!=10"}) do
--X(0, "X!foreach", [=[["tn sql","\n  1 \"SELECT * FROM t1,           t2 WHERE t1.a=t2.e AND t2.d<t1.b AND t1.c!=10\"\n  2 \"SELECT * FROM t2,           t1 WHERE t1.a=t2.e AND t2.d<t1.b AND t1.c!=10\"\n  3 \"SELECT * FROM t2 CROSS JOIN t1 WHERE t1.a=t2.e AND t2.d<t1.b AND t1.c!=10\"\n"]]=]) do
    test:do_test(
        "1."..tn,
        function()
            return test:execsql("EXPLAIN QUERY PLAN "..sql)
        end, {
            '/SCAN TABLE T2/',
            '/SEARCH TABLE T1/'
        })
end
test:do_execsql_test(
    2.0,
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INT PRIMARY KEY, b INT, c INT);
        CREATE TABLE t2(d INT PRIMARY KEY, e INT, f INT);

        CREATE UNIQUE INDEX i2 ON t1(b);
    ]], {
        -- <2.0>
        
        -- </2.0>
    })

-- for _ in X(0, "X!foreach", [=[["tn sql","\n  1 \"SELECT * FROM t1,           t2 WHERE t1.a>? AND t2.d>t1.c AND t1.b=t2.e\"\n  2 \"SELECT * FROM t2,           t1 WHERE t1.a>? AND t2.d>t1.c AND t1.b=t2.e\"\n  3 \"SELECT * FROM t2 CROSS JOIN t1 WHERE t1.a>? AND t2.d>t1.c AND t1.b=t2.e\"\n"]]=]) do
for tn, sql in ipairs({"SELECT * FROM t1,           t2 WHERE t1.a>? AND t2.d>t1.c AND t1.b=t2.e",
                       "SELECT * FROM t2,           t1 WHERE t1.a>? AND t2.d>t1.c AND t1.b=t2.e",
                       "SELECT * FROM t2 CROSS JOIN t1 WHERE t1.a>? AND t2.d>t1.c AND t1.b=t2.e"}) do
    test:do_test(
        "2."..tn,
        function()
            return test:execsql("EXPLAIN QUERY PLAN "..sql)
        end, {
            '/SCAN TABLE T2/',
            '/SEARCH TABLE T1/'
        })
end

test:do_execsql_test(
    3.0,
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INT, b INT, c INT, PRIMARY KEY(a,b));
        CREATE TABLE t2(d INT PRIMARY KEY, e INT, f INT);
    ]], {
        -- <3.0>
        
        -- </3.0>
    })

-- Test plan changed because of changes in index representation
for tn, sql in ipairs({[[SELECT t1.a, t1.b, t2.d, t2.e FROM t1, t2
                         WHERE t2.d=t1.b AND t1.a=(t2.d+1) AND t1.b = (t2.e+1)]],

                         [[SELECT t1.a, t1.b, t2.d, t2.e FROM t2, t1
                           WHERE t2.d=t1.b AND t1.a=(t2.d+1) AND t1.b = (t2.e+1)]] }) do
    test:do_test(
        "3."..tn,
        function()
            return test:execsql("EXPLAIN QUERY PLAN "..sql)
        end, {
            '/SCAN TABLE T1/',
            '/SEARCH TABLE T2/'
        })
end

test:do_test(
    "3.3",
    function()
        local sql = [[SELECT t1.a, t1.b, t2.d, t2.e FROM t2 CROSS JOIN t1
                           WHERE t2.d=t1.b AND t1.a=(t2.d+1) AND t1.b = (t2.e+1)]]
        return test:execsql("EXPLAIN QUERY PLAN "..sql)
    end, {
    '/SCAN TABLE T2/',
    '/SEARCH TABLE T1/'
})

test:do_execsql_test(
    4.0,
    [[
        CREATE TABLE t4(a INT,b INT,c INT,d INT,e INT, PRIMARY KEY(a,b,c));
        CREATE INDEX t4adc ON t4(a,d,c);
        CREATE UNIQUE INDEX t4aebc ON t4(a,e,b,c);
        EXPLAIN QUERY PLAN SELECT a FROM t4 WHERE a=? AND b=?;
    ]], {
        -- <4.0>
        "/A=. AND B=./"
        -- </4.0>
    })

test:finish_test()

