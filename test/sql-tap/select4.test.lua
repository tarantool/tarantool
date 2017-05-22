#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(103)

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
-- focus of this file is testing UNION, INTERSECT and EXCEPT operators
-- in SELECT statements.
--
-- $Id: select4.test,v 1.30 2009/04/16 00:24:24 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Most tests in this file depend on compound-select. But there are a couple
-- right at the end that test DISTINCT, so we cannot omit the entire file.
--
-- Build some test data
--
test:execsql [[
    DROP TABLE IF EXISTS t1;
    CREATE TABLE t1(n int primary key, log int);
    BEGIN;
]]
local i, j
for i = 1, 31, 1 do
    j = 0
    while math.pow(2, j) < i do
        j = j+1
    end
    test:execsql(string.format("INSERT INTO t1 VALUES(%s,%s)", i, j))
end
test:execsql [[
    COMMIT;
]]
test:do_execsql_test(
    "select4-1.0",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log
    ]], {
        -- <select4-1.0>
        0, 1, 2, 3, 4, 5
        -- </select4-1.0>
    })

-- Union All operator
--
test:do_test(
    "select4-1.1a",
    function()
        local r = test:execsql "SELECT DISTINCT log FROM t1"
        return table.sort(r) or r
    end, {
        -- <select4-1.1a>
        0, 1, 2, 3, 4, 5
        -- </select4-1.1a>
    })

test:do_test(
    "select4-1.1b",
    function()
        local r = test:execsql "SELECT n FROM t1 WHERE log=3"
        return table.sort(r) or r
    end, {
        -- <select4-1.1b>
        5, 6, 7, 8
        -- </select4-1.1b>
    })

test:do_execsql_test(
    "select4-1.1c",
    [[
        SELECT DISTINCT log FROM t1
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-1.1c>
        0, 1, 2, 3, 4, 5, 5, 6, 7, 8
        -- </select4-1.1c>
    })

-- do_test select4-1.1d {
--   execsql {
--     DROP TABLE IF EXISTS t2;
--     CREATE TABLE t2 AS
--       SELECT DISTINCT log FROM t1
--       UNION ALL
--       SELECT n FROM t1 WHERE log=3
--       ORDER BY log;
--     SELECT * FROM t2;
--   }
-- } {0 1 2 3 4 5 5 6 7 8}
-- execsql {DROP TABLE t2}
-- do_test select4-1.1e {
--   execsql {
--     CREATE TABLE t2 AS
--       SELECT DISTINCT log FROM t1
--       UNION ALL
--       SELECT n FROM t1 WHERE log=3
--       ORDER BY log DESC;
--     SELECT * FROM t2;
--   }
-- } {8 7 6 5 5 4 3 2 1 0}
-- execsql {DROP TABLE t2}
test:do_execsql_test(
    "select4-1.1f",
    [[
        SELECT DISTINCT log FROM t1
        UNION ALL
        SELECT n FROM t1 WHERE log=2
    ]], {
        -- <select4-1.1f>
        0, 1, 2, 3, 4, 5, 3, 4
        -- </select4-1.1f>
    })

-- do_test select4-1.1g {
--   execsql {
--     CREATE TABLE t2 AS 
--       SELECT DISTINCT log FROM t1
--       UNION ALL
--       SELECT n FROM t1 WHERE log=2;
--     SELECT * FROM t2;
--   }
-- } {0 1 2 3 4 5 3 4}
-- execsql {DROP TABLE t2}
test:do_execsql_test(
    "select4-1.2",
    [[
        SELECT log FROM t1 WHERE n IN 
          (SELECT DISTINCT log FROM t1 UNION ALL
           SELECT n FROM t1 WHERE log=3)
        ORDER BY log;
    ]], {
        -- <select4-1.2>
        0, 1, 2, 2, 3, 3, 3, 3
        -- </select4-1.2>
    })



-- EVIDENCE-OF: R-02644-22131 In a compound SELECT statement, only the
-- last or right-most simple SELECT may have an ORDER BY clause.
--
test:do_catchsql_test(
    "select4-1.3",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-1.3>
        1, "ORDER BY clause should come after UNION ALL not before"
        -- </select4-1.3>
    })

test:do_catchsql_test(
    "select4-1.4",
    [[
        SELECT (VALUES(0) INTERSECT SELECT(0) UNION SELECT(0) ORDER BY 1 UNION
                SELECT 0 UNION SELECT 0 ORDER BY 1);
    ]], {
        -- <select4-1.4>
        1, "ORDER BY clause should come after UNION not before"
        -- </select4-1.4>
    })

