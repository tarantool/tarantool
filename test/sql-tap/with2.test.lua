#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(59)

--!./tcltestrunner.lua
-- 2014 January 11
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
-- focus of this file is testing the WITH clause.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "with2"

test:do_execsql_test(
    1.0,
    [[
        CREATE TABLE t1(a  INT PRIMARY KEY);
        INSERT INTO t1 VALUES(1);
        INSERT INTO t1 VALUES(2);
    ]])

test:do_execsql_test(
    1.1,
    [[
        WITH x1 AS (SELECT * FROM t1)
        SELECT sum(a) FROM x1;
    ]], {
        -- <1.1>
        3
        -- </1.1>
    })

test:do_execsql_test(
    1.2,
    [[
        WITH x1 AS (SELECT * FROM t1)
        SELECT (SELECT sum(a) FROM x1);
    ]], {
        -- <1.2>
        3
        -- </1.2>
    })

test:do_execsql_test(
    1.3,
    [[
        WITH x1 AS (SELECT * FROM t1)
        SELECT (SELECT sum(a) FROM x1);
    ]], {
        -- <1.3>
        3
        -- </1.3>
    })

test:do_execsql_test(
    1.4,
    [[
        CREATE TABLE t2(i  INT PRIMARY KEY);
        INSERT INTO t2 VALUES(2);
        INSERT INTO t2 VALUES(3);
        INSERT INTO t2 VALUES(5);

        WITH x1   AS (SELECT i FROM t2),
             i(a) AS (
               SELECT min(i)-1 FROM x1 UNION SELECT a+1 FROM i WHERE a<10
             )
        SELECT a FROM i WHERE a NOT IN x1
    ]], {
        -- <1.4>
        1, 4, 6, 7, 8, 9, 10
        -- </1.4>
    })

test:do_execsql_test(
    1.5,
    [[
        WITH x1 AS (SELECT a FROM t1),
             x2 AS (SELECT i FROM t2),
             x3 AS (SELECT * FROM x1, x2 WHERE x1.a IN x2 AND x2.i IN x1)
        SELECT * FROM x3 
    ]], {
        -- <1.5>
        2, 2
        -- </1.5>
    })

test:do_execsql_test(
    1.6,
    [[
        --CREATE TABLE t3 AS SELECT 3 AS x;
        --CREATE TABLE t4 AS SELECT 4 AS x;
        CREATE TABLE t3(x  INT PRIMARY KEY); INSERT INTO t3 VALUES(3);
        CREATE TABLE t4(x  INT PRIMARY KEY); INSERT INTO t4 VALUES(4);

        WITH x1 AS (SELECT * FROM t3),
             x2 AS (
               WITH t3 AS (SELECT * FROM t4)
               SELECT * FROM x1
             )
        SELECT * FROM x2;
    ]], {
        -- <1.6>
        3
        -- </1.6>
    })

test:do_execsql_test(
    1.9,
    [[
        WITH x1 AS (SELECT * FROM t1)
        SELECT (SELECT sum(a) FROM x1), (SELECT max(a) FROM x1);
    ]], {
        -- <1.9>
        3, 2
        -- </1.9>
    })

test:do_execsql_test(
    1.10,
    [[
        WITH x1 AS (SELECT * FROM t1)
        SELECT (SELECT sum(a) FROM x1), (SELECT max(a) FROM x1), a FROM x1;
    ]], {
        -- <1.10>
        3, 2, 1, 3, 2, 2
        -- </1.10>
    })

test:do_execsql_test(
    1.11,
    [[
        WITH 
        i(x) AS ( 
          WITH 
          j(x) AS ( SELECT * FROM i ), 
          i(x) AS ( SELECT * FROM t1 )
          SELECT * FROM j
        )
        SELECT * FROM i;
    ]], {
        -- <1.11>
        1, 2
        -- </1.11>
    })

