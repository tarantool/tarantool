#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(70)

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
    "1.1",
    [[
        CREATE TABLE T1(IDT1  INT PRIMARY KEY, A INT, B INT, EX TEXT);
        CREATE INDEX I1 ON T1(A);
        CREATE INDEX I2 ON T1(B);
        CREATE TABLE T2(IDT2  INT PRIMARY KEY, A INT, B INT, EX TEXT);
        CREATE TABLE T3(IDT3  INT PRIMARY KEY, A INT, B INT, EX TEXT);
    ]])

test:do_eqp_test(
    "1.2",
    [[
        SELECT * FROM T2, T1 WHERE T1.A=1 OR T1.B=2;
    ]], {
        -- <1.2>
        {0, 0, 1, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?) (~10 rows)"},
        {0, 0, 1, "SEARCH TABLE T1 USING COVERING INDEX I2 (B=?) (~10 rows)"},
        {0, 1, 0, "SCAN TABLE T2 (~1048576 rows)"}
        -- </1.2>
    })

test:do_eqp_test(
    "1.3",
    [[
        SELECT * FROM T2 CROSS JOIN T1 WHERE T1.A=1 OR T1.B=2;
    ]], {
        -- <1.3>
        {0, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
        {0, 1, 1, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?) (~10 rows)"},
        {0, 1, 1, "SEARCH TABLE T1 USING COVERING INDEX I2 (B=?) (~10 rows)"}
        -- </1.3>
    })

test:do_eqp_test(
    "1.3",
    [[
        SELECT A FROM T1 ORDER BY A;
    ]], {
        -- <1.3>
        {0, 0, 0, "SCAN TABLE T1 USING COVERING INDEX I1 (~1048576 rows)"}
        -- </1.3>
    })

test:do_eqp_test(
    "1.4",
    [[
        SELECT A FROM T1 ORDER BY +A;
    ]], {
        -- <1.4>
        {0, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"}
        -- </1.4>
    })

test:do_eqp_test(
    "1.5",
    [[
        SELECT A FROM T1 WHERE A=4;
    ]], {
        -- <1.5>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?) (~10 rows)"}
        -- </1.5>
    })

test:do_eqp_test(
    "1.6",
    [[
        SELECT DISTINCT COUNT(*) FROM T3 GROUP BY A;
    ]], {
        -- <1.6>
        {0, 0, 0, "SCAN TABLE T3 (~1048576 rows)"},
        {0, 0, 0, "USE TEMP B-TREE FOR GROUP BY"},
        {0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},

        -- </1.6>
    })

test:do_eqp_test(
    "1.7",
    [[
        SELECT * FROM T3 JOIN (SELECT 1);
    ]], {
        -- <1.7>
        {0, 0, 1, "SCAN SUBQUERY 1 (~1 row)"},
        {0, 1, 0, "SCAN TABLE T3 (~1048576 rows)"},

        -- </1.7>
    })

test:do_eqp_test(
    "1.8",
    [[
        SELECT * FROM T3 JOIN (SELECT 1 UNION SELECT 2);
    ]], {
        -- <1.8>
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 USING TEMP B-TREE (UNION)"},
        {0, 0, 1, "SCAN SUBQUERY 1 (~1 row)"},
        {0, 1, 0, "SCAN TABLE T3 (~1048576 rows)"},

        -- </1.8>
    })

test:do_eqp_test(
    "1.9",
    [[
        SELECT * FROM T3 JOIN (SELECT 1 EXCEPT SELECT A FROM T3 LIMIT 17);
    ]], {
        -- <1.9>
        {3, 0, 0, "SCAN TABLE T3 (~1048576 rows)"},
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 USING TEMP B-TREE (EXCEPT)"},
        {0, 0, 1,"SCAN SUBQUERY 1 (~1 row)"},
        {0, 1, 0,"SCAN TABLE T3 (~1048576 rows)"},

        -- </1.9>
    })

test:do_eqp_test(
    "1.10",
    [[
        SELECT * FROM T3 JOIN (SELECT 1 INTERSECT SELECT A FROM T3 LIMIT 17);
    ]], {
        -- <1.10>
        {3, 0, 0, "SCAN TABLE T3 (~1048576 rows)"},
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 USING TEMP B-TREE (INTERSECT)"},
        {0, 0, 1, "SCAN SUBQUERY 1 (~1 row)"},
        {0, 1, 0, "SCAN TABLE T3 (~1048576 rows)"},

        -- </1.10>
    })

test:do_eqp_test(
    "1.11",
    [[
        SELECT * FROM T3 JOIN (SELECT 1 UNION ALL SELECT A FROM T3 LIMIT 17);
    ]], {
        -- <1.11>
        {3, 0, 0, "SCAN TABLE T3 (~1048576 rows)"},
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 (UNION ALL)"},
        {0, 0, 1, "SCAN SUBQUERY 1 (~1 row)"},
        {0, 1, 0, "SCAN TABLE T3 (~1048576 rows)"},

        -- </1.11>
    })

---------------------------------------------------------------------------
-- Test cases eqp-2.* - tests for single select statements.
--
test:drop_all_tables()
test:do_execsql_test(
    "2.1",
    [[
        CREATE TABLE T1(IDT1  INT PRIMARY KEY, X INT, Y INT, EX TEXT);
        CREATE TABLE T2(IDT2  INT PRIMARY KEY, X INT, Y INT, EX TEXT);
        CREATE INDEX T2I1 ON T2(X);
    ]])

test:do_eqp_test("2.2.1",
    "SELECT DISTINCT MIN(X), MAX(X) FROM T1 GROUP BY X ORDER BY 1", {
    {0, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
    {0, 0, 0, "USE TEMP B-TREE FOR GROUP BY"},
    {0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
test:do_eqp_test("2.2.2",
    "SELECT DISTINCT MIN(X), MAX(X) FROM T2 GROUP BY X ORDER BY 1", {
    {0, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},
    {0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
-- MUST_WORK_TEST wrong explain
--test:do_eqp_test("2.2.3", "SELECT DISTINCT * FROM t1", {
--    {0, 0, 0, "SCAN TABLE T1"},
--    {0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
--})
test:do_eqp_test("2.2.4", "SELECT DISTINCT * FROM T1, T2", {
    {0, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
    -- changed after reordering indexes
    -- actually it does not matter (in fact, it seems like pk should have been used in both cases)
    --{0, 1, 1, "SCAN TABLE T2 USING COVERING INDEX t2i1"},
    {0, 1, 1, "SCAN TABLE T2 (~1048576 rows)"},
    --{0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
})
test:do_eqp_test("2.2.5", "SELECT DISTINCT * FROM T1, T2 ORDER BY T1.X", {
    {0, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
    {0, 1, 1, "SCAN TABLE T2 (~1048576 rows)"},
    --{0, 0, 0, "USE TEMP B-TREE FOR DISTINCT"},
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
test:do_eqp_test("2.2.6", "SELECT DISTINCT T2.X FROM T1, T2 ORDER BY T2.X", {
    {0, 0, 1, "SCAN TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},
    {0, 1, 0, "SCAN TABLE T1 (~1048576 rows)"},
})
test:do_eqp_test("2.3.1", "SELECT MAX(X) FROM T2", {
    {0, 0, 0, "SEARCH TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},
})
test:do_eqp_test("2.3.2", "SELECT MIN(X) FROM T2", {
    {0, 0, 0, "SEARCH TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},
})
test:do_eqp_test("2.3.3", "SELECT MIN(X), MAX(X) FROM T2", {
    {0, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
})
test:do_eqp_test("2.4.1", "SELECT * FROM T1 WHERE IDT1=?", {
    {0, 0, 0, "SEARCH TABLE T1 USING PRIMARY KEY (IDT1=?) (~1 row)"},
})
---------------------------------------------------------------------------
-- Test cases eqp-3.* - tests for select statements that use sub-selects.
--
test:do_eqp_test(
    "3.1.1",
    [[
        SELECT (SELECT X FROM T1 AS SUB) FROM T1;
    ]], {
        -- <3.1.1>
        {0, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {0, 0, 0, "EXECUTE SCALAR SUBQUERY 1"},
        {1, 0, 0, "SCAN TABLE T1 AS SUB (~1048576 rows)"},

        -- </3.1.1>
    })

test:do_eqp_test(
    "3.1.2",
    [[
        SELECT * FROM T1 WHERE (SELECT X FROM T1 AS SUB);
    ]], {
        -- <3.1.2>
        {0, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {0, 0, 0, "EXECUTE SCALAR SUBQUERY 1"},
        {1, 0, 0, "SCAN TABLE T1 AS SUB (~1048576 rows)"},

        -- </3.1.2>
    })

test:do_eqp_test(
    "3.1.3",
    [[
        SELECT * FROM T1 WHERE (SELECT X FROM T1 AS SUB ORDER BY Y);
    ]], {
        -- <3.1.3>
        {0, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {0, 0, 0, "EXECUTE SCALAR SUBQUERY 1"},
        {1, 0, 0, "SCAN TABLE T1 AS SUB (~1048576 rows)"},
        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},

        -- </3.1.3>
    })

test:do_eqp_test(
    "3.1.4",
    [[
        SELECT * FROM T1 WHERE (SELECT X FROM T2 ORDER BY X);
    ]], {
        -- <3.1.4>
        {0, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {0, 0, 0, "EXECUTE SCALAR SUBQUERY 1"},
        {1, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},

        -- </3.1.4>
    })

test:do_eqp_test("3.2.1", [[
  SELECT * FROM (SELECT * FROM T1 ORDER BY X LIMIT 10) ORDER BY Y LIMIT 5;
]], {
    {1, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
    {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
    {0, 0, 0, "SCAN SUBQUERY 1 (~1 row)"},
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
test:do_eqp_test("3.2.2", [[
  SELECT * FROM
    (SELECT * FROM T1 ORDER BY X LIMIT 10) AS X1,
    (SELECT * FROM T2 ORDER BY X LIMIT 10) AS X2
  ORDER BY X2.Y LIMIT 5;
]], {
    {1, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
    {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
    {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},
    {0, 0, 0, "SCAN SUBQUERY 1 AS X1 (~1 row)"},
    {0, 1, 1, "SCAN SUBQUERY 2 AS X2 (~1 row)"},
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
test:do_eqp_test("3.3.1", [[
  SELECT * FROM T1 WHERE Y IN (SELECT Y FROM T2);
]], {
    {0, 0, 0, "SCAN TABLE T1 (~983040 rows)"},
    {0, 0, 0, "EXECUTE LIST SUBQUERY 1"},
    {1, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
})
test:do_eqp_test("3.3.2", [[
  SELECT * FROM T1 WHERE Y IN (SELECT Y FROM T2 WHERE T1.X!=T2.X);
]], {
    {0, 0, 0, "SCAN TABLE T1 (~983040 rows)"},
    {0, 0, 0, "EXECUTE CORRELATED LIST SUBQUERY 1"},
    {1, 0, 0, "SCAN TABLE T2 (~983040 rows)"},
})
test:do_eqp_test("3.3.3", [[
  SELECT * FROM T1 WHERE EXISTS (SELECT Y FROM T2 WHERE T1.X!=T2.X);
]], {
    {0, 0, 0, "SCAN TABLE T1 (~983040 rows)"},
    {0, 0, 0, "EXECUTE CORRELATED SCALAR SUBQUERY 1"},
    {1, 0, 0, "SCAN TABLE T2 (~983040 rows)"},
})
---------------------------------------------------------------------------
-- Test cases eqp-4.* - tests for composite select statements.
--
test:do_eqp_test(
    "4.1.1",
    [[
        SELECT * FROM T1 UNION ALL SELECT * FROM T2;
    ]], {
        -- <4.1.1>
        {1, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {2, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (UNION ALL)"},

        -- </4.1.1>
    })

test:do_eqp_test(
    "4.1.2",
    [[
        SELECT * FROM T1 UNION ALL SELECT * FROM T2 ORDER BY 2;
    ]], {
        -- <4.1.2>
        {1, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (UNION ALL)"},

        -- </4.1.2>
    })

test:do_eqp_test(
    "4.1.3",
    [[
        SELECT * FROM T1 UNION SELECT * FROM T2 ORDER BY 2;
    ]], {
        -- <4.1.3>
        {1, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},
        {2, 0, 0, "USE TEMP B-TREE FOR RIGHT PART OF ORDER BY"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (UNION)"},

        -- </4.1.3>
    })

test:do_eqp_test(
    "4.1.4",
    [[
        SELECT * FROM T1 INTERSECT SELECT * FROM T2 ORDER BY 2;
    ]], {
        -- <4.1.4>
        {1, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},
        {2, 0, 0, "USE TEMP B-TREE FOR RIGHT PART OF ORDER BY"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (INTERSECT)"},

        -- </4.1.4>
    })

test:do_eqp_test(
    "4.1.5",
    [[
        SELECT * FROM T1 EXCEPT SELECT * FROM T2 ORDER BY 2;
    ]], {
        -- <4.1.5>
        {1, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {1, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {2, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},
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
        SELECT X FROM T1 UNION SELECT X FROM T2;
    ]], {
        -- <4.3.1>
        {1, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {2, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 USING TEMP B-TREE (UNION)"},

        -- </4.3.1>
    })

test:do_eqp_test(
    "4.3.2",
    [[
        SELECT X FROM T1 UNION SELECT X FROM T2 UNION SELECT X FROM T1;
    ]], {
        -- <4.3.2>
        {2, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {3, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 USING TEMP B-TREE (UNION)"},
        {4, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 4 USING TEMP B-TREE (UNION)"}
        -- </4.3.2>
    })

test:do_eqp_test(
    "4.3.3",
    [[
        SELECT X FROM T1 UNION SELECT X FROM T2
        UNION SELECT X FROM T1 ORDER BY 1;
    ]], {
        -- <4.3.3>
        {2, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {2, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {3, 0, 0, "SCAN TABLE T2 USING COVERING INDEX T2I1 (~1048576 rows)"},
        {1, 0, 0, "COMPOUND SUBQUERIES 2 AND 3 (UNION)"},
        {4, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
        {4, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
        {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 4 (UNION)"}
        -- </4.3.3>
    })

---------------------------------------------------------------------------
-- This next block of tests verifies that the examples on the
-- lang_explain.html page are correct.
--
test:drop_all_tables()
-- EVIDENCE-OF: R-47779-47605 sql> EXPLAIN QUERY PLAN SELECT a, b
-- FROM t1 WHERE a=1;
-- 0|0|0|SCAN TABLE T1
--
test:do_execsql_test(
    "5.1.0",
    [[
        CREATE TABLE T1(IDT1  INT PRIMARY KEY, A INT, B INT, EX TEXT);
    ]])

test:do_eqp_test("5.1.1", "SELECT A, B FROM T1 WHERE A=1;", {
    {0, 0, 0, "SCAN TABLE T1 (~524288 rows)"},
})
-- EVIDENCE-OF: R-55852-17599 sql> CREATE INDEX i1 ON t1(a);
-- sql> EXPLAIN QUERY PLAN SELECT a, b FROM t1 WHERE a=1;
-- 0|0|0|SEARCH TABLE T1 USING COVERING INDEX i1
--
test:do_execsql_test(
    "5.2.0",
    [[
        CREATE INDEX I1 ON T1(A);
    ]])

test:do_eqp_test("5.2.1", "SELECT A, B FROM T1 WHERE A=1;", {
    {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?) (~10 rows)"},
})
-- EVIDENCE-OF: R-21179-11011 sql> CREATE INDEX i2 ON t1(a, b);
-- sql> EXPLAIN QUERY PLAN SELECT a, b FROM t1 WHERE a=1;
-- 0|0|0|SEARCH TABLE T1 USING COVERING INDEX i2 (a=?)
--
test:do_execsql_test(
    "5.3.0",
    [[
        CREATE INDEX I2 ON T1(A, B);
    ]])

test:do_eqp_test("5.3.1", "SELECT A, B FROM T1 WHERE A=1;", {
    -- It is equal for tarantol wheather to use i1 or i2
    -- because both of them are covering
    {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?) (~10 rows)"},
    --{0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"},
})
-- EVIDENCE-OF: R-09991-48941 sql> EXPLAIN QUERY PLAN
-- SELECT t1.*, t2.* FROM t1, t2 WHERE t1.a=1 AND t1.b>2;
-- 0|0|0|SEARCH TABLE T1 USING COVERING INDEX i2 (a=? AND b>?)
-- 0|1|1|SCAN TABLE T2
--
test:do_execsql_test(
    "5.4.0",
    [[
        CREATE TABLE T2(IDT2  INT PRIMARY KEY, C INT, D INT, EX TEXT);
    ]])

test:do_eqp_test("5.4.1",
    "SELECT T1.A, T2.C FROM T1, T2 WHERE T1.A=1 AND T1.B>2;", {
    {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I2 (A=? AND B>?) (~2 rows)"},
    {0, 1, 1, "SCAN TABLE T2 (~1048576 rows)"},
})
-- EVIDENCE-OF: R-33626-61085 sql> EXPLAIN QUERY PLAN
-- SELECT t1.*, t2.* FROM t2, t1 WHERE t1.a=1 AND t1.b>2;
-- 0|0|1|SEARCH TABLE T1 USING COVERING INDEX i2 (a=? AND b>?)
-- 0|1|0|SCAN TABLE T2
--
test:do_eqp_test(5.5,
    "SELECT T1.A, T2.C FROM T2, T1 WHERE T1.A=1 AND T1.B>2;", {
    {0, 0, 1, "SEARCH TABLE T1 USING COVERING INDEX I2 (A=? AND B>?) (~2 rows)"},
    {0, 1, 0, "SCAN TABLE T2 (~1048576 rows)"},
})
-- EVIDENCE-OF: R-04002-25654 sql> CREATE INDEX i3 ON t1(b);
-- sql> EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a=1 OR b=2;
-- 0|0|0|SEARCH TABLE T1 USING COVERING INDEX i2 (a=?)
-- 0|0|0|SEARCH TABLE T1 USING COVERING INDEX i3 (b=?)
--
test:do_execsql_test(
    "5.5.0",
    [[
        CREATE INDEX I3 ON T1(B);
    ]])

test:do_eqp_test("5.6.1", "SELECT A, B FROM T1 WHERE A=1 OR B=2;", {
    -- It is equal for tarantol wheather to use i1 or i2
    -- because both of them are covering
    --{0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I2 (A=?)"},
    {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?) (~10 rows)"},
    {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I3 (B=?) (~10 rows)"},
})
-- EVIDENCE-OF: R-24577-38891 sql> EXPLAIN QUERY PLAN
-- SELECT c, d FROM t2 ORDER BY c;
-- 0|0|0|SCAN TABLE T2
-- 0|0|0|USE TEMP B-TREE FOR ORDER BY
--
test:do_eqp_test(5.7, "SELECT C, D FROM T2 ORDER BY C;", {
    {0, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
    {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"},
})
-- EVIDENCE-OF: R-58157-12355 sql> CREATE INDEX i4 ON t2(c);
-- sql> EXPLAIN QUERY PLAN SELECT c, d FROM t2 ORDER BY c;
-- 0|0|0|SCAN TABLE T2 USING COVERING INDEX i4
--
test:do_execsql_test(
    "5.8.0",
    [[
        CREATE INDEX I4 ON T2(C)
    ]])

test:do_eqp_test("5.8.1", "SELECT C, D FROM T2 ORDER BY C;", {
    {0, 0, 0, "SCAN TABLE T2 USING COVERING INDEX I4 (~1048576 rows)"},
})
-- EVIDENCE-OF: R-13931-10421 sql> EXPLAIN QUERY PLAN SELECT
-- (SELECT b FROM t1 WHERE a=0), (SELECT a FROM t1 WHERE b=t2.c) FROM t2;
-- 0|0|0|SCAN TABLE T2
-- 0|0|0|EXECUTE SCALAR SUBQUERY 1
-- 1|0|0|SEARCH TABLE T1 USING COVERING INDEX i2 (a=?)
-- 0|0|0|EXECUTE CORRELATED SCALAR SUBQUERY 2
-- 2|0|0|SEARCH TABLE T1 USING COVERING INDEX i3 (b=?)
--
test:do_eqp_test("5.9", [[
  SELECT (SELECT B FROM T1 WHERE A=0), (SELECT A FROM T1 WHERE B=T2.C) FROM T2;
]], {
    {0, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
    {0, 0, 0, "EXECUTE SCALAR SUBQUERY 1"},
    -- It is equally for tarantol wheather to use i1 or i2
    -- because both of them are covering
    --{1, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I2 (A=?)"},
    {1, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?) (~10 rows)"},
    {0, 0, 0, "EXECUTE CORRELATED SCALAR SUBQUERY 2"},
    {2, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I3 (B=?) (~10 rows)"},
})
-- EVIDENCE-OF: R-50892-45943 sql> EXPLAIN QUERY PLAN
-- SELECT count(*) FROM (SELECT MAX(b) AS x FROM t1 GROUP BY a) GROUP BY x;
-- 1|0|0|SCAN TABLE T1 USING COVERING INDEX i2
-- 0|0|0|SCAN SUBQUERY 1
-- 0|0|0|USE TEMP B-TREE FOR GROUP BY
--
test:do_eqp_test("5.10", [[
  SELECT COUNT(*) FROM (SELECT MAX(B) AS X FROM T1 GROUP BY A) GROUP BY X;
]], {
    {1, 0, 0, "SCAN TABLE T1 USING COVERING INDEX I1 (~1048576 rows)"},
    {0, 0, 0, "SCAN SUBQUERY 1 (~1 row)"},
    {0, 0, 0, "USE TEMP B-TREE FOR GROUP BY"},
})
-- EVIDENCE-OF: R-46219-33846 sql> EXPLAIN QUERY PLAN
-- SELECT * FROM (SELECT * FROM t2 WHERE c=1), t1;
-- 0|0|0|SEARCH TABLE T2 USING COVERING INDEX i4 (c=?)
-- 0|1|1|SCAN TABLE T1
--
test:do_eqp_test("5.11", "SELECT A, B FROM (SELECT * FROM T2 WHERE C=1), T1;", {
    {0, 0, 0, "SEARCH TABLE T2 USING COVERING INDEX I4 (C=?) (~10 rows)"},
    {0, 1, 1, "SCAN TABLE T1 (~1048576 rows)"},
})
-- EVIDENCE-OF: R-37879-39987 sql> EXPLAIN QUERY PLAN
-- SELECT a FROM t1 UNION SELECT c FROM t2;
-- 1|0|0|SCAN TABLE T1
-- 2|0|0|SCAN TABLE T2
-- 0|0|0|COMPOUND SUBQUERIES 1 AND 2 USING TEMP B-TREE (UNION)
--
test:do_eqp_test("5.12", "SELECT A,B FROM T1 UNION SELECT C, 99 FROM T2;", {
    {1, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
    {2, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
    {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 USING TEMP B-TREE (UNION)"},
})
-- EVIDENCE-OF: R-44864-63011 sql> EXPLAIN QUERY PLAN
-- SELECT a FROM t1 EXCEPT SELECT d FROM t2 ORDER BY 1;
-- 1|0|0|SCAN TABLE T1 USING COVERING INDEX i2
-- 2|0|0|SCAN TABLE T2 2|0|0|USE TEMP B-TREE FOR ORDER BY
-- 0|0|0|COMPOUND SUBQUERIES 1 AND 2 (EXCEPT)
--
test:do_eqp_test("5.13",
    "SELECT A FROM T1 EXCEPT SELECT D FROM T2 ORDER BY 1;", {
    {1, 0, 0, "SCAN TABLE T1 USING COVERING INDEX I1 (~1048576 rows)"},
    {2, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
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
--     set stmt [sql_prepare_v2 db $sql -1 DUMMY]
--     print_explain_query_plan $stmt
--     sql_finalize $stmt
--   }
--   sql db test.db
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
    "7.0",
    [[
        CREATE TABLE T1(IDT1  INT PRIMARY KEY, A INT, B INT, EX VARCHAR(100));
        CREATE TABLE T2(IDT2  INT PRIMARY KEY, A INT, B INT, EX VARCHAR(100));
        CREATE INDEX I1 ON T2(A);
    ]])

test:do_eqp_test("7.1", "SELECT COUNT(*) FROM T1;", {
    {0, 0, 0, "B+tree count T1"},
})
test:do_eqp_test("7.2", "SELECT COUNT(*) FROM T2;", {
    {0, 0, 0, "B+tree count T2"},
})
-- MUST_WORK_TEST
if (0 > 0)
 then
    test:do_execsql_test(
        "7.3",
        [[
           INSERT INTO t1(a,b) VALUES(1, 2);
           INSERT INTO t1(a,b) VALUES(3, 4);

           INSERT INTO t2(a,b) VALUES(1, 2);
           INSERT INTO t2(a,b) VALUES(3, 4);
           INSERT INTO t2(a,b) VALUES(5, 6);

           ANALYZE;
        ]])

    test:do_eqp_test("7.4", "SELECT COUNT(*) FROM t1;", {
       {0, 0, 0, "SCAN TABLE T1"}
   })
    test:do_eqp_test("7.5", "SELECT COUNT(*) FROM t2;", {
       {0, 0, 0, "SCAN TABLE T2 USING COVERING INDEX I1"}
   })
    ---------------------------------------------------------------------------
    --The following tests - eqp-8.* - test that queries that use the OP_Count
    --optimization return something sensible with EQP.
end
test:drop_all_tables()
test:do_execsql_test(
    "8.0",
    [[
        CREATE TABLE T1(A INT , B INT , C INT , PRIMARY KEY(B, C));
        CREATE TABLE T2(ID INT PRIMARY KEY, A INT, B INT, C INT);
        CREATE TABLE T(ID INT PRIMARY KEY, R_D TEXT, S INTEGER);
        CREATE INDEX I1 ON T (R_D, S);
        CREATE INDEX I2 ON T (S);
    ]])

test:do_eqp_test("8.1.1", "SELECT * FROM T2;", {
    {0, 0, 0, "SCAN TABLE T2 (~1048576 rows)"},
})
-- test:do_eqp_test 8.1.2 "SELECT * FROM t2 WHERE rowid=?" {
--     {0, 0, 0, "SEARCH TABLE T2 USING INTEGER PRIMARY KEY (rowid=?)"},
-- }
test:do_eqp_test("8.1.3", "SELECT COUNT(*) FROM T2;", {
    {0, 0, 0, "B+tree count T2"},
})
test:do_eqp_test("8.2.1", "SELECT * FROM T1;", {
    {0, 0, 0, "SCAN TABLE T1 (~1048576 rows)"},
})
test:do_eqp_test("8.2.2", "SELECT * FROM T1 WHERE B=?;", {
    {0, 0, 0, "SEARCH TABLE T1 USING PRIMARY KEY (B=?) (~10 rows)"},
})
test:do_eqp_test("8.2.3", "SELECT * FROM T1 WHERE B=? AND C=?;", {
    {0, 0, 0, "SEARCH TABLE T1 USING PRIMARY KEY (B=? AND C=?) (~1 row)"},
})
test:do_eqp_test("8.2.4", "SELECT COUNT(*) FROM T1;", {
    {0, 0, 0, "B+tree count T1"},
})

-- Verify that INDEXED BY clause forces specified index.
-- Test case (in simplified form) is taken from customer.
--
test:do_eqp_test(
    "8.2.5.1",
    [[SELECT R_D, S FROM T INDEXED BY I1 WHERE R_D > '10' LIMIT 10;]], {
    {0, 0, 0, "SEARCH TABLE T USING COVERING INDEX I1 (R_D>?) (~262144 rows)"},
})

test:do_eqp_test(
    "8.2.5.2",
    [[SELECT R_D, S FROM T INDEXED BY I1 WHERE R_D > '10' AND S = 0 LIMIT 10;]],
    {
    { 0, 0, 0, "SEARCH TABLE T USING COVERING INDEX I1 (R_D>?) (~245760 rows)" },
})

test:finish_test()
