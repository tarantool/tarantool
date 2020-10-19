#!/usr/bin/env tarantool
local test = require("sqltester")
local yaml = require("yaml")
local fio = require("fio")

local ffi = require("ffi")
test:plan(74)

ffi.cdef[[
       int dup(int oldfd);
       int dup2(int oldfd, int newfd);
       int close(int fd);
       int fsync(int fd);
       int fflush(int stream);
 ]]

--!./tcltestrunner.lua
-- 2005 July 28
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.  The
-- focus of this file is testing the use of indices in WHERE clauses
-- based on recent changes to the optimizer.
--
-- $Id: where2.test,v 1.15 2009/02/02 01:50:40 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Build some test data
--
test:do_test(
    "where2-1.0",
    function()
        test:execsql [[
            CREATE TABLE t1(w int PRIMARY KEY, x int, y int, z int);
            START TRANSACTION;
        ]]
        for i=1,100 do
            local w, x, y, z
            w = i
            x = math.floor(math.log(i) / math.log(2))
            y = (((i * i) + (2 * i)) + 1)
            z = (x + y)
            test:execsql(string.format("INSERT INTO t1 VALUES(%s,%s,%s,%s)", w, x, y, z))
        end
        return test:execsql [[
            COMMIT;
            CREATE INDEX i1xy ON t1(x,y);
            CREATE INDEX i1zyx ON t1(z,y,x);
        ]]
    end, {
        -- <where2-1.0>
        
        -- </where2-1.0>
    })

-- Do an SQL statement.  Append the search count to the end of the result.
--
local function count(sql)
    local sql_sort_count = box.stat.sql().sql_sort_count
    local r = test:execsql(sql)
    print("lol "..(box.stat.sql().sql_sort_count - sql_sort_count))
    table.insert(r, box.stat.sql().sql_sort_count - sql_sort_count)
    return r
end

-- This procedure executes the SQL.  Then it checks to see if the OP_Sort
-- opcode was executed.  If an OP_Sort did occur, then "sort" is appended
-- to the result.  If no OP_Sort happened, then "nosort" is appended.
--
-- This procedure is used to check to make sure sorting is or is not
-- occurring as expected.
--
local function cksort(sql)
    local sort = "nosort"
    local sql_sort_count = box.stat.sql().sql_sort_count
    local data = test:execsql(sql)
    if sql_sort_count < box.stat.sql().sql_sort_count then
            sort = 'sort'
    end
    table.insert(data, sort)
    return data
end

-- This procedure executes the SQL.  Then it appends to the result the
-- "sort" or "nosort" keyword (as in the cksort procedure above) then
-- it appends the name of the table and index used.
--

-- This procedure executes the SQL.  Then it appends 
-- the names of the table and index used
--
local function queryplan(sql)
    local sort = "nosort"
    local sql_sort_count = box.stat.sql().sql_sort_count
    local data = test:execsql(sql)
    if sql_sort_count < box.stat.sql().sql_sort_count then
        sort = 'sort'
    end
    table.insert(data, sort)
    local eqp = test:execsql("EXPLAIN QUERY PLAN "..sql)
    for i,v in ipairs(eqp) do
        if i % 4 == 0 then
            local as, tab, idx = string.match(v, "TABLE (%w+ AS) (%w+) USING.*INDEX (%w+)")
            if as == nil then
                tab, idx = string.match(v, "TABLE (%w+) USING.*INDEX (%w+)")
            end

            if tab ~= nil then
                table.insert(data, tab)
                table.insert(data, idx)
            else
                as, tab = string.match(v, "TABLE (%w+ AS) (%w+)")
                if tab == nil  then
                    tab = string.match(v, "TABLE (%w+)")
                end

                if tab ~= nil then
                    table.insert(data, tab)
                    table.insert(data, '*')
                end
            end
        end
    end
    return data
end

-- local function queryplan(sql)
--     sql_sort_count = 0
--     data = test:execsql(sql)
--     if sql_sort_count
--     then
--         x = "sort"
--     end
--     table.insert(data,x) or data
--     eqp = test:execsql("EXPLAIN QUERY PLAN "..sql.."")
--     -- puts eqp=$eqp
--     for _ in X(0, "X!foreach", [=[["a b c x",["eqp"]]]=]) do
--         if X(83, "X!cmd", [=[["regexp"," TABLE (\\w+ AS )?(\\w+) USING.* INDEX (\\w+)\\y",["x"],"all","as","tab","idx"]]=])
--  then
--             table.insert(data,tab, idx) or data
--         elseif X(83, "X!cmd", [=[["expr","[regexp { TABLE (\\w+ AS )?(\\w+)\\y} $x all as tab]"]]=])
--  then
--             table.insert(data,tab, "*") or data
--         end
--     end
--     return data
-- end