test:do_execsql_test(
    1.12,
    [[
        WITH r(i) AS (
          VALUES('.')
          UNION ALL
          SELECT i || '.' FROM r, (
            SELECT x FROM x INTERSECT SELECT y FROM y
          ) WHERE length(i) < 10
        ),
        x(x) AS ( VALUES(1) UNION ALL VALUES(2) UNION ALL VALUES(3) ),
        y(y) AS ( VALUES(2) UNION ALL VALUES(4) UNION ALL VALUES(6) )

        SELECT * FROM r;
    ]], {
        -- <1.12>
        ".", "..", "...", "....", ".....", "......", ".......", "........", ".........", ".........."
        -- </1.12>
    })

test:do_execsql_test(
    1.13,
    [[
        WITH r(i) AS (
          VALUES('.')
          UNION ALL
          SELECT i || '.' FROM r, ( SELECT x FROM x WHERE x=2 ) WHERE length(i) < 10
        ),
        x(x) AS ( VALUES(1) UNION ALL VALUES(2) UNION ALL VALUES(3) )

        SELECT * FROM r ORDER BY length(i) DESC;
    ]], {
        -- <1.13>
        "..........", ".........", "........", ".......", "......", ".....", "....", "...", "..", "."
        -- </1.13>
    })

test:do_execsql_test(
    1.14,
    [[
        WITH 
        t4(x) AS ( 
          VALUES(4)
          UNION ALL 
          SELECT x+1 FROM t4 WHERE x<10
        )
        SELECT * FROM t4;
    ]], {
        -- <1.14>
        4, 5, 6, 7, 8, 9, 10
        -- </1.14>
    })

test:do_catchsql_test(1.16, [[
    WITH 
    t4(x) AS ( 
      VALUES(4)
      UNION ALL 
      SELECT x+1 FROM t4, t4, t4 WHERE x<10
    )
    SELECT * FROM t4;
]], {
    -- <1.16>
    1, "multiple references to recursive table: T4"
    -- </1.16>
})

-----------------------------------------------------------------------------
-- Check that variables can be used in CTEs.
--
local min = 3
local max = 9
test:do_execsql_test(
    2.1,
    string.format([[
        WITH i(x) AS (
          VALUES(%s) UNION ALL SELECT x+1 FROM i WHERE x < %s
        )
        SELECT * FROM i;
    ]], min, max), {
        -- <2.1>
        3, 4, 5, 6, 7, 8, 9
        -- </2.1>
    })

test:do_execsql_test(
    2.2,
    string.format([[
        WITH i(x) AS (
          VALUES(%s) UNION ALL SELECT x+1 FROM i WHERE x < %s
        )
        SELECT x FROM i JOIN i AS j USING (x);
    ]], min, max), {
        -- <2.2>
        3, 4, 5, 6, 7, 8, 9
        -- </2.2>
    })

-----------------------------------------------------------------------------
-- Check that circular references are rejected.
--
test:do_catchsql_test(3.1, [[
    WITH i(x, y) AS ( VALUES(1, (SELECT x FROM i)) )
    SELECT * FROM i;
]], {
    -- <3.1>
    1, "circular reference: I"
    -- </3.1>
})

test:do_catchsql_test(3.2, [[
    WITH 
    i(x) AS ( SELECT * FROM j ),
    j(x) AS ( SELECT * FROM k ),
    k(x) AS ( SELECT * FROM i )
    SELECT * FROM i;
]], {
    -- <3.2>
    1, "circular reference: I"
    -- </3.2>
})

test:do_catchsql_test(3.3, [[
    WITH 
    i(x) AS ( SELECT * FROM (SELECT * FROM j) ),
    j(x) AS ( SELECT * FROM (SELECT * FROM i) )
    SELECT * FROM i;
]], {
    -- <3.3>
    1, "circular reference: I"
    -- </3.3>
})

