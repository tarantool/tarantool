#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(37)

testprefix = "analyze3"

--!./tcltestrunner.lua
-- 2009 August 06
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
-- This file implements regression tests for SQLite library. This file 
-- implements tests for range and LIKE constraints that use bound variables
-- instead of literal constant arguments.
--
------------------------------------------------------------------------
-- Test Organization:
--
-- analyze3-1.*: Test that the values of bound parameters are considered 
--               in the same way as constants when planning queries that
--               use range constraints.
--
-- analyze3-2.*: Test that the values of bound parameters are considered 
--               in the same way as constants when planning queries that
--               use LIKE expressions in the WHERE clause.
--
-- analyze3-3.*: Test that binding to a variable does not invalidate the 
--               query plan when there is no way in which replanning the
--               query may produce a superior outcome.
--
-- analyze3-4.*: Test that SQL or authorization callback errors occuring
--               within sqlite3Reprepare() are handled correctly.
--
-- analyze3-5.*: Check that the query plans of applicable statements are
--               invalidated if the values of SQL parameter are modified
--               using the clear_bindings() or transfer_bindings() APIs.
-- 
-- analyze3-6.*: Test that the problem fixed by commit [127a5b776d] is fixed.
--
-- analyze3-7.*: Test that some memory leaks discovered by fuzz testing 
--               have been fixed.
--

local function eqp(sql)
    return test:execsql("EXPLAIN QUERY PLAN"..sql)
end

local function sf_execsql(sql, db)
    r = test:execsql(sql)
    return {box.sql.debug().sqlite_search_count, r}
end

---------------------------------------------------------------------------
--
-- analyze3-1.1.1: 
--   Create a table with two columns. Populate the first column (affinity 
--   INTEGER) with integer values from 100 to 1100. Create an index on this 
--   column. ANALYZE the table.
--
-- analyze3-1.1.2 - 3.1.3
--   Show that there are two possible plans for querying the table with
--   a range constraint on the indexed column - "full table scan" or "use 
--   the index". When the range is specified using literal values, SQLite
--   is able to pick the best plan based on the samples in sqlite_stat3.
--
-- analyze3-1.1.4 - 3.1.9
--   Show that using SQL variables produces the same results as using
--   literal values to constrain the range scan.
--
--   These tests also check that the compiler code considers column 
--   affinities when estimating the number of rows scanned by the "use 
--   index strategy".
--
test:do_test(
    "analyze3-1.1.1",
    function()
        test:execsql([[
            CREATE TABLE t1(id INT PRIMARY KEY, x INTEGER, y);
            CREATE INDEX i1 ON t1(x);
            START TRANSACTION;
        ]])
        for i=1,1000 do
            test:execsql(string.format(" INSERT INTO t1 VALUES(%s, %s+99, %s-1) ", i, i, i))
        end
        test:execsql([[
            COMMIT;
            ANALYZE;
        ]])
        return test:execsql([[ SELECT count(*)>0 FROM "_sql_stat4"; ]])


    end, {
        -- <analyze3-1.1.1>
        1
        -- </analyze3-1.1.1>
    })

test:do_execsql_test(
    "analyze3-1.1.x",
    [[
        SELECT count(*) FROM t1 WHERE x>200 AND x<300;
    ]], {
        -- <analyze3-1.1.x>
        99
        -- </analyze3-1.1.x>
    })

test:do_execsql_test(
    "analyze3-1.1.x",
    [[
        SELECT count(*) FROM t1 WHERE x>0 AND x<1100;
    ]], {
        -- <analyze3-1.1.x>
        1000
        -- </analyze3-1.1.x>
    })

-- The first of the following two SELECT statements visits 99 rows. So
-- it is better to use the index. But the second visits every row in 
-- the table (1000 in total) so it is better to do a full-table scan.
--
test:do_eqp_test(
    "analyze3-1.1.2",
    [[
        SELECT sum(y) FROM t1 WHERE x>200 AND x<300
    ]], {
        -- <analyze3-1.1.2>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (X>? AND X<?)"}
        -- </analyze3-1.1.2>
    })