-- Union operator
--
test:do_execsql_test(
    "select4-2.1",
    [[
        SELECT DISTINCT log FROM t1
        UNION
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-2.1>
        0, 1, 2, 3, 4, 5, 6, 7, 8
        -- </select4-2.1>
    })

test:do_execsql_test(
    "select4-2.2",
    [[
        SELECT log FROM t1 WHERE n IN 
          (SELECT DISTINCT log FROM t1 UNION
           SELECT n FROM t1 WHERE log=3)
        ORDER BY log;
    ]], {
        -- <select4-2.2>
        0, 1, 2, 2, 3, 3, 3, 3
        -- </select4-2.2>
    })



test:do_catchsql_test(
    "select4-2.3",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log
        UNION
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-2.3>
        1, "ORDER BY clause should come after UNION not before"
        -- </select4-2.3>
    })

test:do_catchsql_test(
    "select4-2.4",
    [[
        SELECT 0 ORDER BY (SELECT 0) UNION SELECT 0;
    ]], {
        -- <select4-2.4>
        1, "ORDER BY clause should come after UNION not before"
        -- </select4-2.4>
    })

test:do_execsql_test(
    "select4-2.5",
    [[
        SELECT 123 AS x ORDER BY (SELECT x ORDER BY 1);
    ]], {
        -- <select4-2.5>
        123
        -- </select4-2.5>
    })

-- Except operator
--
test:do_execsql_test(
    "select4-3.1.1",
    [[
        SELECT DISTINCT log FROM t1
        EXCEPT
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-3.1.1>
        0, 1, 2, 3, 4
        -- </select4-3.1.1>
    })

-- do_test select4-3.1.2 {
--   execsql {
--     CREATE TABLE t2 AS 
--       SELECT DISTINCT log FROM t1
--       EXCEPT
--       SELECT n FROM t1 WHERE log=3
--       ORDER BY log;
--     SELECT * FROM t2;
--   }
-- } {0 1 2 3 4}
-- execsql {DROP TABLE t2}
-- do_test select4-3.1.3 {
--   execsql {
--     CREATE TABLE t2 AS 
--       SELECT DISTINCT log FROM t1
--       EXCEPT
--       SELECT n FROM t1 WHERE log=3
--       ORDER BY log DESC;
--     SELECT * FROM t2;
--   }
-- } {4 3 2 1 0}
-- execsql {DROP TABLE t2}
test:do_execsql_test(
    "select4-3.2",
    [[
        SELECT log FROM t1 WHERE n IN 
          (SELECT DISTINCT log FROM t1 EXCEPT
           SELECT n FROM t1 WHERE log=3)
        ORDER BY log;
    ]], {
        -- <select4-3.2>
        0, 1, 2, 2
        -- </select4-3.2>
    })



test:do_catchsql_test(
    "select4-3.3",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log
        EXCEPT
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-3.3>
        1, "ORDER BY clause should come after EXCEPT not before"
        -- </select4-3.3>
    })

-- Intersect operator
--
test:do_execsql_test(
    "select4-4.1.1",
    [[
        SELECT DISTINCT log FROM t1
        INTERSECT
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-4.1.1>
        5
        -- </select4-4.1.1>
    })

test:do_execsql_test(
    "select4-4.1.2",
    [[
        SELECT DISTINCT log FROM t1
        UNION ALL
        SELECT 6
        INTERSECT
        SELECT n FROM t1 WHERE log=3
        ORDER BY t1.log;
    ]], {
        -- <select4-4.1.2>
        5, 6
        -- </select4-4.1.2>
    })

-- do_test select4-4.1.3 {
--   execsql {
--     CREATE TABLE t2 AS
--       SELECT DISTINCT log FROM t1 UNION ALL SELECT 6
--       INTERSECT
--       SELECT n FROM t1 WHERE log=3
--       ORDER BY log;
--     SELECT * FROM t2;
--   }
-- } {5 6}
-- execsql {DROP TABLE t2}
-- do_test select4-4.1.4 {
--   execsql {
--     CREATE TABLE t2 AS
--       SELECT DISTINCT log FROM t1 UNION ALL SELECT 6
--       INTERSECT
--       SELECT n FROM t1 WHERE log=3
--       ORDER BY log DESC;
--     SELECT * FROM t2;
--   }
-- } {6 5}
-- execsql {DROP TABLE t2}
test:do_execsql_test(
    "select4-4.2",
    [[
        SELECT log FROM t1 WHERE n IN 
          (SELECT DISTINCT log FROM t1 INTERSECT
           SELECT n FROM t1 WHERE log=3)
        ORDER BY log;
    ]], {
        -- <select4-4.2>
        3
        -- </select4-4.2>
    })



test:do_catchsql_test(
    "select4-4.3",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log
        INTERSECT
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-4.3>
        1, "ORDER BY clause should come after INTERSECT not before"
        -- </select4-4.3>
    })

test:do_catchsql_test(
    "select4-4.4",
    [[
        SELECT 3 IN (
          SELECT 0 ORDER BY 1
          INTERSECT
          SELECT 1
          INTERSECT 
          SELECT 2
          ORDER BY 1
        );
    ]], {
        -- <select4-4.4>
        1, "ORDER BY clause should come after INTERSECT not before"
        -- </select4-4.4>
    })