-- Prefer a UNIQUE index over another index.
--
test:do_test(
    "where2-1.1",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE w=85 AND x=6 AND y=7396
  ]])
    end, {
        -- <where2-1.1>
        85, 6, 7396, 7402, "nosort", "T1", "*"
        -- </where2-1.1>
    })

-- # Always prefer a rowid== constraint over any other index.
-- #
-- do_test where2-1.3 {
--   queryplan {
--     SELECT * FROM t1 WHERE w=85 AND x=6 AND y=7396 AND rowid=85
--   }
-- } {85 6 7396 7402 nosort t1 *}
-- When constrained by a UNIQUE index, the ORDER BY clause is always ignored.
--
test:do_test(
    "where2-2.1",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE w=85 ORDER BY random();
  ]])
    end, {
        -- <where2-2.1>
        85, 6, 7396, 7402, "sort", "T1", "*"
        -- </where2-2.1>
    })

test:do_test(
    "where2-2.2",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE x=6 AND y=7396 ORDER BY random();
  ]])
    end, {
        -- <where2-2.2>
        85, 6, 7396, 7402, "sort", "T1", "I1XY"
        -- </where2-2.2>
    })

-- Ticket [65bdeb9739605cc22966f49208452996ff29a640] 2014-02-26
-- Make sure "ORDER BY random" does not gets optimized out.
--
test:do_test(
    "where2-2.4",
    function()
        test:execsql [[
            CREATE TABLE x1(a INTEGER PRIMARY KEY, b INT DEFAULT 1);
            WITH RECURSIVE
               cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<50)
            INSERT INTO x1 SELECT x, 1 FROM cnt;
            CREATE TABLE x2(x INTEGER PRIMARY KEY);
            INSERT INTO x2 VALUES(1);
        ]]
        local sql, out1, out2, out3
        sql = "SELECT * FROM x1, x2 WHERE x=1 ORDER BY random()"
        out1 = test:execsql(sql)
        out2 = test:execsql(sql)
        out3 = test:execsql(sql)
        return ((out1 ~= out2) and (out2 ~= out3))
    end,
        -- <where2-2.4>
        true
        -- </where2-2.4>
    )

test:do_execsql_test(
    "where2-2.5",
    [[
        -- random() is not optimized out
        EXPLAIN SELECT * FROM x1, x2 WHERE x=1 ORDER BY random();
    ]], {
        -- <where2-2.5>
        "/RANDOM/"
        -- </where2-2.5>
    })

test:do_execsql_test(
    "where2-2.5b",
    [[
        -- random() is not optimized out
        EXPLAIN SELECT * FROM x1, x2 WHERE x=1 ORDER BY random();
    ]], {
        -- <where2-2.5b>
        "/SorterOpen/"
        -- </where2-2.5b>
    })

-- MUST_WORK_TEST
test:do_execsql_test(
    "where2-2.6",
    [[
        -- other constant functions are optimized out
        EXPLAIN SELECT * FROM x1, x2 WHERE x=1 ORDER BY abs(5);
    ]], {
        -- <where2-2.6>
        "~/ABS/"
        -- </where2-2.6>
    })

test:do_execsql_test(
    "where2-2.6b",
    [[
        -- other constant functions are optimized out
        EXPLAIN SELECT * FROM x1, x2 WHERE x=1 ORDER BY abs(5);
    ]], {
        -- <where2-2.6b>
        "~/SorterOpen/"
        -- </where2-2.6b>
    })

-- # Efficient handling of forward and reverse table scans.
-- #
-- do_test where2-3.1 {
--   queryplan {
--     SELECT * FROM t1 ORDER BY rowid LIMIT 2
--   }
-- } {1 0 4 4 2 1 9 10 nosort t1 *}
-- do_test where2-3.2 {
--   queryplan {
--     SELECT * FROM t1 ORDER BY rowid DESC LIMIT 2
--   }
-- } {100 6 10201 10207 99 6 10000 10006 nosort t1 *}
-- The IN operator can be used by indices at multiple layers
--
test:do_test(
    "where2-4.1",
    function()
        return queryplan([[
      SELECT * FROM t1 WHERE z IN (10207,10006) AND y IN (10000,10201)
                       AND x>0 AND x<10
      ORDER BY w
    ]])
    end, {
        -- <where2-4.1>
        99, 6, 10000, 10006, 100, 6, 10201, 10207, "sort", "T1", "I1ZYX"
        -- </where2-4.1>
    })

