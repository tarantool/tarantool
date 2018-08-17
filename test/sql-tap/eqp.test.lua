#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(68)

--!./tcltestrunner.lua
-- 2010 November 6
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
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


local testprefix = "eqp"
---------------------------------------------------------------------------
--
-- eqp-1.*:        Assorted tests.
-- eqp-2.*:        Tests for single select statements.
-- eqp-3.*:        Select statements that execute sub-selects.
-- eqp-4.*:        Compound select statements.
-- ...
-- eqp-7.*:        "SELECT count(*) FROM tbl" statements (VDBE code OP_Count).
--

test:do_execsql_test(
    1.1,
    [[
        CREATE TABLE t1(idt1 primary key, a INT, b INT, ex TEXT);
        CREATE INDEX i1 ON t1(a);
        CREATE INDEX i2 ON t1(b);
        CREATE TABLE t2(idt2 primary key, a INT, b INT, ex TEXT);
        CREATE TABLE t3(idt3 primary key, a INT, b INT, ex TEXT);
    ]])

test:do_eqp_test(
    1.2,
    [[
        SELECT * FROM t2, t1 WHERE t1.a=1 OR t1.b=2;
    ]], {
        -- <1.2>
        {0, 0, 1, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"},
        {0, 0, 1, "SEARCH TABLE T1 USING COVERING INDEX I2 (B=?)"},
        {0, 1, 0, "SCAN TABLE T2"}
        -- </1.2>
    })

test:do_eqp_test(
    1.3,
    [[
        SELECT * FROM t2 CROSS JOIN t1 WHERE t1.a=1 OR t1.b=2;
    ]], {
        -- <1.3>
        {0, 0, 0, "SCAN TABLE T2"},
        {0, 1, 1, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"},
        {0, 1, 1, "SEARCH TABLE T1 USING COVERING INDEX I2 (B=?)"}
        -- </1.3>
    })

test:do_eqp_test(
    1.3,
    [[
        SELECT a FROM t1 ORDER BY a
    ]], {
        -- <1.3>
        {0, 0, 0, "SCAN TABLE T1 USING COVERING INDEX I1"}
        -- </1.3>
    })

test:do_eqp_test(
    1.4,
    [[
        SELECT a FROM t1 ORDER BY +a
    ]], {
        -- <1.4>
        {0, 0, 0, "SCAN TABLE T1"},
        {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"}
        -- </1.4>
    })

test:do_eqp_test(
    1.5,
    [[
        SELECT a FROM t1 WHERE a=4
    ]], {
        -- <1.5>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"}
        -- </1.5>
    })

test:do_eqp_test(
    1.6,
    [[
        SELECT DISTINCT count(*) FROM t3 GROUP BY a;
    ]], {
        -- <1.6>
        {0, 0, 0, "SCAN TABLE T3"},
        {0, 0, 0, "USE TEMP B-TREE FOR GROUP BY"},
        {0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
        
        -- </1.6>
    })

test:do_eqp_test(
    1.7,
    [[
        SELECT * FROM t3 JOIN (SELECT 1)
    ]], {
        -- <1.7>
        {0, 0, 1, "SCAN SUBQUERY 1"},
        {0, 1, 0, "SCAN TABLE T3"},
        
        -- </1.7>
    })

test:do_eqp_test(
    1.8,
    [[
        SELECT * FROM t3 JOIN (SELECT 1 UNION SELECT 2)
    ]], {
        -- <1.8>
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 USING TEMP B-TREE (UNION)"},
        {0, 0, 1, "SCAN SUBQUERY 1"},
        {0, 1, 0, "SCAN TABLE T3"},
        
        -- </1.8>
    })

test:do_eqp_test(
    1.9,
    [[
        SELECT * FROM t3 JOIN (SELECT 1 EXCEPT SELECT a FROM t3 LIMIT 17)
    ]], {
        -- <1.9>
        {3, 0, 0, "SCAN TABLE T3"},
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 USING TEMP B-TREE (EXCEPT)"},
        {0, 0, 1, "SCAN SUBQUERY 1"},
        {0, 1, 0, "SCAN TABLE T3"},
        
        -- </1.9>
    })

test:do_eqp_test(
    "1.10",
    [[
        SELECT * FROM t3 JOIN (SELECT 1 INTERSECT SELECT a FROM t3 LIMIT 17)
    ]], {
        -- <1.10>
        {3, 0, 0, "SCAN TABLE T3"},
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 USING TEMP B-TREE (INTERSECT)"},
        {0, 0, 1, "SCAN SUBQUERY 1"},
        {0, 1, 0, "SCAN TABLE T3"},
        
        -- </1.10>
    })

test:do_eqp_test(
    1.11,
    [[
        SELECT * FROM t3 JOIN (SELECT 1 UNION ALL SELECT a FROM t3 LIMIT 17)
    ]], {
        -- <1.11>
        {3, 0, 0, "SCAN TABLE T3"},
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 (UNION ALL)"},
        {0, 0, 1, "SCAN SUBQUERY 1"},
        {0, 1, 0, "SCAN TABLE T3"},
        
        -- </1.11>
    })

---------------------------------------------------------------------------
-- Test cases eqp-2.* - tests for single select statements.
--
test:drop_all_tables()
test:do_execsql_test(
    2.1,
    [[
        CREATE TABLE t1(idt1 primary key, x INT, y INT, ex TEXT);

        CREATE TABLE t2(idt2 primary key, x INT, y INT, ex TEXT);
        CREATE INDEX t2i1 ON t2(x);
    ]])

test:do_eqp_test("2.2.1", "SELECT DISTINCT min(x), max(x) FROM t1 GROUP BY x ORDER BY 1", {
    {0, 0, 0, "SCAN TABLE T1"},
    {0, 0, 0, "USE TEMP B-TREE FOR GROUP BY"},
    {0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
test:do_eqp_test("2.2.2", "SELECT DISTINCT min(x), max(x) FROM t2 GROUP BY x ORDER BY 1", {
    {0, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1"},
    {0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
-- MUST_WORK_TEST wrong explain
--test:do_eqp_test("2.2.3", "SELECT DISTINCT * FROM t1", {
--    {0, 0, 0, "SCAN TABLE T1"},
--    {0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
--})
test:do_eqp_test("2.2.4", "SELECT DISTINCT * FROM t1, t2", {
    {0, 0, 0, "SCAN TABLE T1"},
    -- changed after reordering indexes
    -- actually it does not matter (in fact, it seems like pk should have been used in both cases)
    --{0, 1, 1, "SCAN TABLE T2 USING COVERING INDEX t2i1"},
    {0, 1, 1, "SCAN TABLE T2"},
    --{0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
})
test:do_eqp_test("2.2.5", "SELECT DISTINCT * FROM t1, t2 ORDER BY t1.x", {
    {0, 0, 0, "SCAN TABLE T1"},
    {0, 1, 1, "SCAN TABLE T2"},
    --{0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
test:do_eqp_test("2.2.6", "SELECT DISTINCT t2.x FROM t1, t2 ORDER BY t2.x", {
    {0, 0, 1, "SCAN TABLE T2 USING COVERING INDEX T2I1"},
    {0, 1, 0, "SCAN TABLE T1"},
})
test:do_eqp_test("2.3.1", "SELECT max(x) FROM t2", {
    {0, 0, 0, "SEARCH TABLE T2 USING COVERING INDEX T2I1"},
})
test:do_eqp_test("2.3.2", "SELECT min(x) FROM t2", {
    {0, 0, 0, "SEARCH TABLE T2 USING COVERING INDEX T2I1"},
})
test:do_eqp_test("2.3.3", "SELECT min(x), max(x) FROM t2", {
    {0, 0, 0, "SCAN TABLE T2"},
})
test:do_eqp_test("2.4.1", "SELECT * FROM t1 WHERE idt1=?", {
    {0, 0, 0, "SEARCH TABLE T1 USING PRIMARY KEY (IDT1=?)"},
})
---------------------------------------------------------------------------
-- Test cases eqp-3.* - tests for select statements that use sub-selects.
--
test:do_eqp_test(
    "3.1.1",
    [[
        SELECT (SELECT x FROM t1 AS sub) FROM t1;
    ]], {
        -- <3.1.1>
        {0, 0, 0, "SCAN TABLE T1"},
        {0, 0, 0, "EXECUTE SCALAR SUBQUERY 1"},
        {1, 0, 0, "SCAN TABLE T1 AS SUB"},
        
        -- </3.1.1>
    })

test:do_eqp_test(
    "3.1.2",
    [[
        SELECT * FROM t1 WHERE (SELECT x FROM t1 AS sub);
    ]], {
        -- <3.1.2>
        {0, 0, 0, "SCAN TABLE T1"},
        {0, 0, 0, "EXECUTE SCALAR SUBQUERY 1"},
        {1, 0, 0, "SCAN TABLE T1 AS SUB"},
        
        -- </3.1.2>
    })

test:do_eqp_test(
    "3.1.3",
    [[
        SELECT * FROM t1 WHERE (SELECT x FROM t1 AS sub ORDER BY y);
    ]], {
        -- <3.1.3>
        {0, 0, 0, "SCAN TABLE T1"},
        {0, 0, 0, "EXECUTE SCALAR SUBQUERY 1"},
        {1, 0, 0, "SCAN TABLE T1 AS SUB"},
        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        
        -- </3.1.3>
    })

test:do_eqp_test(
    "3.1.4",
    [[
        SELECT * FROM t1 WHERE (SELECT x FROM t2 ORDER BY x);
    ]], {
        -- <3.1.4>
        {0, 0, 0, "SCAN TABLE T1"},
        {0, 0, 0, "EXECUTE SCALAR SUBQUERY 1"},
        {1, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1"},
        
        -- </3.1.4>
    })

test:do_eqp_test("3.2.1", [[
  SELECT * FROM (SELECT * FROM t1 ORDER BY x LIMIT 10) ORDER BY y LIMIT 5
]], {
    {1, 0, 0, "SCAN TABLE T1"}, 
    {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"}, 
    {0, 0, 0, "SCAN SUBQUERY 1"}, 
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
test:do_eqp_test("3.2.2", [[
  SELECT * FROM 
    (SELECT * FROM t1 ORDER BY x LIMIT 10) AS x1,
    (SELECT * FROM t2 ORDER BY x LIMIT 10) AS x2
  ORDER BY x2.y LIMIT 5
]], {
    {1, 0, 0, "SCAN TABLE T1"}, 
    {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"}, 
    {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1"},
    {0, 0, 0, "SCAN SUBQUERY 1 AS X1"}, 
    {0, 1, 1, "SCAN SUBQUERY 2 AS X2"}, 
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
test:do_eqp_test("3.3.1", [[
  SELECT * FROM t1 WHERE y IN (SELECT y FROM t2)
]], {
    {0, 0, 0, "SCAN TABLE T1"}, 
    {0, 0, 0, "EXECUTE LIST SUBQUERY 1"}, 
    {1, 0, 0, "SCAN TABLE T2"},
})
test:do_eqp_test("3.3.2", [[
  SELECT * FROM t1 WHERE y IN (SELECT y FROM t2 WHERE t1.x!=t2.x)
]], {
    {0, 0, 0, "SCAN TABLE T1"}, 
    {0, 0, 0, "EXECUTE CORRELATED LIST SUBQUERY 1"}, 
    {1, 0, 0, "SCAN TABLE T2"},
})
test:do_eqp_test("3.3.3", [[
  SELECT * FROM t1 WHERE EXISTS (SELECT y FROM t2 WHERE t1.x!=t2.x)
]], {
    {0, 0, 0, "SCAN TABLE T1"}, 
    {0, 0, 0, "EXECUTE CORRELATED SCALAR SUBQUERY 1"}, 
    {1, 0, 0, "SCAN TABLE T2"},
})
---------------------------------------------------------------------------
-- Test cases eqp-4.* - tests for composite select statements.
--
test:do_eqp_test(
    "4.1.1",
    [[
        SELECT * FROM t1 UNION ALL SELECT * FROM t2
    ]], {
        -- <4.1.1>
        {1, 0, 0, "SCAN TABLE T1"},
        {2, 0, 0, "SCAN TABLE T2"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (UNION ALL)"},
        
        -- </4.1.1>
    })

test:do_eqp_test(
    "4.1.2",
    [[
        SELECT * FROM t1 UNION ALL SELECT * FROM t2 ORDER BY 2
    ]], {
        -- <4.1.2>
        {1, 0, 0, "SCAN TABLE T1"},
        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (UNION ALL)"},
        
        -- </4.1.2>
    })

test:do_eqp_test(
    "4.1.3",
    [[
        SELECT * FROM t1 UNION SELECT * FROM t2 ORDER BY 2
    ]], {
        -- <4.1.3>
        {1, 0, 0, "SCAN TABLE T1"},
        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1"},
        {2, 0, 0, "USE TEMP B-TREE FOR RIGHT PART OF ORDER BY"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (UNION)"},
        
        -- </4.1.3>
    })

test:do_eqp_test(
    "4.1.4",
    [[
        SELECT * FROM t1 INTERSECT SELECT * FROM t2 ORDER BY 2
    ]], {
        -- <4.1.4>
        {1, 0, 0, "SCAN TABLE T1"},
        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1"},
        {2, 0, 0, "USE TEMP B-TREE FOR RIGHT PART OF ORDER BY"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (INTERSECT)"},
        
        -- </4.1.4>
    })

test:do_eqp_test(
    "4.1.5",
    [[
        SELECT * FROM t1 EXCEPT SELECT * FROM t2 ORDER BY 2
    ]], {
        -- <4.1.5>
        {1, 0, 0, "SCAN TABLE T1"},
        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1"},
        {2, 0, 0, "USE TEMP B-TREE FOR RIGHT PART OF ORDER BY"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (EXCEPT)"}
        -- </4.1.5>
    })
-- MUST_WORK_TEST wrong explain
--test:do_eqp_test(
--    "4.2.2",
--    [[
--        SELECT * FROM t1 UNION ALL SELECT * FROM t2 ORDER BY 1
--    ]], {
--        -- <4.2.2>
--        {1, 0, 0, "SCAN TABLE T1"},
--        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
--        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX t2i1"},
--        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (UNION ALL)"},
--
--        -- </4.2.2>
--    })

-- MUST_WORK_TEST wrong explain
--test:do_eqp_test(
--    "4.2.3",
--    [[
--        SELECT * FROM t1 UNION SELECT * FROM t2 ORDER BY 1
--    ]], {
--        -- <4.2.3>
--        {1, 0, 0, "SCAN TABLE T1"},
--        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
--        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX t2i1"},
--        {2, 0, 0, "USE TEMP B-TREE FOR RIGHT PART OF ORDER BY"},
--        0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (UNION)"
--        -- </4.2.3>
--    })

-- MUST_WORK_TEST wrong explain
--test:do_eqp_test(
--    "4.2.4",
--    [[
--        SELECT * FROM t1 INTERSECT SELECT * FROM t2 ORDER BY 1
--    ]], {
--        -- <4.2.4>
--        {1, 0, 0, "SCAN TABLE T1"},
--        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
--        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX t2i1"},
--        {2, 0, 0, "USE TEMP B-TREE FOR RIGHT PART OF ORDER BY"},
--        0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (INTERSECT)"
--        -- </4.2.4>
--    })

-- MUST_WORK_TEST wrong explain
--test:do_eqp_test(
--    "4.2.5",
--    [[
--        SELECT * FROM t1 EXCEPT SELECT * FROM t2 ORDER BY 1
--    ]], {
--        -- <4.2.5>
--        {1, 0, 0, "SCAN TABLE T1"},
--        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
--        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX t2i1"},
--        {2, 0, 0, "USE TEMP B-TREE FOR RIGHT PART OF ORDER BY"},
--        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (EXCEPT)"}
--        -- </4.2.5>
--    })

test:do_eqp_test(
    "4.3.1",
    [[
        SELECT x FROM t1 UNION SELECT x FROM t2
    ]], {
        -- <4.3.1>
        {1, 0, 0, "SCAN TABLE T1"},
        {2, 0, 0, "SCAN TABLE T2"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 USING TEMP B-TREE (UNION)"},
        
        -- </4.3.1>
    })

test:do_eqp_test(
    "4.3.2",
    [[
        SELECT x FROM t1 UNION SELECT x FROM t2 UNION SELECT x FROM t1
    ]], {
        -- <4.3.2>
        {2, 0, 0, "SCAN TABLE T1"},
        {3, 0, 0, "SCAN TABLE T2"},
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 USING TEMP B-TREE (UNION)"},
        {4, 0, 0, "SCAN TABLE T1"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 4 USING TEMP B-TREE (UNION)"}
        -- </4.3.2>
    })

test:do_eqp_test(
    "4.3.3",
    [[
        SELECT x FROM t1 UNION SELECT x FROM t2 UNION SELECT x FROM t1 ORDER BY 1
    ]], {
        -- <4.3.3>
        {2, 0, 0, "SCAN TABLE T1"},
        {2, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {3, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1"},
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 (UNION)"},
        {4, 0, 0, "SCAN TABLE T1"},
        {4, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 4 (UNION)"}
        -- </4.3.3>
    })

---------------------------------------------------------------------------
-- This next block of tests verifies that the examples on the 
-- lang_explain.html page are correct.
--
test:drop_all_tables()
-- EVIDENCE-OF: R-47779-47605 sqlite> EXPLAIN QUERY PLAN SELECT a, b
-- FROM t1 WHERE a=1;
-- 0|0|0|SCAN TABLE T1
--
test:do_execsql_test(
    "5.1.0",
    [[
        CREATE TABLE t1(idt1 PRIMARY KEY, a INT, b INT, ex TEXT) 
    ]])

test:do_eqp_test("5.1.1", "SELECT a, b FROM t1 WHERE a=1", {
    {0, 0, 0, "SCAN TABLE T1"},
})
-- EVIDENCE-OF: R-55852-17599 sqlite> CREATE INDEX i1 ON t1(a);
-- sqlite> EXPLAIN QUERY PLAN SELECT a, b FROM t1 WHERE a=1;
-- 0|0|0|SEARCH TABLE T1 USING COVERING INDEX i1
--
test:do_execsql_test(
    "5.2.0",
    [[
        CREATE INDEX i1 ON t1(a) 
    ]])

test:do_eqp_test("5.2.1", "SELECT a, b FROM t1 WHERE a=1", {
    {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"},
})
-- EVIDENCE-OF: R-21179-11011 sqlite> CREATE INDEX i2 ON t1(a, b);
-- sqlite> EXPLAIN QUERY PLAN SELECT a, b FROM t1 WHERE a=1;
-- 0|0|0|SEARCH TABLE T1 USING COVERING INDEX i2 (a=?)
--
test:do_execsql_test(
    "5.3.0",
    [[
        CREATE INDEX i2 ON t1(a, b) 
    ]])

test:do_eqp_test("5.3.1", "SELECT a, b FROM t1 WHERE a=1", {
    -- It is equal for tarantol wheather to use i1 or i2
    -- because both of them are covering
    {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"},
    --{0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"},
})
-- EVIDENCE-OF: R-09991-48941 sqlite> EXPLAIN QUERY PLAN
-- SELECT t1.*, t2.* FROM t1, t2 WHERE t1.a=1 AND t1.b>2;
-- 0|0|0|SEARCH TABLE T1 USING COVERING INDEX i2 (a=? AND b>?)
-- 0|1|1|SCAN TABLE T2
--
test:do_execsql_test(
    "5.4.0",
    [[
        CREATE TABLE t2(idt2 primary key, c INT, d INT, ex TEXT)
    ]])

test:do_eqp_test("5.4.1", "SELECT t1.a, t2.c FROM t1, t2 WHERE t1.a=1 AND t1.b>2", {
    {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I2 (A=? AND B>?)"},
    {0, 1, 1, "SCAN TABLE T2"},
})
-- EVIDENCE-OF: R-33626-61085 sqlite> EXPLAIN QUERY PLAN
-- SELECT t1.*, t2.* FROM t2, t1 WHERE t1.a=1 AND t1.b>2;
-- 0|0|1|SEARCH TABLE T1 USING COVERING INDEX i2 (a=? AND b>?)
-- 0|1|0|SCAN TABLE T2
--
test:do_eqp_test(5.5, "SELECT t1.a, t2.c FROM t2, t1 WHERE t1.a=1 AND t1.b>2", {
    {0, 0, 1, "SEARCH TABLE T1 USING COVERING INDEX I2 (A=? AND B>?)"},
    {0, 1, 0, "SCAN TABLE T2"},
})
-- EVIDENCE-OF: R-04002-25654 sqlite> CREATE INDEX i3 ON t1(b);
-- sqlite> EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a=1 OR b=2;
-- 0|0|0|SEARCH TABLE T1 USING COVERING INDEX i2 (a=?)
-- 0|0|0|SEARCH TABLE T1 USING COVERING INDEX i3 (b=?)
--
test:do_execsql_test(
    "5.5.0",
    [[
        CREATE INDEX i3 ON t1(b)
    ]])

test:do_eqp_test("5.6.1", "SELECT a, b FROM t1 WHERE a=1 OR b=2", {
    -- It is equal for tarantol wheather to use i1 or i2
    -- because both of them are covering
    --{0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I2 (A=?)"},
    {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"},
    {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I3 (B=?)"},
})
-- EVIDENCE-OF: R-24577-38891 sqlite> EXPLAIN QUERY PLAN
-- SELECT c, d FROM t2 ORDER BY c;
-- 0|0|0|SCAN TABLE T2
-- 0|0|0|USE TEMP B-TREE FOR ORDER BY
--
test:do_eqp_test(5.7, "SELECT c, d FROM t2 ORDER BY c", {
    {0, 0, 0, "SCAN TABLE T2"},
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
-- EVIDENCE-OF: R-58157-12355 sqlite> CREATE INDEX i4 ON t2(c);
-- sqlite> EXPLAIN QUERY PLAN SELECT c, d FROM t2 ORDER BY c;
-- 0|0|0|SCAN TABLE T2 USING COVERING INDEX i4
--
test:do_execsql_test(
    "5.8.0",
    [[
        CREATE INDEX i4 ON t2(c)
    ]])

test:do_eqp_test("5.8.1", "SELECT c, d FROM t2 ORDER BY c", {
    {0, 0, 0, "SCAN TABLE T2 USING COVERING INDEX I4"},
})
-- EVIDENCE-OF: R-13931-10421 sqlite> EXPLAIN QUERY PLAN SELECT
-- (SELECT b FROM t1 WHERE a=0), (SELECT a FROM t1 WHERE b=t2.c) FROM t2;
-- 0|0|0|SCAN TABLE T2
-- 0|0|0|EXECUTE SCALAR SUBQUERY 1
-- 1|0|0|SEARCH TABLE T1 USING COVERING INDEX i2 (a=?)
-- 0|0|0|EXECUTE CORRELATED SCALAR SUBQUERY 2
-- 2|0|0|SEARCH TABLE T1 USING COVERING INDEX i3 (b=?)
--
test:do_eqp_test(5.9, [[
  SELECT (SELECT b FROM t1 WHERE a=0), (SELECT a FROM t1 WHERE b=t2.c) FROM t2
]], {
    {0, 0, 0, "SCAN TABLE T2"},
    {0, 0, 0, "EXECUTE SCALAR SUBQUERY 1"},
    -- It is equally for tarantol wheather to use i1 or i2
    -- because both of them are covering
    --{1, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I2 (A=?)"},
    {1, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"},
    {0, 0, 0, "EXECUTE CORRELATED SCALAR SUBQUERY 2"},
    {2, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I3 (B=?)"},
})
-- EVIDENCE-OF: R-50892-45943 sqlite> EXPLAIN QUERY PLAN
-- SELECT count(*) FROM (SELECT max(b) AS x FROM t1 GROUP BY a) GROUP BY x;
-- 1|0|0|SCAN TABLE T1 USING COVERING INDEX i2
-- 0|0|0|SCAN SUBQUERY 1
-- 0|0|0|USE TEMP B-TREE FOR GROUP BY
--
test:do_eqp_test(5.10, [[
  SELECT count(*) FROM (SELECT max(b) AS x FROM t1 GROUP BY a) GROUP BY x
]], {
    {1, 0, 0, "SCAN TABLE T1 USING COVERING INDEX I1"},
    {0, 0, 0, "SCAN SUBQUERY 1"},
    {0, 0, 0, "USE TEMP B-TREE FOR GROUP BY"},
})
-- EVIDENCE-OF: R-46219-33846 sqlite> EXPLAIN QUERY PLAN
-- SELECT * FROM (SELECT * FROM t2 WHERE c=1), t1;
-- 0|0|0|SEARCH TABLE T2 USING COVERING INDEX i4 (c=?)
-- 0|1|1|SCAN TABLE T1
--
test:do_eqp_test(5.11, "SELECT a, b FROM (SELECT * FROM t2 WHERE c=1), t1", {
    {0, 0, 0, "SEARCH TABLE T2 USING COVERING INDEX I4 (C=?)"},
    {0, 1, 1, "SCAN TABLE T1"},
})
-- EVIDENCE-OF: R-37879-39987 sqlite> EXPLAIN QUERY PLAN
-- SELECT a FROM t1 UNION SELECT c FROM t2;
-- 1|0|0|SCAN TABLE T1
-- 2|0|0|SCAN TABLE T2
-- 0|0|0|COMPOUND SUBQUERIES 1 AND 2 USING TEMP B-TREE (UNION)
--
test:do_eqp_test(5.12, "SELECT a,b FROM t1 UNION SELECT c, 99 FROM t2", {
    {1, 0, 0, "SCAN TABLE T1"},
    {2, 0, 0, "SCAN TABLE T2"},
    {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 USING TEMP B-TREE (UNION)"},
})
-- EVIDENCE-OF: R-44864-63011 sqlite> EXPLAIN QUERY PLAN
-- SELECT a FROM t1 EXCEPT SELECT d FROM t2 ORDER BY 1;
-- 1|0|0|SCAN TABLE T1 USING COVERING INDEX i2
-- 2|0|0|SCAN TABLE T2 2|0|0|USE TEMP B-TREE FOR ORDER BY
-- 0|0|0|COMPOUND SUBQUERIES 1 AND 2 (EXCEPT)
--
test:do_eqp_test(5.13, "SELECT a FROM t1 EXCEPT SELECT d FROM t2 ORDER BY 1", {
    {1, 0, 0, "SCAN TABLE T1 USING COVERING INDEX I1"},
    {2, 0, 0, "SCAN TABLE T2"},
    {2, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
    {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (EXCEPT)"},
})
-- #-------------------------------------------------------------------------
-- # The following tests - eqp-6.* - test that the example C code on 
-- # documentation page eqp.html works. The C code is duplicated in test1.c
-- # and wrapped in Tcl command [print_explain_query_plan] 
-- #
-- set boilerplate {
--   proc explain_query_plan {db sql} {
--     set stmt [sqlite3_prepare_v2 db $sql -1 DUMMY]
--     print_explain_query_plan $stmt
--     sqlite3_finalize $stmt
--   }
--   sqlite3 db test.db
--   explain_query_plan db {%SQL%}
--   db close
--   exit
-- }
-- # Do a "Print Explain Query Plan" test.
-- proc do_peqp_test {tn sql res} {
--   set fd [open script.tcl w]
--   puts $fd [string map [list %SQL% $sql] $::boilerplate]
--   close $fd
--   uplevel do_test $tn [list {
--     set fd [open "|[info nameofexec] script.tcl"]
--     set data [read $fd]
--     close $fd
--     set data
--   }] [list $res]
-- }
-- do_peqp_test 6.1 {
--   SELECT a, b FROM t1 EXCEPT SELECT d, 99 FROM t2 ORDER BY 1
-- } [string trimleft {
-- 1 0 0 SCAN TABLE T1 USING COVERING INDEX i2
-- 2 0 0 SCAN TABLE T2
-- 2 0 0 USE TEMP B-TREE FOR ORDER BY
-- 0 0 0 COMPOUND SUBQUERIES 1 AND 2 (EXCEPT)
-- }]
---------------------------------------------------------------------------
-- The following tests - eqp-7.* - test that queries that use the OP_Count
-- optimization return something sensible with EQP.
--
test:drop_all_tables()
test:do_execsql_test(
    7.0,
    [[
        CREATE TABLE t1(idt1 primary key, a INT, b INT, ex CHAR(100));
        CREATE TABLE t2(idt2 primary key, a INT, b INT, ex CHAR(100));
        CREATE INDEX i1 ON t2(a);
    ]])

test:do_eqp_test(7.1, "SELECT count(*) FROM t1", {
    {0, 0, 0, "B+tree count T1"},
})
test:do_eqp_test(7.2, "SELECT count(*) FROM t2", {
    {0, 0, 0, "B+tree count T2"},
})
-- MUST_WORK_TEST
if (0 > 0)
 then
    test:do_execsql_test(
        7.3,
        [[
           INSERT INTO t1(a,b) VALUES(1, 2);
           INSERT INTO t1(a,b) VALUES(3, 4);

           INSERT INTO t2(a,b) VALUES(1, 2);
           INSERT INTO t2(a,b) VALUES(3, 4);
           INSERT INTO t2(a,b) VALUES(5, 6);

           ANALYZE;
        ]])

    --db("close")
    --sqlite3("db", "test.db")
    test:do_eqp_test(7.4, "SELECT count(*) FROM t1", {
       {0, 0, 0, "SCAN TABLE T1"}
   })
    test:do_eqp_test(7.5, "SELECT count(*) FROM t2", {
       {0, 0, 0, "SCAN TABLE T2 USING COVERING INDEX I1"}
   })
    ---------------------------------------------------------------------------
    --The following tests - eqp-8.* - test that queries that use the OP_Count
    --optimization return something sensible with EQP.
end
test:drop_all_tables()
test:do_execsql_test(
    8.0,
    [[
        CREATE TABLE t1(a, b, c, PRIMARY KEY(b, c));
        CREATE TABLE t2(id primary key, a, b, c);
    ]])

test:do_eqp_test("8.1.1", "SELECT * FROM t2", {
    {0, 0, 0, "SCAN TABLE T2"},
})
-- test:do_eqp_test 8.1.2 "SELECT * FROM t2 WHERE rowid=?" {
--     {0, 0, 0, "SEARCH TABLE T2 USING INTEGER PRIMARY KEY (rowid=?)"},
-- }
test:do_eqp_test("8.1.3", "SELECT count(*) FROM t2", {
    {0, 0, 0, "B+tree count T2"},
})
test:do_eqp_test("8.2.1", "SELECT * FROM t1", {
    {0, 0, 0, "SCAN TABLE T1"},
})
test:do_eqp_test("8.2.2", "SELECT * FROM t1 WHERE b=?", {
    {0, 0, 0, "SEARCH TABLE T1 USING PRIMARY KEY (B=?)"},
})
test:do_eqp_test("8.2.3", "SELECT * FROM t1 WHERE b=? AND c=?", {
    {0, 0, 0, "SEARCH TABLE T1 USING PRIMARY KEY (B=? AND C=?)"},
})
test:do_eqp_test("8.2.4", "SELECT count(*) FROM t1", {
    {0, 0, 0, "B+tree count T1"},
})


test:finish_test()