-- Various error messages while processing UNION or INTERSECT
--
test:do_catchsql_test(
    "select4-5.1",
    [[
        SELECT DISTINCT log FROM t2
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-5.1>
        1, "no such table: t2"
        -- </select4-5.1>
    })

test:do_catchsql_test(
    "select4-5.2",
    [[
        SELECT DISTINCT log AS "xyzzy" FROM t1
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY xyzzy;
    ]], {
        -- <select4-5.2>
        0, {0, 1, 2, 3, 4, 5, 5, 6, 7, 8}
        -- </select4-5.2>
    })

test:do_catchsql_test(
    "select4-5.2b",
    [[
        SELECT DISTINCT log AS xyzzy FROM t1
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY "xyzzy";
    ]], {
        -- <select4-5.2b>
        0, {0, 1, 2, 3, 4, 5, 5, 6, 7, 8}
        -- </select4-5.2b>
    })

test:do_catchsql_test(
    "select4-5.2c",
    [[
        SELECT DISTINCT log FROM t1
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY "xyzzy";
    ]], {
        -- <select4-5.2c>
        1, "1st ORDER BY term does not match any column in the result set"
        -- </select4-5.2c>
    })

test:do_catchsql_test(
    "select4-5.2d",
    [[
        SELECT DISTINCT log FROM t1
        INTERSECT
        SELECT n FROM t1 WHERE log=3
        ORDER BY "xyzzy";
    ]], {
        -- <select4-5.2d>
        1, "1st ORDER BY term does not match any column in the result set"
        -- </select4-5.2d>
    })

test:do_catchsql_test(
    "select4-5.2e",
    [[
        SELECT DISTINCT log FROM t1
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY n;
    ]], {
        -- <select4-5.2e>
        0, {0, 1, 2, 3, 4, 5, 5, 6, 7, 8}
        -- </select4-5.2e>
    })

test:do_catchsql_test(
    "select4-5.2f",
    [[
        SELECT DISTINCT log FROM t1
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-5.2f>
        0, {0, 1, 2, 3, 4, 5, 5, 6, 7, 8}
        -- </select4-5.2f>
    })

test:do_catchsql_test(
    "select4-5.2g",
    [[
        SELECT DISTINCT log FROM t1
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY 1;
    ]], {
        -- <select4-5.2g>
        0, {0, 1, 2, 3, 4, 5, 5, 6, 7, 8}
        -- </select4-5.2g>
    })

test:do_catchsql_test(
    "select4-5.2h",
    [[
        SELECT DISTINCT log FROM t1
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY 2;
    ]], {
        -- <select4-5.2h>
        1, "1st ORDER BY term out of range - should be between 1 and 1"
        -- </select4-5.2h>
    })

test:do_catchsql_test(
    "select4-5.2i",
    [[
        SELECT DISTINCT 1, log FROM t1
        UNION ALL
        SELECT 2, n FROM t1 WHERE log=3
        ORDER BY 2, 1;
    ]], {
        -- <select4-5.2i>
        0, {1, 0, 1, 1, 1, 2, 1, 3, 1, 4, 1, 5, 2, 5, 2, 6, 2, 7, 2, 8}
        -- </select4-5.2i>
    })

test:do_catchsql_test(
    "select4-5.2j",
    [[
        SELECT DISTINCT 1, log FROM t1
        UNION ALL
        SELECT 2, n FROM t1 WHERE log=3
        ORDER BY 1, 2 DESC;
    ]], {
        -- <select4-5.2j>
        0, {1, 5, 1, 4, 1, 3, 1, 2, 1, 1, 1, 0, 2, 8, 2, 7, 2, 6, 2, 5}
        -- </select4-5.2j>
    })

test:do_catchsql_test(
    "select4-5.2k",
    [[
        SELECT DISTINCT 1, log FROM t1
        UNION ALL
        SELECT 2, n FROM t1 WHERE log=3
        ORDER BY n, 1;
    ]], {
        -- <select4-5.2k>
        0, {1, 0, 1, 1, 1, 2, 1, 3, 1, 4, 1, 5, 2, 5, 2, 6, 2, 7, 2, 8}
        -- </select4-5.2k>
    })

test:do_catchsql_test(
    "select4-5.3",
    [[
        SELECT DISTINCT log, n FROM t1
        UNION ALL
        SELECT n FROM t1 WHERE log=3
        ORDER BY log;
    ]], {
        -- <select4-5.3>
        1, "SELECTs to the left and right of UNION ALL do not have the same number of result columns"
        -- </select4-5.3>
    })

test:do_catchsql_test(
    "select4-5.3-3807-1",
    [[
        SELECT 1 UNION SELECT 2, 3 UNION SELECT 4, 5 ORDER BY 1;
    ]], {
        -- <select4-5.3-3807-1>
        1, "SELECTs to the left and right of UNION do not have the same number of result columns"
        -- </select4-5.3-3807-1>
    })

test:do_catchsql_test(
    "select4-5.4",
    [[
        SELECT log FROM t1 WHERE n=2
        UNION ALL
        SELECT log FROM t1 WHERE n=3
        UNION ALL
        SELECT log FROM t1 WHERE n=4
        UNION ALL
        SELECT log FROM t1 WHERE n=5
        ORDER BY log;
    ]], {
        -- <select4-5.4>
        0, {1, 2, 2, 3}
        -- </select4-5.4>
    })

test:do_execsql_test(
    "select4-6.1",
    [[
        SELECT log, count(*) as cnt FROM t1 GROUP BY log
        UNION
        SELECT log, n FROM t1 WHERE n=7
        ORDER BY cnt, log;
    ]], {
        -- <select4-6.1>
        0, 1, 1, 1, 2, 2, 3, 4, 3, 7, 4, 8, 5, 15
        -- </select4-6.1>
    })

test:do_execsql_test(
    "select4-6.2",
    [[
        SELECT log, count(*) FROM t1 GROUP BY log
        UNION
        SELECT log, n FROM t1 WHERE n=7
        ORDER BY count(*), log;
    ]], {
        -- <select4-6.2>
        0, 1, 1, 1, 2, 2, 3, 4, 3, 7, 4, 8, 5, 15
        -- </select4-6.2>
    })

-- NULLs are indistinct for the UNION operator.
-- Make sure the UNION operator recognizes this
--
test:do_execsql_test(
    "select4-6.3",
    [[
        SELECT NULL UNION SELECT NULL UNION
        SELECT 1 UNION SELECT 2 AS 'x'
        ORDER BY x;
    ]], {
        -- <select4-6.3>
        "", 1, 2
        -- </select4-6.3>
    })

test:do_execsql_test(
    "select4-6.3.1",
    [[
        SELECT NULL UNION ALL SELECT NULL UNION ALL
        SELECT 1 UNION ALL SELECT 2 AS 'x'
        ORDER BY x;
    ]], {
        -- <select4-6.3.1>
        "", "", 1, 2
        -- </select4-6.3.1>
    })

-- Make sure the DISTINCT keyword treats NULLs as indistinct.
--
test:do_execsql_test(
    "select4-6.4",
    [[
        SELECT * FROM (
           SELECT NULL, 1 UNION ALL SELECT NULL, 1
        );
    ]], {
        -- <select4-6.4>
        "", 1, "", 1
        -- </select4-6.4>
    })

test:do_execsql_test(
    "select4-6.5",
    [[
        SELECT DISTINCT * FROM (
           SELECT NULL, 1 UNION ALL SELECT NULL, 1
        );
    ]], {
        -- <select4-6.5>
        "", 1
        -- </select4-6.5>
    })

test:do_execsql_test(
    "select4-6.6",
    [[
        SELECT DISTINCT * FROM (
           SELECT 1,2  UNION ALL SELECT 1,2
        );
    ]], {
        -- <select4-6.6>
        1, 2
        -- </select4-6.6>
    })



-- Test distinctness of NULL in other ways.
--
test:do_execsql_test(
    "select4-6.7",
    [[
        SELECT NULL EXCEPT SELECT NULL
    ]], {
        -- <select4-6.7>
        
        -- </select4-6.7>
    })

test:execsql [[DROP TABLE IF EXISTS t2;
CREATE TABLE t2 (x int primary key, y int);
INSERT INTO t2 VALUES (0, 1), (1, 1), (2, 2), (3, 4), (4, 8), (5, 15);]]
-- # Make sure column names are correct when a compound select appears as
-- # an expression in the WHERE clause.
-- #
-- do_test select4-7.1 {
--   execsql {
--     CREATE TABLE t2 AS SELECT log AS 'x', count(*) AS 'y' FROM t1 GROUP BY log;
--     SELECT * FROM t2 ORDER BY x;
--   }
-- } {0 1 1 1 2 2 3 4 4 8 5 15}  
test:do_execsql2_test(
    "select4-7.2",
    [[
        SELECT * FROM t1 WHERE n IN (SELECT n FROM t1 INTERSECT SELECT x FROM t2)
        ORDER BY n
    ]], {
        -- <select4-7.2>
        "n", 1, "log", 0, "n", 2, "log", 1, "n", 3, "log", 2, "n", 4, "log", 2, "n", 5, "log", 3
        -- </select4-7.2>
    })

test:do_execsql2_test(
    "select4-7.3",
    [[
        SELECT * FROM t1 WHERE n IN (SELECT n FROM t1 EXCEPT SELECT x FROM t2)
        ORDER BY n LIMIT 2
    ]], {
        -- <select4-7.3>
        "n", 6, "log", 3, "n", 7, "log", 3
        -- </select4-7.3>
    })

test:do_execsql2_test(
    "select4-7.4",
    [[
        SELECT * FROM t1 WHERE n IN (SELECT n FROM t1 UNION SELECT x FROM t2)
        ORDER BY n LIMIT 2
    ]], {
        -- <select4-7.4>
        "n", 1, "log", 0, "n", 2, "log", 1
        -- </select4-7.4>
    })



-- ifcapable subquery


-- ifcapable compound
-- Make sure DISTINCT works appropriately on TEXT and NUMERIC columns.
test:do_test(
    "select4-8.1",
    function()
        test:execsql [[
            BEGIN;
            CREATE TABLE t3(a text primary key, b float, c text);
            INSERT INTO t3 VALUES(1, 1.1, '1.1');
            INSERT INTO t3 VALUES(2, 1.10, '1.10');
            INSERT INTO t3 VALUES(3, 1.10, '1.1');
            INSERT INTO t3 VALUES(4, 1.1, '1.10');
            INSERT INTO t3 VALUES(5, 1.2, '1.2');
            INSERT INTO t3 VALUES(6, 1.3, '1.3');
            COMMIT;
        ]]
        return test:execsql [[
            SELECT DISTINCT b FROM t3 ORDER BY c;
        ]]
    end, {
        -- <select4-8.1>
        1.1, 1.2, 1.3
        -- </select4-8.1>
    })

test:do_execsql_test(
    "select4-8.2",
    [[
        SELECT DISTINCT c FROM t3 ORDER BY c;
    ]], {
        -- <select4-8.2>
        "1.1", "1.10", "1.2", "1.3"
        -- </select4-8.2>
    })

-- Make sure the names of columns are taken from the right-most subquery
-- right in a compound query.  Ticket #1721
--
test:do_execsql2_test(
    "select4-9.1",
    [[
        SELECT x, y FROM t2 UNION SELECT a, b FROM t3 ORDER BY x LIMIT 1
    ]], {
        -- <select4-9.1>
        "x", 0, "y", 1
        -- </select4-9.1>
    })

test:do_execsql2_test(
    "select4-9.2",
    [[
        SELECT x, y FROM t2 UNION ALL SELECT a, b FROM t3 ORDER BY x LIMIT 1
    ]], {
        -- <select4-9.2>
        "x", 0, "y", 1
        -- </select4-9.2>
    })

test:do_execsql2_test(
    "select4-9.3",
    [[
        SELECT x, y FROM t2 EXCEPT SELECT a, b FROM t3 ORDER BY x LIMIT 1
    ]], {
        -- <select4-9.3>
        "x", 0, "y", 1
        -- </select4-9.3>
    })

test:do_execsql2_test(
    "select4-9.4",
    [[
        SELECT x, y FROM t2 INTERSECT SELECT 0 AS a, 1 AS b;
    ]], {
        -- <select4-9.4>
        "x", 0, "y", 1
        -- </select4-9.4>
    })

test:do_execsql2_test(
    "select4-9.5",
    [[
        SELECT 0 AS x, 1 AS y
        UNION
        SELECT 2 AS p, 3 AS q
        UNION
        SELECT 4 AS a, 5 AS b
        ORDER BY x LIMIT 1
    ]], {
        -- <select4-9.5>
        "x", 0, "y", 1
        -- </select4-9.5>
    })

test:do_execsql2_test(
    "select4-9.6",
    [[
        SELECT * FROM (
          SELECT 0 AS x, 1 AS y
          UNION
          SELECT 2 AS p, 3 AS q
          UNION
          SELECT 4 AS a, 5 AS b
        ) ORDER BY 1 LIMIT 1;
    ]], {
        -- <select4-9.6>
        "x", 0, "y", 1
        -- </select4-9.6>
    })

test:do_execsql2_test(
    "select4-9.7",
    [[
        SELECT * FROM (
          SELECT 0 AS x, 1 AS y
          UNION
          SELECT 2 AS p, 3 AS q
          UNION
          SELECT 4 AS a, 5 AS b
        ) ORDER BY x LIMIT 1;
    ]], {
        -- <select4-9.7>
        "x", 0, "y", 1
        -- </select4-9.7>
    })



-- ifcapable subquery
test:do_execsql_test(
    "select4-9.8",
    [[
        SELECT 0 AS x, 1 AS y
        UNION
        SELECT 2 AS y, -3 AS x
        ORDER BY x LIMIT 1;
    ]], {
        -- <select4-9.8>
        0, 1
        -- </select4-9.8>
    })

test:do_execsql2_test(
    "select4-9.9.1",
    [[
        SELECT 1 AS a, 2 AS b UNION ALL SELECT 3 AS b, 4 AS a
    ]], {
        -- <select4-9.9.1>
        "a", 1, "b", 2, "a", 3, "b", 4
        -- </select4-9.9.1>
    })

test:do_execsql2_test(
    "select4-9.9.2",
    [[
        SELECT * FROM (SELECT 1 AS a, 2 AS b UNION ALL SELECT 3 AS b, 4 AS a)
         WHERE b=3
    ]], {
        -- <select4-9.9.2>
        
        -- </select4-9.9.2>
    })

test:do_execsql2_test(
    "select4-9.10",
    [[
        SELECT * FROM (SELECT 1 AS a, 2 AS b UNION ALL SELECT 3 AS b, 4 AS a)
         WHERE b=2
    ]], {
        -- <select4-9.10>
        "a", 1, "b", 2
        -- </select4-9.10>
    })

test:do_execsql2_test(
    "select4-9.11",
    [[
        SELECT * FROM (SELECT 1 AS a, 2 AS b UNION ALL SELECT 3 AS e, 4 AS b)
         WHERE b=2
    ]], {
        -- <select4-9.11>
        "a", 1, "b", 2
        -- </select4-9.11>
    })

test:do_execsql2_test(
    "select4-9.12",
    [[
        SELECT * FROM (SELECT 1 AS a, 2 AS b UNION ALL SELECT 3 AS e, 4 AS b)
         WHERE b>0
    ]], {
        -- <select4-9.12>
        "a", 1, "b", 2, "a", 3, "b", 4
        -- </select4-9.12>
    })



-- ifcapable subquery
-- Try combining DISTINCT, LIMIT, and OFFSET.  Make sure they all work
-- together.
--
test:do_execsql_test(
    "select4-10.1",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log
    ]], {
        -- <select4-10.1>
        0, 1, 2, 3, 4, 5
        -- </select4-10.1>
    })

