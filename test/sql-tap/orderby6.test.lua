#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(52)

--!./tcltestrunner.lua
-- 2014-03-21
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
-- focus of this file is testing that the block-sort optimization.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
testprefix = "orderby6"
-- Run all tests twice.  Once with a normal table and a second time
-- with a WITHOUT ROWID table
--
-- Tarantool: no rowid support. So run the case once
-- for _ in X(0, "X!foreach", [=[["tn rowidclause","1 {} 2 {WITHOUT ROWID}"]]=]) do
    -- Construct a table with 1000 rows and a split primary key
    --
    -- MUST_WORK_TEST
    --reset_db
    test:execsql "DROP TABLE IF EXISTS t1; DROP TABLE IF EXISTS t2;"
    test:do_test(
        "1.1",
        function()
            test:execsql "CREATE TABLE t1(a INT ,b INT ,c INT ,PRIMARY KEY(b,c));"
            return test:execsql [[
                WITH RECURSIVE
                 cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<1000)
                INSERT INTO t1 SELECT x, x%40, x/40 FROM cnt;
            ]]
        end, {
            
        })

    -- Run various ORDER BY queries that can benefit from block-sort.
    -- Compare the output to the same output using a full-sort enforced
    -- by adding + to each term of the ORDER BY clause.
    --
    test:do_execsql_test(
        "1.2", 
        [[
            SELECT b,a,c FROM t1 ORDER BY b,a,c;
        ]], 
        test:execsql "SELECT b,a,c FROM t1 ORDER BY +b,+a,+c"
        )

    test:do_execsql_test(
        "1.3",
        [[
            SELECT b,a,c FROM t1 ORDER BY b,c DESC,a;
        ]], 
        test:execsql "SELECT b,a,c FROM t1 ORDER BY +b,+c DESC,+a"
        )

    test:do_execsql_test(
        "1.4",
        [[
            SELECT b,a,c FROM t1 ORDER BY b DESC,c,a;
        ]], 
        test:execsql "SELECT b,a,c FROM t1 ORDER BY +b DESC,+c,+a"
        )

    test:do_execsql_test(
        "1.5",
        [[
            SELECT b,a,c FROM t1 ORDER BY b DESC,a,c;
        ]], 
        test:execsql "SELECT b,a,c FROM t1 ORDER BY +b DESC,+a,+c"
        )

    -- LIMIT and OFFSET clauses on block-sort queries.
    --
    test:do_execsql_test(
        "1.11",
        [[
            SELECT a FROM t1 ORDER BY b, a LIMIT 10 OFFSET 20;
        ]], {
            840, 880, 920, 960, 1000, 1, 41, 81, 121, 161
        })

    test:do_execsql_test(
        "1.11x",
        [[
            SELECT a FROM t1 ORDER BY +b, a LIMIT 10 OFFSET 20;
        ]], {
            840, 880, 920, 960, 1000, 1, 41, 81, 121, 161
        })
   -- Tests below are disabled due to incapability of sorting two or more
   -- key columns with different orders (DESC/ASC). As soon as Tarantool
   -- supports this feature, these tests will be uncommented.
   -- #3016
   -- test:do_execsql_test(
   --     "1.12",
   --     [[
   --         SELECT a FROM t1 ORDER BY b DESC, a LIMIT 10 OFFSET 20;
   --     ]], {
   --         839, 879, 919, 959, 999, 38, 78, 118, 158, 198
   --     })

   -- test:do_execsql_test(
   --     "1.12",
   --     [[
   --         SELECT a FROM t1 ORDER BY +b DESC, a LIMIT 10 OFFSET 20;
   --     ]], {
   --         839, 879, 919, 959, 999, 38, 78, 118, 158, 198
   --     })

   -- test:do_execsql_test(
   --     "1.13",
   --     [[
   --         SELECT a FROM t1 ORDER BY b, a DESC LIMIT 10 OFFSET 45;
   --     ]], {
   --         161, 121, 81, 41, 1, 962, 922, 882, 842, 802
   --     })

   -- test:do_execsql_test(
   --     "1.13x",
   --     [[
   --         SELECT a FROM t1 ORDER BY +b, a DESC LIMIT 10 OFFSET 45;
   --     ]], {
   --         161, 121, 81, 41, 1, 962, 922, 882, 842, 802
   --     })

   -- test:do_execsql_test(
   --     "1.14",
   --     [[
   --         SELECT a FROM t1 ORDER BY b DESC, a LIMIT 10 OFFSET 45;
   --     ]], {
   --         838, 878, 918, 958, 998, 37, 77, 117, 157, 197
   --     })

   -- test:do_execsql_test(
   --     "1.14x",
   --     [[
   --         SELECT a FROM t1 ORDER BY +b DESC, a LIMIT 10 OFFSET 45;
   --     ]], {
   --         838, 878, 918, 958, 998, 37, 77, 117, 157, 197
   --     })

    -- Many test cases where the LIMIT+OFFSET window is in various
    -- alignments with block-sort boundaries.
    --
    local data = {
        {limit=0, offset=4, orderby="+b,+a"},
        {limit=0, offset=5, orderby="+b,+a"},
        {limit=0, offset=6, orderby="+b,+a"},
        {limit=0, offset=9, orderby="+b,+a"},
        {limit=0, offset=0, orderby="+b,+a"},
        {limit=0, offset=1, orderby="+b,+a"},
        {limit=7, offset=4, orderby="+b,+a"},
        {limit=7, offset=9, orderby="+b,+a"},
        {limit=0, offset=4, orderby="+b DESC,+a"},
        {limit=0, offset=5, orderby="+b DESC,+a"},
        {limit=0, offset=6, orderby="+b DESC,+a"},
        {limit=0, offset=9, orderby="+b DESC,+a"},
        {limit=0, offset=0, orderby="+b DESC,+a"},
        {limit=0, offset=1, orderby="+b DESC,+a"},
        {limit=7, offset=4, orderby="+b DESC,+a"},
        {limit=7, offset=9, orderby="+b DESC,+a"},
        {limit=0, offset=4, orderby="+b,+a DESC"},
        {limit=0, offset=5, orderby="+b,+a DESC"},
        {limit=0, offset=6, orderby="+b,+a DESC"},
        {limit=0, offset=9, orderby="+b,+a DESC"},
        {limit=0, offset=0, orderby="+b,+a DESC"},
        {limit=0, offset=1, orderby="+b,+a DESC"},
        {limit=7, offset=4, orderby="+b,+a DESC"},
        {limit=7, offset=9, orderby="+b,+a DESC"},
        {limit=0, offset=4, orderby="+b DESC,+a DESC"},
        {limit=0, offset=5, orderby="+b DESC,+a DESC"},
        {limit=0, offset=6, orderby="+b DESC,+a DESC"},
        {limit=0, offset=9, orderby="+b DESC,+a DESC"},
        {limit=0, offset=0, orderby="+b DESC,+a DESC"},
        {limit=0, offset=1, orderby="+b DESC,+a DESC"},
        {limit=7, offset=4, orderby="+b DESC,+a DESC"},
        {limit=7, offset=9, orderby="+b DESC,+a DESC"}}
    for i, v in ipairs(data) do
        local sql1 = "SELECT a FROM t1 ORDER BY "..v.orderby.." LIMIT "..v.limit.." OFFSET "..v.offset..";"
        local sql2 = "SELECT a FROM t1 ORDER BY "..string.gsub(v.orderby, "+", "").." LIMIT "..v.limit.." OFFSET "..v.offset..";"
        test:do_execsql_test("1.21."..i, sql1, test:execsql(sql2))
    end