test:do_test(
    "where2-4.2",
    function()
        return queryplan([[
      SELECT * FROM t1 WHERE z IN (10207,10006) AND y=10000
                       AND x>0 AND x<10
      ORDER BY w
    ]])
    end, {
        -- <where2-4.2>
        99, 6, 10000, 10006, "sort", "T1", "I1ZYX"
        -- </where2-4.2>
    })

test:do_test(
    "where2-4.3",
    function()
        return queryplan([[
      SELECT * FROM t1 WHERE z=10006 AND y IN (10000,10201)
                       AND x>0 AND x<10
      ORDER BY w
    ]])
    end, {
        -- <where2-4.3>
        99, 6, 10000, 10006, "sort", "T1", "I1ZYX"
        -- </where2-4.3>
    })

test:do_test(
    "where2-4.4",
    function()
        return queryplan([[
        SELECT * FROM t1 WHERE z IN (SELECT 10207 UNION SELECT 10006)
                         AND y IN (10000,10201)
                         AND x>0 AND x<10
        ORDER BY w
      ]])
    end, {
        -- <where2-4.4>
        99, 6, 10000, 10006, 100, 6, 10201, 10207, "sort", "T1", "I1ZYX"
        -- </where2-4.4>
    })

test:do_test(
    "where2-4.5",
    function()
        return queryplan([[
        SELECT w,x,y,z FROM t1 WHERE z IN (SELECT 10207 UNION SELECT 10006)
                         AND y IN (SELECT 10000 UNION SELECT 10201)
                         AND x>0 AND x<10
        ORDER BY w
      ]])
    end, {
        -- <where2-4.5>
        99, 6, 10000, 10006, 100, 6, 10201, 10207, "sort", "T1", "I1ZYX"
        -- </where2-4.5>
    })



test:do_test(
    "where2-4.6a",
    function()
        return queryplan([[
      SELECT w,x,y,z FROM t1
       WHERE x IN (1,2,3,4,5,6,7,8)
         AND y IN (10000,10001,10002,10003,10004,10005)
       ORDER BY x
    ]])
    end, {
        -- <where2-4.6a>
        99, 6, 10000, 10006, "nosort", "T1", "I1XY"
        -- </where2-4.6a>
    })

test:do_test(
    "where2-4.6b",
    function()
        return queryplan([[
      SELECT w,x,y,z FROM t1
       WHERE x IN (1,2,3,4,5,6,7,8)
         AND y IN (10000,10001,10002,10003,10004,10005)
       ORDER BY x DESC
    ]])
    end, {
        -- <where2-4.6b>
        99, 6, 10000, 10006, "nosort", "T1", "I1XY"
        -- </where2-4.6b>
    })

test:do_test(
    "where2-4.6c",
    function()
        return queryplan([[
      SELECT w,x,y,z FROM t1
       WHERE x IN (1,2,3,4,5,6,7,8)
         AND y IN (10000,10001,10002,10003,10004,10005)
       ORDER BY x, y
    ]])
    end, {
        -- <where2-4.6c>
        99, 6, 10000, 10006, "nosort", "T1", "I1XY"
        -- </where2-4.6c>
    })

test:do_test(
    "where2-4.6d",
    function()
        return queryplan([[
      SELECT w,x,y,z FROM t1
       WHERE x IN (1,2,3,4,5,6,7,8)
         AND y IN (10000,10001,10002,10003,10004,10005)
       ORDER BY x, y DESC
    ]])
    end, {
        -- <where2-4.6d>
        99, 6, 10000, 10006, "sort", "T1", "I1XY"
        -- </where2-4.6d>
    })

-- Duplicate entires on the RHS of an IN operator do not cause duplicate
-- output rows.
--
test:do_test(
    "where2-4.6x",
    function()
        return queryplan([[
      SELECT w,x,y,z FROM t1 WHERE z IN (10207,10006,10006,10207)
      ORDER BY w
    ]])
    end, {
        -- <where2-4.6x>
        99, 6, 10000, 10006, 100, 6, 10201, 10207, "sort", "T1", "I1ZYX"
        -- </where2-4.6x>
    })

test:do_test(
    "where2-4.6y",
    function()
        return queryplan([[
      SELECT w,x,y,z FROM t1 WHERE z IN (10207,10006,10006,10207)
      ORDER BY w DESC
    ]])
    end, {
        -- <where2-4.6y>
        100, 6, 10201, 10207, 99, 6, 10000, 10006, "sort", "T1", "I1ZYX"
        -- </where2-4.6y>
    })