test:do_execsql_test(
    "select4-10.2",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log LIMIT 4
    ]], {
        -- <select4-10.2>
        0, 1, 2, 3
        -- </select4-10.2>
    })

test:do_execsql_test(
    "select4-10.3",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log LIMIT 0
    ]], {
        -- <select4-10.3>
        
        -- </select4-10.3>
    })

test:do_execsql_test(
    "select4-10.4",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log LIMIT -1
    ]], {
        -- <select4-10.4>
        0, 1, 2, 3, 4, 5
        -- </select4-10.4>
    })

test:do_execsql_test(
    "select4-10.5",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log LIMIT -1 OFFSET 2
    ]], {
        -- <select4-10.5>
        2, 3, 4, 5
        -- </select4-10.5>
    })

test:do_execsql_test(
    "select4-10.6",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log LIMIT 3 OFFSET 2
    ]], {
        -- <select4-10.6>
        2, 3, 4
        -- </select4-10.6>
    })

test:do_execsql_test(
    "select4-10.7",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY +log LIMIT 3 OFFSET 20
    ]], {
        -- <select4-10.7>
        
        -- </select4-10.7>
    })

test:do_execsql_test(
    "select4-10.8",
    [[
        SELECT DISTINCT log FROM t1 ORDER BY log LIMIT 0 OFFSET 3
    ]], {
        -- <select4-10.8>
        
        -- </select4-10.8>
    })