--     for _ in X(0, "X!foreach", [=[["tx limit offset orderby","\n     1  10 24 {+b,+a}\n     2  10 25 {+b,+a}\n     3  10 26 {+b,+a}\n     4  10 39 {+b,+a}\n     5  10 40 {+b,+a}\n     6  10 41 {+b,+a}\n     7  27 24 {+b,+a}\n     8  27 49 {+b,+a}\n     11 10 24 {+b DESC,+a}\n     12 10 25 {+b DESC,+a}\n     13 10 26 {+b DESC,+a}\n     14 10 39 {+b DESC,+a}\n     15 10 40 {+b DESC,+a}\n     16 10 41 {+b DESC,+a}\n     17 27 24 {+b DESC,+a}\n     18 27 49 {+b DESC,+a}\n     21 10 24 {+b,+a DESC}\n     22 10 25 {+b,+a DESC}\n     23 10 26 {+b,+a DESC}\n     24 10 39 {+b,+a DESC}\n     25 10 40 {+b,+a DESC}\n     26 10 41 {+b,+a DESC}\n     27 27 24 {+b,+a DESC}\n     28 27 49 {+b,+a DESC}\n     31 10 24 {+b DESC,+a DESC}\n     32 10 25 {+b DESC,+a DESC}\n     33 10 26 {+b DESC,+a DESC}\n     34 10 39 {+b DESC,+a DESC}\n     35 10 40 {+b DESC,+a DESC}\n     36 10 41 {+b DESC,+a DESC}\n     37 27 24 {+b DESC,+a DESC}\n     38 27 49 {+b DESC,+a DESC}\n  "]]=]) do
--         sql1 = string.format("SELECT a FROM t1 ORDER BY %s LIMIT %s OFFSET %s;", orderby, limit, offset)
--         sql2 = X(96, "X!cmd", [=[["string","map","+ {}",["sql1"]]]=])
--         -- puts $sql2\n$sql1\n[db eval $sql2]
--         test:do_test(
-- string.format("%s.21.%s", tn, tx),
--             function()
--                 return test:execsql sql2
--             end, {
--                 test:execsql sql1
--             })