test:do_test(
    "where2-4.7",
    function()
        return queryplan([[
        SELECT w,x,y,z FROM t1 WHERE z IN (
           SELECT 10207 UNION ALL SELECT 10006
           UNION ALL SELECT 10006 UNION ALL SELECT 10207)
        ORDER BY w
      ]])
    end, {
        -- <where2-4.7>
        99, 6, 10000, 10006, 100, 6, 10201, 10207, "sort", "T1", "I1ZYX"
        -- </where2-4.7>
    })





-- ifcapable subquery
-- The use of an IN operator disables the index as a sorter.
--
test:do_test(
    "where2-5.1",
    function()
        return queryplan([[
    SELECT w,x,y,z FROM t1 WHERE w=99 ORDER BY w
  ]])
    end, {
        -- <where2-5.1>
        99, 6, 10000, 10006, "nosort", "T1", "*"
        -- </where2-5.1>
    })

test:do_test(
    "where2-5.2a",
    function()
        return queryplan([[
      SELECT w,x,y,z FROM t1 WHERE w IN (99) ORDER BY w
    ]])
    end, {
        -- <where2-5.2a>
        99, 6, 10000, 10006, "nosort", "T1", "*"
        -- </where2-5.2a>
    })

test:do_test(
    "where2-5.2b",
    function()
        return queryplan([[
      SELECT w,x,y,z FROM t1 WHERE w IN (99) ORDER BY w DESC
    ]])
    end, {
        -- <where2-5.2b>
        99, 6, 10000, 10006, "nosort", "T1", "*"
        -- </where2-5.2b>
    })



-- Verify that OR clauses get translated into IN operators.
--
local idx = "*"


test:do_test(
    "where2-6.1.1",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE w=99 OR w=100 ORDER BY +w ;
  ]])
    end, {
        -- <where2-6.1.1>
        99, 6, 10000, 10006, 100, 6, 10201, 10207, "sort", "T1", idx
        -- </where2-6.1.1>
    })

test:do_test(
    "where2-6.1.2",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE 99=w OR 100=w ORDER BY +w
  ]])
    end, {
        -- <where2-6.1.2>
        99, 6, 10000, 10006, 100, 6, 10201, 10207, "sort", "T1", idx
        -- </where2-6.1.2>
    })

test:do_test(
    "where2-6.2",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE w=99 OR w=100 OR 6=w ORDER BY +w
  ]])
    end, {
        -- <where2-6.2>
        6, 2, 49, 51, 99, 6, 10000, 10006, 100, 6, 10201, 10207, "sort", "T1", idx
        -- </where2-6.2>
    })

-- tarantool
-- changed to * after reordering indexes
-- actually it does not matter (because it uses full scan)
--idx = "I1ZYX"
idx = "*"

test:do_test(
    "where2-6.3",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE w=99 OR w=100 OR 6=+w ORDER BY +w
  ]])
    end, {
        -- <where2-6.3>
        6, 2, 49, 51, 99, 6, 10000, 10006, 100, 6, 10201, 10207, "sort", "T1", idx
        -- </where2-6.3>
    })

test:do_test(
    "where2-6.4",
    function()
        return queryplan([[
    SELECT *, '|' FROM t1 WHERE w=99 OR +w=100 OR 6=w ORDER BY +w
  ]])
    end, {
        -- <where2-6.4>
        6, 2, 49, 51, "|", 99, 6, 10000, 10006, "|", 100, 6, 10201, 10207, "|", "sort", "T1", idx
        -- </where2-6.4>
    })

test:do_test(
    "where2-6.5",
    function()
        return queryplan([[
    SELECT *, '|' FROM t1 WHERE w=99 OR y=10201 OR 6=w ORDER BY +w
  ]])
    end, {
        -- <where2-6.5>
        6, 2, 49, 51, "|", 99, 6, 10000, 10006, "|", 100, 6, 10201, 10207, "|", "sort", "T1", idx
        -- </where2-6.5>
    })

idx = "I1ZYX"


test:do_test(
    "where2-6.6.1",
    function()
        return queryplan([[
    SELECT b.w,b.x,b.y,b.z FROM t1 a, t1 b
     WHERE a.w=1 AND (a.y=b.z OR b.z=10)
     ORDER BY +b.w
  ]])
    end, {
        -- <where2-6.5>
        1, 0, 4, 4, 2, 1, 9, 10, "sort", "A", "*", "B", idx
        -- </where2-6.5>
    })