test:do_execsql_test(
    "select4-10.9",
    [[
        SELECT DISTINCT max(n), log FROM t1 ORDER BY +log; -- LIMIT 2 OFFSET 1
    ]], {
        -- <select4-10.9>
        31, 5
        -- </select4-10.9>
    })

test:execsql [[DROP TABLE IF EXISTS t2;
CREATE TABLE t2 (rowid int primary key, x, y);]]
-- Make sure compound SELECTs with wildly different numbers of columns
-- do not cause assertion faults due to register allocation issues.
--
test:do_catchsql_test(
    "select4-11.1",
    [[
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
        UNION
        SELECT x FROM t2
    ]], {
        -- <select4-11.1>
        1, "SELECTs to the left and right of UNION do not have the same number of result columns"
        -- </select4-11.1>
    })

test:do_catchsql_test(
    "select4-11.2",
    [[
        SELECT x FROM t2
        UNION
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
    ]], {
        -- <select4-11.2>
        1, "SELECTs to the left and right of UNION do not have the same number of result columns"
        -- </select4-11.2>
    })

test:do_catchsql_test(
    "select4-11.3",
    [[
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
        UNION ALL
        SELECT x FROM t2
    ]], {
        -- <select4-11.3>
        1, "SELECTs to the left and right of UNION ALL do not have the same number of result columns"
        -- </select4-11.3>
    })