test:do_eqp_test(
    "analyze3-1.1.3",
    [[
        SELECT sum(y) FROM t1 WHERE x>0 AND x<1100 
    ]], {
        -- Tarantool: index is always covering, thus there is no need to scan table.
        -- <analyze3-1.1.3>
        -- 0, 0, 0, "SCAN TABLE t1"
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (X>? AND X<?)"}
        -- </analyze3-1.1.3>
    })

test:do_sf_execsql_test(
    "analyze3-1.1.4",
    [[
        SELECT sum(y) FROM t1 WHERE x>200 AND x<300 
    ]], {
        -- <analyze3-1.1.4>
        -- 199, 0, 14850
        -- Tarantool: index is always covering, hence number of searc counts is 100
        -- we're unable to get steps var as there're no prepared stmts
        100, {14850}
        -- </analyze3-1.1.4>
    })

test:do_test(
    "analyze3-1.1.5",
    function()
        return test:sf_execsql([[ SELECT sum(y) FROM t1 WHERE x>'200' AND x<'300' ]])
    end, {
        -- <analyze3-1.1.5>
        100, {14850}
        -- </analyze3-1.1.5>
    })

test:do_test(
    "analyze3-1.1.6",
    function()
        return test:sf_execsql(" SELECT sum(y) FROM t1 WHERE x>200.0 AND x<300.0 ")
    end, {
        -- <analyze3-1.1.6>
        100, {14850}
        -- </analyze3-1.1.6>
    })

test:do_sf_execsql_test(
    "analyze3-1.1.7",
    [[
        SELECT sum(y) FROM t1 WHERE x>0 AND x<1100 
    ]], {
        -- <analyze3-1.1.7>
        1000, {499500}
        -- </analyze3-1.1.7>
    })

test:do_test(
    "analyze3-1.1.8",
    function()
        return test:sf_execsql([[ SELECT sum(y) FROM t1 WHERE x>'0' AND x<'1100' ]])
    end, {
        -- <analyze3-1.1.8>
        1000, {499500}
        -- </analyze3-1.1.8>
    })

test:do_test(
    "analyze3-1.1.9",
    function()
        return test:sf_execsql(" SELECT sum(y) FROM t1 WHERE x>0.0 AND x<1100.0 ")
    end, {
        -- <analyze3-1.1.9>
        1000, {499500}
        -- </analyze3-1.1.9>
    })

-- The following tests are similar to the block above. The difference is
-- that the indexed column has TEXT affinity in this case. In the tests
-- above the affinity is INTEGER.
--
test:do_execsql_test(
    "analyze3-1.2.1",
    [[
        CREATE TABLE t2(id INTEGER PRIMARY KEY, x TEXT, y);
        START TRANSACTION;
          INSERT INTO t2 SELECT * FROM t1;
        COMMIT;
        CREATE INDEX i2 ON t2(x);
        ANALYZE;
    ]], {
        -- <analyze3-1.2.1>
        
        -- </analyze3-1.2.1>
    })

test:do_execsql_test(
    "analyze3-2.1.x",
    [[
        SELECT count(*) FROM t2 WHERE x>1 AND x<2;
    ]], {
        -- <analyze3-2.1.x>
        200
        -- </analyze3-2.1.x>
    })

test:do_execsql_test(
    "analyze3-2.1.x",
    [[
        SELECT count(*) FROM t2 WHERE x>0 AND x<99;
    ]], {
        -- <analyze3-2.1.x>
        990
        -- </analyze3-2.1.x>
    })

test:do_eqp_test(
    "analyze3-1.2.2",
    [[
        SELECT sum(y) FROM t2 WHERE x>1 AND x<2
    ]], {
        -- <analyze3-1.2.2>
        {0, 0, 0, "SEARCH TABLE T2 USING COVERING INDEX I2 (X>? AND X<?)"}
        -- </analyze3-1.2.2>
    })

-- Tarantool: same as for 1.1.3
test:do_eqp_test(
    "analyze3-1.2.3",
    [[
        SELECT sum(y) FROM t2 WHERE x>0 AND x<99
    ]], {
        -- <analyze3-1.2.3>
        -- 0, 0, 0, "SCAN TABLE t2"
        {0, 0, 0, "SEARCH TABLE T2 USING COVERING INDEX I2 (X>? AND X<?)"}
        -- </analyze3-1.2.3>
    })

 test:do_sf_execsql_test(
    "analyze3-1.2.4",
    [[
        SELECT sum(y) FROM t2 WHERE x>12 AND x<20 
    ]], {
        -- <analyze3-1.2.4>
        81, {4760}
        -- </analyze3-1.2.4>
    })