test:do_catchsql_test(3.4, [[
    WITH 
    i(x) AS ( SELECT * FROM (SELECT * FROM j) ),
    j(x) AS ( SELECT * FROM (SELECT * FROM i) )
    SELECT * FROM j;
]], {
    -- <3.4>
    1, "circular reference: J"
    -- </3.4>
})

test:do_catchsql_test(3.5, [[
    WITH 
    i(x) AS ( 
      WITH j(x) AS ( SELECT * FROM i )
      SELECT * FROM j
    )
    SELECT * FROM i;
]], {
    -- <3.5>
    1, "circular reference: I"
    -- </3.5>
})

-----------------------------------------------------------------------------
-- Try empty and very long column lists.
--
test:do_catchsql_test(4.1, [[
    WITH x() AS ( SELECT 1,2,3 )
    SELECT * FROM x;
]], {
    -- <4.1>
    1, [[Syntax error at line 1 near ')']]
    -- </4.1>
})

local function genstmt(n)
    local cols = "c1"
    local vals = "1"
    for i=2,n do
        cols = cols..", c"..i
        vals = vals..", "..i
    end
    return string.format([[
    WITH x(%s) AS (SELECT %s)
    SELECT (c%s == %s) FROM x
    ]], cols, vals, n, n)
end

test:do_execsql_test(
    4.2,
genstmt(10), {
        -- <4.2>
        true
        -- </4.2>
    })

test:do_execsql_test(
    4.3,
genstmt(100), {
        -- <4.3>
        true
        -- </4.3>
    })

test:do_execsql_test(
    4.4,
genstmt(255), {
        -- <4.4>
        true
        -- </4.4>
    })

-- nLimit = sql_limit("db", "sql_LIMIT_COLUMN", -1)
-- Tarantool: max number of columns in result set
-- test:do_execsql_test(
--     4.5,
--     genstmt(nLimit-1), {
--         -- <4.5>
--         1
--         -- </4.5>
--     })

-- test:do_execsql_test(
--     4.6,
--     genstmt(nLimit), {
--         -- <4.6>
--         1
--         -- </4.6>
--     })

-- test:do_catchsql_test(4.7, genstmt(X(0, "X!expr", [=[["+",["nLimit"],1]]=])), {
--     -- <4.7>
--     1, "too many columns in result set"
--     -- </4.7>
-- })

-----------------------------------------------------------------
-- Check that adding a WITH clause to an INSERT disables the xfer
-- optimization.

local function do_xfer_test(test, test_func, test_name, func, exp, opts)
    local opts = opts or {}
    local exp_xfer_count = opts.exp_xfer_count
    local before = box.stat.sql().sql_xfer_count
    test_func(test, test_name, func, exp)
    local after = box.stat.sql().sql_xfer_count
    test:is(after - before, exp_xfer_count,
                   test_name .. '-xfer-count')
end

test.do_execsql_xfer_test = function(test, test_name, func, exp, opts)
    do_xfer_test(test, test.do_execsql_test, test_name, func, exp, opts)
end

test.do_catchsql_xfer_test = function(test, test_name, func, exp, opts)
    do_xfer_test(test, test.do_catchsql_test, test_name, func, exp, opts)
end

test:do_execsql_test(
    5.1,
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t1(a INT PRIMARY KEY, b INT);
        CREATE TABLE t2(a INT PRIMARY KEY, b INT);
        INSERT INTO t2 VALUES (1, 1), (2, 2);
    ]], {
        -- <5.1>
        -- <5.1>
    })

test:do_execsql_xfer_test(
    5.2,
    [[
        INSERT INTO t1 SELECT * FROM t2;
        DELETE FROM t1;
    ]], {
        -- <5.2>
        -- <5.2>
    },  {
        exp_xfer_count = 1
    })

test:do_execsql_xfer_test(
    5.3,
    [[
        INSERT INTO t1 SELECT a, b FROM t2;
        DELETE FROM t1;
    ]], {
        -- <5.3>
        -- <5.3>
    },  {
        exp_xfer_count = 0
    })