test:do_catchsql_test(
    "select4-11.4",
    [[
        SELECT x FROM t2
        UNION ALL
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
    ]], {
        -- <select4-11.4>
        1, "SELECTs to the left and right of UNION ALL do not have the same number of result columns"
        -- </select4-11.4>
    })

test:do_catchsql_test(
    "select4-11.5",
    [[
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
        EXCEPT
        SELECT x FROM t2
    ]], {
        -- <select4-11.5>
        1, "SELECTs to the left and right of EXCEPT do not have the same number of result columns"
        -- </select4-11.5>
    })

test:do_catchsql_test(
    "select4-11.6",
    [[
        SELECT x FROM t2
        EXCEPT
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
    ]], {
        -- <select4-11.6>
        1, "SELECTs to the left and right of EXCEPT do not have the same number of result columns"
        -- </select4-11.6>
    })

test:do_catchsql_test(
    "select4-11.7",
    [[
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
        INTERSECT
        SELECT x FROM t2
    ]], {
        -- <select4-11.7>
        1, "SELECTs to the left and right of INTERSECT do not have the same number of result columns"
        -- </select4-11.7>
    })

test:do_catchsql_test(
    "select4-11.8",
    [[
        SELECT x FROM t2
        INTERSECT
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
    ]], {
        -- <select4-11.8>
        1, "SELECTs to the left and right of INTERSECT do not have the same number of result columns"
        -- </select4-11.8>
    })