test:do_test(
    "analyze3-1.2.5",
    function()
        return test:sf_execsql([[SELECT typeof('12'), typeof('20'), sum(y) FROM t2 WHERE x>'12' AND x<'20']])
    end, {
        -- <analyze3-1.2.5>
        81, {"text", "text", 4760}
        -- </analyze3-1.2.5>
    })

test:do_test(
    "analyze3-1.2.6",
    function()
        return test:sf_execsql("SELECT typeof(12), typeof(20), sum(y) FROM t2 WHERE x>12 AND x<20")
    end, {
        -- <analyze3-1.2.6>
        81, {"integer", "integer", 4760}
        -- </analyze3-1.2.6>
    })

test:do_sf_execsql_test(
    "analyze3-1.2.7",
    [[
        SELECT sum(y) FROM t2 WHERE x>0 AND x<99 
    ]], {
        -- <analyze3-1.2.7>
        991, {490555}
        -- </analyze3-1.2.7>
    })

test:do_test(
    "analyze3-1.2.8",
    function()
        return test:sf_execsql([[SELECT typeof('0'), typeof('99'), sum(y) FROM t2 WHERE x>'0' AND x<'99']])
    end, {
        -- <analyze3-1.2.8>
        991, {"text", "text", 490555}
        -- </analyze3-1.2.8>
    })

test:do_test(
    "analyze3-1.2.9",
    function()
        return test:sf_execsql("SELECT typeof(0), typeof(99), sum(y) FROM t2 WHERE x>0 AND x<99")
    end, {
        -- <analyze3-1.2.9>
        991, {"integer", "integer", 490555}
        -- </analyze3-1.2.9>
    })

-- Same tests a third time. This time, column x has INTEGER affinity and
-- is not the leftmost column of the table. This triggered a bug causing
-- SQLite to use sub-optimal query plans in 3.6.18 and earlier.
--
test:do_execsql_test(
    "analyze3-1.3.1",
    [[
        CREATE TABLE t3(id INTEGER PRIMARY KEY, y TEXT, x INTEGER);
        START TRANSACTION;
          INSERT INTO t3 SELECT id, y, x FROM t1;
        COMMIT;
        CREATE INDEX i3 ON t3(x);
        ANALYZE;
    ]], {
        -- <analyze3-1.3.1>
        
        -- </analyze3-1.3.1>
    })

test:do_execsql_test(
    "analyze3-1.3.x",
    [[
        SELECT count(*) FROM t3 WHERE x>200 AND x<300;
    ]], {
        -- <analyze3-1.3.x>
        99
        -- </analyze3-1.3.x>
    })

test:do_execsql_test(
    "analyze3-1.3.y",
    [[
        SELECT count(*) FROM t3 WHERE x>0 AND x<1100
    ]], {
        -- <analyze3-1.3.x>
        1000
        -- </analyze3-1.3.x>
    })

test:do_eqp_test(
    "analyze3-1.3.2",
    [[
        SELECT sum(y) FROM t3 WHERE x>200 AND x<300
    ]], {
        -- <analyze3-1.3.2>
        {0, 0, 0, "SEARCH TABLE T3 USING COVERING INDEX I3 (X>? AND X<?)"}
        -- </analyze3-1.3.2>
    })

-- Tarantool: same as 1.1.3
test:do_eqp_test(
    "analyze3-1.3.3",
    [[
        SELECT sum(y) FROM t3 WHERE x>0 AND x<1100
    ]], {
        -- <analyze3-1.3.3>
        -- 0, 0, 0, "SCAN TABLE t3"
        {0, 0, 0, "SEARCH TABLE T3 USING COVERING INDEX I3 (X>? AND X<?)"}
        -- </analyze3-1.3.3>
    })