test:do_test(
    "where2-6.6.2",
    function()
        return queryplan([[
    SELECT b.w,b.x,b.y,b.z FROM t1 a, t1 b
     WHERE a.w=1 AND (b.z=10 OR a.y=b.z OR b.z=10)
     ORDER BY +b.w
  ]])
    end, {
        -- <where2-6.6>
        1, 0, 4, 4, 2, 1, 9, 10, "sort", "A", "*", "B", idx
        -- </where2-6.6>
    })

-- if X(356, "X!cmd", [=[["expr","[permutation] != \"no_optimization\""]]=])
-- then
    -- Ticket #2249.  Make sure the OR optimization is not attempted if
    -- comparisons between columns of different affinities are needed.
    --
    test:do_test(
        "where2-6.7",
        function()
            test:execsql [[
                CREATE TABLE t2249a(a TEXT PRIMARY KEY, x VARCHAR(100));
                CREATE TABLE t2249b(b INTEGER PRIMARY KEY);
                INSERT INTO t2249a(a) VALUES('0123');
                INSERT INTO t2249b VALUES(123);
            ]]
            return queryplan([[
    -- Because a is type TEXT and b is type INTEGER, both a and b
    -- will attempt to convert to NUMERIC before the comparison.
    -- They will thus compare equal.
    --
    SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE a=b;
  ]])
        end, {
            -- <where2-6.7>
            123, '0123', "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.7>
        })

    test:do_test(
        "where2-6.9",
        function()
            return queryplan([[
    -- The + operator doesn't affect RHS.
    --
    SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE a=+b;
  ]])
        end, {
            -- <where2-6.9>
            123, "0123", "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.9>
        })

    test:do_test(
        "where2-6.9.2",
        function()
            -- The same thing but with the expression flipped around.
            return queryplan([[
    SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE +b=a
  ]])
        end, {
            -- <where2-6.9.2>
            123, "0123","nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.9.2>
        })

    test:do_test(
        "where2-6.10",
        function()
            return queryplan([[
    SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE +a=+b;
  ]])
        end, {
            -- <where2-6.10>
            123, "0123", "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.10>
        })

    test:do_test(
        "where2-6.11",
        function()
            -- This will not attempt the OR optimization because of the a=b
            -- comparison.
            return queryplan([[
    SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE a=b OR a='hello';
  ]])
        end, {
            -- <where2-6.11>
            123, '0123', "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.11>
        })

    test:do_test(
        "where2-6.11.2",
        function()
            -- Permutations of the expression terms.
            return queryplan([[
    SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE b=a OR a='hello';
  ]])
        end, {
            -- <where2-6.11.2>
            123, '0123', "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.11.2>
        })

    test:do_test(
        "where2-6.11.3",
        function()
            -- Permutations of the expression terms.
            return queryplan([[
    SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE 'hello'=a OR b=a;
  ]])
        end, {
            -- <where2-6.11.3>
            123, '0123', "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.11.3>
        })

    test:do_test(
        "where2-6.11.4",
        function()
            -- Permutations of the expression terms.
            return queryplan([[
    SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE a='hello' OR b=a;
  ]])
        end, {
            -- <where2-6.11.4>
            123, '0123', "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.11.4>
        })

    -- These tests are not run if subquery support is not included in the
    -- build. This is because these tests test the "a = 1 OR a = 2" to
    -- "a IN (1, 2)" optimisation transformation, which is not enabled if
    -- subqueries and the IN operator is not available.
    --
    test:do_test(
        "where2-6.12",
        function()
            return queryplan([[
      SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE a=+b OR a='hello';
    ]])
        end, {
            -- <where2-6.12>
            123, "0123", "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.12>
        })

    test:do_test(
        "where2-6.12.2",
        function()
            return queryplan([[
      SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE a='hello' OR +b=a;
    ]])
        end, {
            -- <where2-6.12.2>
            123, "0123", "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.12.2>
        })

    test:do_test(
        "where2-6.12.3",
        function()
            return queryplan([[
      SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE +b=a OR a='hello';
    ]])
        end, {
            -- <where2-6.12.3>
            123, "0123", "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.12.3>
        })

    test:do_test(
        "where2-6.13",
        function()
            -- The addition of +a on the second term disabled the OR optimization.
            -- But we should still get the same empty-set result as in where2-6.9.
            return queryplan([[
      SELECT b,a FROM t2249b CROSS JOIN t2249a WHERE a=+b OR +a='hello';
    ]])
        end, {
            -- <where2-6.13>
            123, "0123", "nosort", "T2249B", "*", "T2249A", "*"
            -- </where2-6.13>
        })



    -- Variations on the order of terms in a WHERE clause in order
    -- to make sure the OR optimizer can recognize them all.
    test:do_test(
        "where2-6.20",
        function()
            return queryplan([[
    SELECT x.a, y.a FROM t2249a x CROSS JOIN t2249a y WHERE x.a=y.a
  ]])
        end, {
            -- <where2-6.20>
            '0123', '0123', "nosort", "X", "*", "Y", "*"
            -- </where2-6.20>
        })

    -- These tests are not run if subquery support is not included in the
    -- build. This is because these tests test the "a = 1 OR a = 2" to
    -- "a IN (1, 2)" optimisation transformation, which is not enabled if
    -- subqueries and the IN operator is not available.
    --
    test:do_test(
        "where2-6.21",
        function()
            return queryplan([[
      SELECT x.a,y.a FROM t2249a x CROSS JOIN t2249a y
       WHERE x.a=y.a OR y.a='hello'
    ]])
        end, {
            -- <where2-6.21>
            '0123', '0123', "nosort", "X", "*", "Y", "*"
            -- </where2-6.21>
        })

    test:do_test(
        "where2-6.22",
        function()
            return queryplan([[
      SELECT x.a,y.a FROM t2249a x CROSS JOIN t2249a y
       WHERE y.a=x.a OR y.a='hello'
    ]])
        end, {
            -- <where2-6.22>
            '0123', '0123', "nosort", "X", "*", "Y", "*"
            -- </where2-6.22>
        })

    test:do_test(
        "where2-6.23",
        function()
            return queryplan([[
      SELECT x.a,y.a FROM t2249a x CROSS JOIN t2249a y
       WHERE y.a='hello' OR x.a=y.a
    ]])
        end, {
            -- <where2-6.23>
            '0123', '0123', "nosort", "X", "*", "Y", "*"
            -- </where2-6.23>
        })



    -- Unique queries (queries that are guaranteed to return only a single
    -- row of result) do not call the sorter.  But all tables must give
    -- a unique result.  If any one table in the join does not give a unique
    -- result then sorting is necessary.
    --
    test:do_test(
        "where2-7.1",
        function()
            return cksort([[
    create table t8(a INT PRIMARY KEY, b INT, c INT);
    insert into t8 values(1,2,3);
    insert into t8 values(2,3,4);
    create table t9(x INT,y INT, PRIMARY key (x, y));
    insert into t9 values(2,4);
    insert into t9 values(2,3);
    select y from t8, t9 where a=1 order by a, y;
  ]])
        end, {
            -- <where2-7.1>
            3, 4, "sort"
            -- </where2-7.1>
        })

    test:do_test(
        "where2-7.2",
        function()
            return cksort([[
    select * from t8 where a=1 order by b, c
  ]])
        end, {
            -- <where2-7.2>
            1, 2, 3, "nosort"
            -- </where2-7.2>
        })