test:do_catchsql_test(
    "select4-11.11",
    [[
        SELECT x FROM t2
        UNION
        SELECT x FROM t2
        UNION ALL
        SELECT x FROM t2
        EXCEPT
        SELECT x FROM t2
        INTERSECT
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
    ]], {
        -- <select4-11.11>
        1, "SELECTs to the left and right of INTERSECT do not have the same number of result columns"
        -- </select4-11.11>
    })

test:do_catchsql_test(
    "select4-11.12",
    [[
        SELECT x FROM t2
        UNION
        SELECT x FROM t2
        UNION ALL
        SELECT x FROM t2
        EXCEPT
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
        EXCEPT
        SELECT x FROM t2
    ]], {
        -- <select4-11.12>
        1, "SELECTs to the left and right of EXCEPT do not have the same number of result columns"
        -- </select4-11.12>
    })

test:do_catchsql_test(
    "select4-11.13",
    [[
        SELECT x FROM t2
        UNION
        SELECT x FROM t2
        UNION ALL
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
        UNION ALL
        SELECT x FROM t2
        EXCEPT
        SELECT x FROM t2
    ]], {
        -- <select4-11.13>
        1, "SELECTs to the left and right of UNION ALL do not have the same number of result columns"
        -- </select4-11.13>
    })

test:do_catchsql_test(
    "select4-11.14",
    [[
        SELECT x FROM t2
        UNION
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
        UNION
        SELECT x FROM t2
        UNION ALL
        SELECT x FROM t2
        EXCEPT
        SELECT x FROM t2
    ]], {
        -- <select4-11.14>
        1, "SELECTs to the left and right of UNION do not have the same number of result columns"
        -- </select4-11.14>
    })

test:do_catchsql_test(
    "select4-11.15",
    [[
        SELECT x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x FROM t2
        UNION
        SELECT x FROM t2
        INTERSECT
        SELECT x FROM t2
        UNION ALL
        SELECT x FROM t2
        EXCEPT
        SELECT x FROM t2
    ]], {
        -- <select4-11.15>
        1, "SELECTs to the left and right of UNION do not have the same number of result columns"
        -- </select4-11.15>
    })

test:do_catchsql_test(
    "select4-11.16",
    [[
        INSERT INTO t2(rowid) VALUES(2) UNION SELECT 3,4 UNION SELECT 5,6 ORDER BY 1;
    ]], {
        -- <select4-11.16>
        1, "SELECTs to the left and right of UNION do not have the same number of result columns"
        -- </select4-11.16>
    })

-- MUST_WORK_TEST
-- do_test select4-12.1 {
--   catchsql {
--     SELECT 1 UNION SELECT 2,3 UNION SELECT 4,5 ORDER BY 1;
--   } db2
-- } {1 {SELECTs to the left and right of UNION do not have the same number of result columns}}


-- ifcapable compound
-- Ticket [3557ad65a076c] - Incorrect DISTINCT processing with an
-- indexed query using IN.
--
test:do_test(
    "select4-13.1",
    function()
        return test:execsql [[
            CREATE TABLE t13(id int primary key,a,b);
            INSERT INTO t13 VALUES(0, 1,1);
            INSERT INTO t13 VALUES(1, 2,1);
            INSERT INTO t13 VALUES(2, 3,1);
            INSERT INTO t13 VALUES(3, 2,2);
            INSERT INTO t13 VALUES(4, 3,2);
            INSERT INTO t13 VALUES(5, 4,2);
            CREATE INDEX t13ab ON t13(a,b);
            SELECT DISTINCT b from t13 WHERE a IN (1,2,3);
        ]]
    end, {
        -- <select4-13.1>
        1, 2
        -- </select4-13.1>
    })

-- 2014-02-18: Make sure compound SELECTs work with VALUES clauses
--
test:do_execsql_test(
    "select4-14.1",
    [[
        CREATE TABLE t14(a primary key,b,c);
        INSERT INTO t14 VALUES(1,2,3),(4,5,6);
        SELECT * FROM t14 INTERSECT VALUES(3,2,1),(2,3,1),(1,2,3),(2,1,3);
    ]], {
        -- <select4-14.1>
        1, 2, 3
        -- </select4-14.1>
    })