test:do_sf_execsql_test(
    "analyze3-1.3.4",
    [[
        SELECT sum(y) FROM t3 WHERE x>200 AND x<300 
    ]], {
        -- <analyze3-1.3.4>
        100, {14850}
        -- </analyze3-1.3.4>
    })

test:do_test(
    "analyze3-1.3.5",
    function()
        return test:sf_execsql([[ SELECT sum(y) FROM t3 WHERE x>'200' AND x<'300' ]])
    end, {
        -- <analyze3-1.3.5>
        100, {14850}
        -- </analyze3-1.3.5>
    })

test:do_test(
    "analyze3-1.3.6",
    function()
        return test:sf_execsql(" SELECT sum(y) FROM t3 WHERE x>200 AND x<300 ")
    end, {
        -- <analyze3-1.3.6>
        100, {14850}
        -- </analyze3-1.3.6>
    })

test:do_sf_execsql_test(
    "analyze3-1.3.7",
    [[
        SELECT sum(y) FROM t3 WHERE x>0 AND x<1100 
    ]], {
        -- <analyze3-1.3.7>
        1000, {499500}
        -- </analyze3-1.3.7>
    })

test:do_test(
    "analyze3-1.3.8",
    function()
        return test:sf_execsql([[ SELECT sum(y) FROM t3 WHERE x>'0' AND x<'1100' ]])
    end, {
        -- <analyze3-1.3.8>
        1000, {499500}
        -- </analyze3-1.3.8>
    })

test:do_test(
    "analyze3-1.3.9",
    function()
        return test:sf_execsql(" SELECT sum(y) FROM t3 WHERE x>0 AND x<1100 ")
    end, {
        -- <analyze3-1.3.9>
        1000, {499500}
        -- </analyze3-1.3.9>
    })

---------------------------------------------------------------------------
-- Test that the values of bound SQL variables may be used for the LIKE
-- optimization.
--
-- Tarantool: commented until #2121 is resolved.
-- X(269, "X!cmd", [=[["drop_all_tables"]]=])
-- test:do_test(
--     "analyze3-2.1",
--     function()
--         test:execsql([[
--             PRAGMA case_sensitive_like=off;
--             BEGIN;
--             CREATE TABLE t1(a, b TEXT COLLATE nocase);
--             CREATE INDEX i1 ON t1(b);
--         ]])
--         for _ in X(0, "X!for", [=[["set i 0","$i < 1000","incr i"]]=]) do
--             t = ""
--             t = t .. test.lindex({a, b, c, d, e, f, g, h, i, j}, (i / 100))
--             t = t .. test.lindex({a, b, c, d, e, f, g, h, i, j}, X(284, "X!cmd", [=[["expr",["(",["i"],"\/10)%10"]]]=]))
--             t = t .. test.lindex({a, b, c, d, e, f, g, h, i, j}, X(285, "X!cmd", [=[["expr",["(",["i"],"%10)"]]]=]))
--             test:execsql(string.format(" INSERT INTO t1 VALUES(%s, %s) ", i, t))
--         end
--         return test:execsql("COMMIT")
--     end, {
--         -- <analyze3-2.1>
        
--         -- </analyze3-2.1>
--     })

-- test:do_eqp_test(
--     "analyze3-2.2",
--     [[
--         SELECT count(a) FROM t1 WHERE b LIKE 'a%'
--     ]], {
--         -- <analyze3-2.2>
--         0, 0, 0, "SEARCH TABLE t1 USING INDEX i1 (b>? AND b<?)"
--         -- </analyze3-2.2>
--     })

-- test:do_eqp_test(
--     "analyze3-2.3",
--     [[
--         SELECT count(a) FROM t1 WHERE b LIKE '%a'
--     ]], {
--         -- <analyze3-2.3>
--         0, 0, 0, "SCAN TABLE t1"
--         -- </analyze3-2.3>
--     })

-- test:do_sf_execsql_test(
--     "analyze3-2.4",
--     [[
--         SELECT count(*) FROM t1 WHERE b LIKE 'a%' 
--     ]], {
--         -- <analyze3-2.4>
--         102, 0, 100
--         -- </analyze3-2.4>
--     })