test:do_execsql_xfer_test(
    5.4,
    [[
        INSERT INTO t1 SELECT b, a FROM t2;
        DELETE FROM t1;
    ]], {
        -- <5.4>
        -- <5.4>
    },  {
        exp_xfer_count = 0
    })

test:do_execsql_xfer_test(
    5.5,
    [[
        WITH x AS (SELECT a, b FROM t2) INSERT INTO t1 SELECT * FROM x;
        DELETE FROM t1;
    ]], {
        -- <5.5>
        -- <5.5>
    },  {
        exp_xfer_count = 0
    })

test:do_execsql_xfer_test(
    5.6,
    [[
        WITH x AS (SELECT a, b FROM t2) INSERT INTO t1 SELECT * FROM t2;
        DELETE FROM t1;
    ]], {
        -- <5.6>
        -- <5.6>
    },  {
        exp_xfer_count = 0
    })

test:do_execsql_xfer_test(
    5.7,
    [[
        INSERT INTO t1 WITH x AS (SELECT * FROM t2) SELECT * FROM x;
        DELETE FROM t1;
    ]], {
        -- <5.7>
        -- <5.7>
    },  {
        exp_xfer_count = 0
    })

test:do_execsql_xfer_test(
    5.8,
    [[
        INSERT INTO t1 WITH x(a,b) AS (SELECT * FROM t2) SELECT * FROM x;
        DELETE FROM t1;
    ]], {
        -- <5.8>
        -- <5.8>
    },  {
        exp_xfer_count = 0
    })

-----------------------------------------------------------------------------
-- Check that syntax (and other) errors in statements with WITH clauses
-- attached to them do not cause problems (e.g. memory leaks).
--
test:do_execsql_test(
    6.1,
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t1(a  INT PRIMARY KEY, b INT );
        CREATE TABLE t2(a  INT PRIMARY KEY, b INT );
    ]])

test:do_catchsql_test(6.2, [[
    WITH x AS (SELECT * FROM t1)
    INSERT INTO t2 VALUES(1, 2,);
]], {
    -- <6.2>
    1, [[Syntax error at line 2 near ')']]
    -- </6.2>
})

test:do_catchsql_test("6.3.1", [[
    WITH x AS (SELECT * FROM t1)
    INSERT INTO t2 SELECT a, b, FROM t1;
]], {
    -- <6.3>
    1, [[At line 2 at or near position 33: keyword 'FROM' is reserved. Please use double quotes if 'FROM' is an identifier.]]
    -- </6.3>
})

test:do_catchsql_test("6.3.2", [[
    WITH x AS (SELECT * FROM t1)
    INSERT INTO t2 SELECT a, b FROM abc;
]], {
    -- <6.3>
    1, "Space 'ABC' does not exist"
    -- </6.3>
})

test:do_catchsql_test(6.4, [[
    WITH x AS (SELECT * FROM t1)
    INSERT INTO t2 SELECT a, b, FROM t1 a a a;
]], {
    -- <6.4>
    1, [[At line 2 at or near position 33: keyword 'FROM' is reserved. Please use double quotes if 'FROM' is an identifier.]]
    -- </6.4>
})

test:do_catchsql_test(6.5, [[
    WITH x AS (SELECT * FROM t1)
    DELETE FROM t2 WHERE;
]], {
    -- <6.5>
    1, [[Syntax error at line 2 near ';']]
    -- </6.5>
})

test:do_catchsql_test(6.6, [[
    WITH x AS (SELECT * FROM t1) DELETE FROM t2 WHERE
]], {
    -- <6.6>
    1, "Syntax error at line 2 near '\n'"
    -- </6.6>
})

test:do_catchsql_test(6.7, [[
    WITH x AS (SELECT * FROM t1) DELETE FROM t2 WHRE 1;
]], {
    -- <6.7>
    1, "Syntax error at line 1 near 'WHRE'"
    -- </6.7>
})