-- since extra columns deleted from pk (#2289), "order by" is optimized awy
-- because 1 - we do not need to sorder by b (as t8 unique by a)
-- 2 - t9 is already ordered by x
    test:do_test(
        "where2-7.3",
        function()
            return cksort([[
    select * from t8, t9 where a=1 and y=3 order by b, x
  ]])
        end, {
            -- <where2-7.3>
            --1, 2, 3, 2, 3, "sort"
            1, 2, 3, 2, 3, "nosort"
            -- </where2-7.3>
        })

    test:do_test(
        "where2-7.4",
        function()
            test:execsql('create unique index i9y on t9(y);')
            return cksort([[                   
                   select * from t8, t9 where a=1 and y=3 order by b, x]])
        end, {
            -- <where2-7.4>
            --1, 2, 3, 2, 3, "sort"
            -- This is tahter strange, but when pk was deleted from secondary indexes
            -- sql became able to optimize sort using information that i9y is unique and
            -- there is nothing to sort here.
            -- todo: It seems like this kind of optimization was appliable until removing pk and
            -- it is a bug in sql optimizer.
            1, 2, 3, 2, 3, "nosort"
            -- </where2-7.4>
        })

-- end
-- if {[permutation] != "no_optimization"}
-- Ticket #1807.  Using IN constrains on multiple columns of
-- a multi-column index.
--
test:do_execsql_test(
    "where2-8.1",
    [[
        SELECT * FROM t1 WHERE x IN (20,21) AND y IN (1,2)
    ]], {
        -- <where2-8.1>
        
        -- </where2-8.1>
    })

test:do_execsql_test(
    "where2-8.2",
    [[
        SELECT * FROM t1 WHERE x IN (1,2) AND y IN (-5,-6)
    ]], {
        -- <where2-8.2>
        
        -- </where2-8.2>
    })

test:do_test(
    "where2-8.3.0",
    function ()
        test:execsql('CREATE TABLE tx(id int PRIMARY KEY, w int, x int, y int, z int);')
        -- for _ in X(0, "X!for", [=[["set i 1","$i<=100","incr i"]]=]) do
        for i=1,100 do
            local w, x, y, z
            w = i
            x = math.floor(math.log(i) / math.log(2))
            y = (((i * i) + (2 * i)) + 1)
            z = (x + y)
            test:execsql(string.format("INSERT INTO tx VALUES(%s,%s,%s,%s,%s)", i, w, x, y, z))
        end
    end,
    {})

-- do_test where2-8.3 {
--   execsql {
--     SELECT w FROM t1
--      WHERE x IN (SELECT x FROM tx WHERE rowid<0)
--        AND +y IN (SELECT y FROM tx WHERE rowid=1)
--   }
-- } {}
-- do_test where2-8.4 {
--   execsql {
--     SELECT w FROM t1
--      WHERE x IN (SELECT x FROM tx WHERE rowid=1)
--        AND y IN (SELECT y FROM tx WHERE rowid<0)
--   }
-- } {}
--set sql_where_trace 1
test:do_execsql_test(
    "where2-8.5",
    [[
        CREATE INDEX tx_xyz ON tx(x, y, z, w);
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 12 AND 14)
    ]], {
        -- <where2-8.5>
        12, 13, 14
        -- </where2-8.5>
    })