-- test:do_sf_execsql_test(
--     "analyze3-2.5",
--     [[
--         SELECT count(*) FROM t1 WHERE b LIKE '%a' 
--     ]], {
--         -- <analyze3-2.5>
--         999, 999, 100
--         -- </analyze3-2.5>
--     })

-- test:do_test(
--     "analyze3-2.6",
--     function()
--         like = "a%"
--         return sf_execsql(" SELECT count(*) FROM t1 WHERE b LIKE $like ")
--     end, {
--         -- <analyze3-2.6>
--         102, 0, 100
--         -- </analyze3-2.6>
--     })

-- test:do_test(
--     "analyze3-2.7",
--     function()
--         like = "%a"
--         return sf_execsql(" SELECT count(*) FROM t1 WHERE b LIKE $like ")
--     end, {
--         -- <analyze3-2.7>
--         999, 999, 100
--         -- </analyze3-2.7>
--     })

-- test:do_test(
--     "analyze3-2.8",
--     function()
--         like = "a"
--         return sf_execsql(" SELECT count(*) FROM t1 WHERE b LIKE $like ")
--     end, {
--         -- <analyze3-2.8>
--         102, 0, 0
--         -- </analyze3-2.8>
--     })

-- test:do_test(
--     "analyze3-2.9",
--     function()
--         like = "ab"
--         return sf_execsql(" SELECT count(*) FROM t1 WHERE b LIKE $like ")
--     end, {
--         -- <analyze3-2.9>
--         12, 0, 0
--         -- </analyze3-2.9>
--     })

-- test:do_test(
--     "analyze3-2.10",
--     function()
--         like = "abc"
--         return sf_execsql(" SELECT count(*) FROM t1 WHERE b LIKE $like ")
--     end, {
--         -- <analyze3-2.10>
--         3, 0, 1
--         -- </analyze3-2.10>
--     })

-- test:do_test(
--     "analyze3-2.11",
--     function()
--         like = "a_c"
--         return sf_execsql(" SELECT count(*) FROM t1 WHERE b LIKE $like ")
--     end, {
--         -- <analyze3-2.11>
--         102, 0, 10
--         -- </analyze3-2.11>
--     })

---------------------------------------------------------------------------
test:do_test(
    "analyze3-6.1",
    function()
        test:execsql(" DROP TABLE IF EXISTS t1 ")
        test:execsql(" CREATE TABLE t1(id INTEGER PRIMARY KEY, a, b, c) ")
        test:execsql("START TRANSACTION")
        for i=1,1000 do
            test:execsql(string.format("INSERT INTO t1 VALUES(%s, %s, 'x', %s)", i, ((i-1) / 100), ((i-1) / 10)))
        end
        test:execsql("COMMIT")
        test:execsql([[
            DROP INDEX IF EXISTS i1 ON t1;
            DROP INDEX IF EXISTS i2 ON t1;
            CREATE INDEX i2 ON t1(c);
            CREATE INDEX i1 ON t1(a, b);
        ]])
        return test:execsql("ANALYZE")
    end, {
        -- <analyze3-6.1>
        
        -- </analyze3-6.1>
    })

test:do_eqp_test(
    "analyze3-6-3",
    [[
        SELECT * FROM t1 WHERE a = 5 AND c = 13;
    ]], {
        -- <analyze3-6-3>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I2 (C=?)"}
        -- </analyze3-6-3>
    })

test:do_eqp_test(
    "analyze3-6-2",
    [[
        SELECT * FROM t1 WHERE a = 5 AND b > 'w' AND c = 13;
    ]], {
        -- <analyze3-6-2>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I2 (C=?)"}
        -- </analyze3-6-2>
    })

-------------------------------------------------------------------------------
-- 2015-04-20.
-- Memory leak in sqlite3Stat4ProbeFree().  (Discovered while fuzzing.)
--
test:do_execsql_test(
    "analyze-7.1",
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b, c);
        INSERT INTO t1 VALUES(1,1,'0000');
        CREATE INDEX t0b ON t1(b);
        ANALYZE;
        SELECT c FROM t1 WHERE b=3 AND a BETWEEN 30 AND hex(1);
    ]], {
        -- <analyze-7.1>
        
        -- </analyze-7.1>
    })

test:finish_test()