test:do_catchsql_test(6.8, [[
    WITH x AS (SELECT * FROM t1) UPDATE t2 SET a = 10, b = ;
]], {
    -- <6.8>
    1, "Syntax error at line 1 near ';'"
    -- </6.8>
})

test:do_catchsql_test(6.9, [[
    WITH x AS (SELECT * FROM t1) UPDATE t2 SET a = 10, b = 1 WHERE a===b;
]], {
    -- <6.9>
    1, "Syntax error at line 1 near '='"
    -- </6.9>
})

test:do_catchsql_test("6.10", [[
    WITH x(a,b) AS (
      SELECT 1, 1
      UNION ALL
      SELECT a*b,a+b FROM x WHERE c=2
    )
    SELECT * FROM x
]], {
    -- <6.10>
    1, "Can’t resolve field 'C'"
    -- </6.10>
})

---------------------------------------------------------------------------
-- Recursive queries in IN(...) expressions.
--
test:do_execsql_test(
    7.1,
    [[
        CREATE TABLE t5(x INTEGER PRIMARY KEY);
        CREATE TABLE t6(y INTEGER PRIMARY KEY);

        WITH s(x) AS ( VALUES(7) UNION ALL SELECT x+7 FROM s WHERE x<49 )
        INSERT INTO t5 
        SELECT * FROM s;

        INSERT INTO t6 
        WITH s(x) AS ( VALUES(2) UNION ALL SELECT x+2 FROM s WHERE x<49 )
        SELECT * FROM s;
    ]])

test:do_execsql_test(
    7.2,
    [[
        SELECT * FROM t6 WHERE y IN (SELECT x FROM t5)
    ]], {
        -- <7.2>
        14, 28, 42
        -- </7.2>
    })

test:do_execsql_test(
    7.3,
    [[
        WITH ss AS (SELECT x FROM t5)
        SELECT * FROM t6 WHERE y IN (SELECT x FROM ss)
    ]], {
        -- <7.3>
        14, 28, 42
        -- </7.3>
    })

test:do_execsql_test(
    7.4,
    [[
        WITH ss(x) AS ( VALUES(7) UNION ALL SELECT x+7 FROM ss WHERE x<49 )
        SELECT * FROM t6 WHERE y IN (SELECT x FROM ss)
    ]], {
        -- <7.4>
        14, 28, 42
        -- </7.4>
    })

test:do_execsql_test(
    7.5,
    [[
        SELECT * FROM t6 WHERE y IN (
          WITH ss(x) AS ( VALUES(7) UNION ALL SELECT x+7 FROM ss WHERE x<49 )
          SELECT x FROM ss
        )
    ]], {
        -- <7.5>
        14, 28, 42
        -- </7.5>
    })

---------------------------------------------------------------------------
-- At one point the following was causing an assertion failure and a 
-- memory leak.
--
test:do_execsql_test(
    8.1,
    [[
        CREATE TABLE t7(id  INT PRIMARY KEY, y INT );
        INSERT INTO t7 VALUES(1, NULL);
        CREATE VIEW v AS SELECT y FROM t7 ORDER BY y;
    ]])

test:do_execsql_test(
    8.2,
    [[
        WITH q(a) AS (
          SELECT 1
          UNION 
          SELECT a+1 FROM q, v WHERE a<5
        )
        SELECT * FROM q;
    ]], {
        -- <8.2>
        1, 2, 3, 4, 5
        -- </8.2>
    })

test:do_execsql_test(
    8.3,
    [[
        WITH q(a) AS (
          SELECT 1
          UNION ALL
          SELECT a+1 FROM q, v WHERE a<5
        )
        SELECT * FROM q;
    ]], {
        -- <8.3>
        1, 2, 3, 4, 5
        -- </8.3>
    })

test:finish_test()