test:execsql [[DROP TABLE IF EXISTS t14;
CREATE TABLE t14 (a int primary key, b int, c int);
INSERT INTO t14 VALUES (1, 2, 3),(4, 5, 6);]]
test:do_execsql_test(
    "select4-14.2",
    [[
        SELECT * FROM t14 INTERSECT VALUES(1,2,3);
    ]], {
        -- <select4-14.2>
        1, 2, 3
        -- </select4-14.2>
    })

test:do_execsql_test(
    "select4-14.3",
    [[
        SELECT * FROM t14
         UNION VALUES(3,2,1),(2,3,1),(1,2,3),(7,8,9),(4,5,6)
         UNION SELECT * FROM t14 ORDER BY 1, 2, 3
    ]], {
        -- <select4-14.3>
        1, 2, 3, 2, 3, 1, 3, 2, 1, 4, 5, 6, 7, 8, 9
        -- </select4-14.3>
    })

test:do_execsql_test(
    "select4-14.4",
    [[
        SELECT * FROM t14
         UNION VALUES(3,2,1)
         UNION SELECT * FROM t14 ORDER BY 1, 2, 3
    ]], {
        -- <select4-14.4>
        1, 2, 3, 3, 2, 1, 4, 5, 6
        -- </select4-14.4>
    })

test:do_execsql_test(
    "select4-14.5",
    [[
        SELECT * FROM t14 EXCEPT VALUES(3,2,1),(2,3,1),(1,2,3),(2,1,3);
    ]], {
        -- <select4-14.5>
        4, 5, 6
        -- </select4-14.5>
    })

test:do_execsql_test(
    "select4-14.6",
    [[
        SELECT * FROM t14 EXCEPT VALUES(1,2,3)
    ]], {
        -- <select4-14.6>
        4, 5, 6
        -- </select4-14.6>
    })

test:do_execsql_test(
    "select4-14.7",
    [[
        SELECT * FROM t14 EXCEPT VALUES(1,2,3) EXCEPT VALUES(4,5,6)
    ]], {
        -- <select4-14.7>
        
        -- </select4-14.7>
    })

test:do_execsql_test(
    "select4-14.8",
    [[
        SELECT * FROM t14 EXCEPT VALUES('a','b','c') EXCEPT VALUES(4,5,6)
    ]], {
        -- <select4-14.8>
        1, 2, 3
        -- </select4-14.8>
    })

test:do_execsql_test(
    "select4-14.9",
    [[
        SELECT * FROM t14 UNION ALL VALUES(3,2,1),(2,3,1),(1,2,3),(2,1,3);
    ]], {
        -- <select4-14.9>
        1, 2, 3, 4, 5, 6, 3, 2, 1, 2, 3, 1, 1, 2, 3, 2, 1, 3
        -- </select4-14.9>
    })

test:do_execsql_test(
    "select4-14.10",
    [[
        SELECT (VALUES(1),(2),(3),(4))
    ]], {
        -- <select4-14.10>
        1
        -- </select4-14.10>
    })

test:do_execsql_test(
    "select4-14.11",
    [[
        SELECT (SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4)
    ]], {
        -- <select4-14.11>
        1
        -- </select4-14.11>
    })

test:do_execsql_test(
    "select4-14.12",
    [[
        VALUES(1) UNION VALUES(2);
    ]], {
        -- <select4-14.12>
        1, 2
        -- </select4-14.12>
    })

test:do_execsql_test(
    "select4-14.13",
    [[
        VALUES(1),(2),(3) EXCEPT VALUES(2);
    ]], {
        -- <select4-14.13>
        1, 3
        -- </select4-14.13>
    })

test:do_execsql_test(
    "select4-14.14",
    [[
        VALUES(1),(2),(3) EXCEPT VALUES(1),(3);
    ]], {
        -- <select4-14.14>
        2
        -- </select4-14.14>
    })

test:do_execsql_test(
    "select4-14.15",
    [[
        SELECT * FROM (SELECT 123), (SELECT 456) ON likely(0 OR 1) OR 0;
    ]], {
        -- <select4-14.15>
        123, 456
        -- </select4-14.15>
    })

test:do_execsql_test(
    "select4-14.16",
    [[
        VALUES(1),(2),(3),(4) UNION ALL SELECT 5 LIMIT 99;
    ]], {
        -- <select4-14.16>
        1, 2, 3, 4, 5
        -- </select4-14.16>
    })

test:do_execsql_test(
    "select4-14.17",
    [[
        VALUES(1),(2),(3),(4) UNION ALL SELECT 5 LIMIT 3;
    ]], {
        -- <select4-14.17>
        1, 2, 3
        -- </select4-14.17>
    })

test:finish_test()