test:do_execsql_test(
    "where2-8.6",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 12 AND 14)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.6>
        12, 13, 14
        -- </where2-8.6>
    })

test:do_execsql_test(
    "where2-8.7",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 12 AND 14)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.7>
        10, 11, 12, 13, 14, 15
        -- </where2-8.7>
    })

test:do_execsql_test(
    "where2-8.8",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.8>
        10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
        -- </where2-8.8>
    })

test:do_execsql_test(
    "where2-8.9",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 2 AND 4)
    ]], {
        -- <where2-8.9>
        
        -- </where2-8.9>
    })

test:do_execsql_test(
    "where2-8.10",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 2 AND 4)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.10>
        
        -- </where2-8.10>
    })

test:do_execsql_test(
    "where2-8.11",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 2 AND 4)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.11>
        
        -- </where2-8.11>
    })

test:do_execsql_test(
    "where2-8.12",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN -4 AND -2)
    ]], {
        -- <where2-8.12>
        
        -- </where2-8.12>
    })

test:do_execsql_test(
    "where2-8.13",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN -4 AND -2)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.13>
        
        -- </where2-8.13>
    })

test:do_execsql_test(
    "where2-8.14",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN -4 AND -2)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.14>
        
        -- </where2-8.14>
    })

test:do_execsql_test(
    "where2-8.15",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 200 AND 300)
    ]], {
        -- <where2-8.15>
        
        -- </where2-8.15>
    })

test:do_execsql_test(
    "where2-8.16",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 200 AND 300)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.16>
        
        -- </where2-8.16>
    })

test:do_execsql_test(
    "where2-8.17",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE w BETWEEN 200 AND 300)
           AND y IN (SELECT y FROM t1 WHERE w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.17>
        
        -- </where2-8.17>
    })

test:do_execsql_test(
    "where2-8.18",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE +w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE +w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE +w BETWEEN 200 AND 300)
    ]], {
        -- <where2-8.18>
        
        -- </where2-8.18>
    })

test:do_execsql_test(
    "where2-8.19",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE +w BETWEEN 10 AND 20)
           AND y IN (SELECT y FROM t1 WHERE +w BETWEEN 200 AND 300)
           AND z IN (SELECT z FROM t1 WHERE +w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.19>
        
        -- </where2-8.19>
    })

test:do_execsql_test(
    "where2-8.20",
    [[
        SELECT w FROM tx
         WHERE x IN (SELECT x FROM t1 WHERE +w BETWEEN 200 AND 300)
           AND y IN (SELECT y FROM t1 WHERE +w BETWEEN 10 AND 20)
           AND z IN (SELECT z FROM t1 WHERE +w BETWEEN 10 AND 20)
    ]], {
        -- <where2-8.20>
        
        -- </where2-8.20>
    })