--     end
    -------------------------------------------------------------------------
    -- A second test table, t2, has many columns open to sorting.
    test:do_test(
        "1.31",
        function()
            test:execsql "CREATE TABLE t2(a INT ,b INT ,c INT ,d INT ,e INT ,f INT ,PRIMARY KEY(b,c,d,e,f));"
            return test:execsql [[
                WITH RECURSIVE
                 cnt(x) AS (VALUES(0) UNION ALL SELECT x+1 FROM cnt WHERE x<242)
                INSERT INTO t2 SELECT x,  x%3, (x/3)%3, (x/9)%3, (x/27)%3, (x/81)%3
                                FROM cnt;
            ]]
        end, {
            
        })

    test:do_execsql_test(
        "1.32",
        [[
            SELECT a FROM t2 ORDER BY b,c,d,e,f;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b,+c,+d,+e,+f;"
        )

    test:do_execsql_test(
        "1.33",
        [[
            SELECT a FROM t2 ORDER BY b,c,d,e,+f;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b,+c,+d,+e,+f;"
        )

    test:do_execsql_test(
        "1.34",
        [[
            SELECT a FROM t2 ORDER BY b,c,d,+e,+f;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b,+c,+d,+e,+f;"
        )

    test:do_execsql_test(
        "1.35",
        [[
            SELECT a FROM t2 ORDER BY b,c,+d,+e,+f;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b,+c,+d,+e,+f;"
        )

    test:do_execsql_test(
        "1.36",
        [[
            SELECT a FROM t2 ORDER BY b,+c,+d,+e,+f;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b,+c,+d,+e,+f;"
        )

    test:do_execsql_test(
        "1.37",
        [[
            SELECT a FROM t2 ORDER BY b,c,d,e,f DESC;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b,+c,+d,+e,+f DESC;"
        )

    test:do_execsql_test(
        "1.38",
        [[
            SELECT a FROM t2 ORDER BY b,c,d,e DESC,f;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b,+c,+d,+e DESC,+f;"
        )

    test:do_execsql_test(
        "1.39",
        [[
            SELECT a FROM t2 ORDER BY b,c,d DESC,e,f;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b,+c,+d DESC,+e,+f;"
        )

    test:do_execsql_test(
        "1.40",
        [[
            SELECT a FROM t2 ORDER BY b,c DESC,d,e,f;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b,+c DESC,+d,+e,+f;"
        )

    test:do_execsql_test(
        "1.41",
        [[
            SELECT a FROM t2 ORDER BY b DESC,c,d,e,f;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b DESC,+c,+d,+e,+f;"
        )

    test:do_execsql_test(
        "1.42",
        [[
            SELECT a FROM t2 ORDER BY b DESC,c DESC,d,e,f LIMIT 31;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b DESC,+c DESC,+d,+e,+f LIMIT 31"
        )

    test:do_execsql_test(
        "1.43",
        [[
            SELECT a FROM t2 ORDER BY b,c,d,e,f DESC LIMIT 8 OFFSET 7;
        ]], 
        test:execsql "SELECT a FROM t2 ORDER BY +b,+c,+d,+e,+f DESC LIMIT 8 OFFSET 7"
        )
test:finish_test()