-- Make sure WHERE clauses of the form A=1 AND (B=2 OR B=3) are optimized
-- when we have an index on A and B.
--
-- if X(0, "X!capable", [["or_opt&&tclvar"]]) then
    test:do_test(
        "where2-9.1",
        function()
            test:execsql [[
                CREATE TABLE t10(id int PRIMARY KEY,a INT,b INT,c INT);
                START TRANSACTION;
                INSERT INTO t10 VALUES(1, 1,1,1);
                INSERT INTO t10 VALUES(2, 1,2,2);
                INSERT INTO t10 VALUES(3, 1,3,3);
            ]]
            for i=4, 1000 do
            -- for _ in X(0, "X!for", [=[["set i 4","$i<=1000","incr i"]]=]) do
                test:execsql(string.format("INSERT INTO t10 VALUES(%s,1,%s,%s)", i, i, i))
            end
            return test:execsql [[
                COMMIT;
                CREATE INDEX i10 ON t10(a,b);
                SELECT count(*) FROM t10;
            ]]
        end, {
            -- <where2-9.1>
            1000
            -- </where2-9.1>
        })

-- Tarantool: count is not supported yet
    -- test:do_test(
    --     "where2-9.2",
    --     function()
    --         return count([[
    --     SELECT a,b,c FROM t10 WHERE a=1 AND (b=2 OR b=3)
    --   ]])
    --     end, {
    --         -- <where2-9.2>
    --         1, 2, 2, 1, 3, 3, 3
    --         -- </where2-9.2>
    --     })
-- end

-- Indices with redundant columns
--
-- Tarantool doesn't allow indexes with duplicated columns. Comment the test.
--do_test where2-11.1 {
--  execsql {
--    CREATE TABLE t11(id int primary key, a int,b int,c int,d int);
--    CREATE INDEX i11aba ON t11(a,b,a,c); -- column A occurs twice.
--    INSERT INTO t11 VALUES(1, 1,2,3,4);
--    INSERT INTO t11 VALUES(2, 5,6,7,8);
--    INSERT INTO t11 VALUES(3, 1,2,9,10);
--    INSERT INTO t11 VALUES(4, 5,11,12,13);
--    SELECT c FROM t11 WHERE a=1 AND b=2 ORDER BY c;
--  }
--} {3 9}
--do_test where2-11.2 {
--  execsql {
--    CREATE INDEX i11cccccccc ON t11(c,c,c,c,c,c,c,c); -- repeated column
--    SELECT d FROM t11 WHERE c=9;
--  }
--} {10}
--do_test where2-11.3 {
--  execsql {
--    SELECT d FROM t11 WHERE c IN (1,2,3,4,5);
--  }
--} {4}
--do_test where2-11.4 {
--  execsql {
--    SELECT d FROM t11 WHERE c=7 OR (a=1 AND b=2) ORDER BY d;
--  }
--} {4 8 10}
-- Verify that the OR clause is used in an outer loop even when
-- the OR clause scores slightly better on an inner loop.
-- if X(767, "X!cmd", [=[["expr","[permutation] != \"no_optimization\""]]=])
-- then
    test:do_test(
        "where2-12.1",
        function ()
            local data = test:execsql([[
                CREATE TABLE t12(x INTEGER PRIMARY KEY, y INT, z VARCHAR(100));
                CREATE INDEX t12y ON t12(y);
                EXPLAIN QUERY PLAN
                  SELECT a.x, b.x
                    FROM t12 AS a JOIN t12 AS b ON a.y=b.x
                    WHERE (b.x=1 OR b.y=1);
                ]])
            -- Tarantool: model this unsupported match
            -- "/.*SEARCH TABLE t12 AS b .*SEARCH TABLE t12 AS b .*/"
            local cnt = 0
            for _, v in ipairs(data) do
                if type(v) == 'string' and string.find(v, 'SEARCH TABLE T12 AS B ') then
                    cnt = cnt + 1
                end
            end
            return {cnt}
        end
        , {
            -- <where2-12.1>
            -- "/.*SEARCH TABLE t12 AS b .*SEARCH TABLE t12 AS b .*/"
            2
            -- </where2-12.1>
        })

-- end
-- Verify that all necessary OP_OpenRead opcodes occur in the OR optimization.
--
test:do_execsql_test(
    "where2-13.1",
    [[
        CREATE TABLE t13(a INT primary key,b INT);
        INSERT INTO t13 VALUES(4,5);
        SELECT * FROM t13 WHERE (1=2 AND a=3) OR a=4;
    ]], {
        -- <where2-13.1>
        4, 5
        -- </where2-13.1>
    })

test:finish_test()

