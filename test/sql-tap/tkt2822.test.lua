#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(36)

--!./tcltestrunner.lua
-- 2007 Dec 4
--
-- The author disclaims copyright to this source code. In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file is to test that the issues surrounding expressions in
-- ORDER BY clauses on compound SELECT statements raised by ticket
-- #2822 have been dealt with.
--
-- $Id: tkt2822.test,v 1.6 2008/08/20 16:35:10 drh Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- The ORDER BY matching algorithm is three steps:
-- 
--   (1)  If the ORDER BY term is an integer constant i, then
--        sort by the i-th column of the result set.
-- 
--   (2)  If the ORDER BY term is an identifier (not x.y or x.y.z
--        but simply x) then look for a column alias with the same
--        name.  If found, then sort by that column.
-- 
--   (3)  Evaluate the term as an expression and sort by the
--        value of the expression.
-- 
-- For a compound SELECT the rules are modified slightly.
-- In the third rule, the expression must exactly match one
-- of the result columns.  The sequences of three rules is
-- attempted first on the left-most SELECT.  If that doesn't
-- work, we move to the right, one by one.
--
-- Rule (3) is not in standard SQL - it is an SQLite extension,
-- though one copied from PostgreSQL.  The rule for compound
-- queries where a search is made of SELECTs to the right
-- if the left-most SELECT does not match is not a part of
-- standard SQL either.  This extension is unique to SQLite
-- as far as we know.
--
-- Rule (2) was added by the changes ticket #2822.  Prior to
-- that changes, SQLite did not support rule (2), making it
-- technically in violation of standard SQL semantics.  
-- No body noticed because rule (3) has the same effect as
-- rule (2) except in some obscure cases.
--
-- Test plan:
--
--   tkt2822-1.* - Simple identifier as ORDER BY expression.
--   tkt2822-2.* - More complex ORDER BY expressions.
test:do_execsql_test(
    "tkt2822-0.1",
    [[
        CREATE TABLE t1(a  INT primary key, b INT , c INT );
        CREATE TABLE t2(a  INT primary key, b INT , c INT );

        INSERT INTO t1 VALUES(1, 3, 9);
        INSERT INTO t1 VALUES(3, 9, 27);
        INSERT INTO t1 VALUES(5, 15, 45);

        INSERT INTO t2 VALUES(2, 6, 18);
        INSERT INTO t2 VALUES(4, 12, 36);
        INSERT INTO t2 VALUES(6, 18, 54);
    ]], {
        -- <tkt2822-0.1>
        
        -- </tkt2822-0.1>
    })

-- Test the "ORDER BY <integer>" syntax.
--
test:do_execsql_test(
    "tkt2822-1.1",
    [[
        SELECT a, b, c FROM t1 UNION ALL SELECT a, b, c FROM t2 ORDER BY 1;
    ]], {
        -- <tkt2822-1.1>
        1, 3, 9, 2, 6, 18, 3, 9, 27, 4, 12, 36, 5, 15, 45, 6, 18, 54
        -- </tkt2822-1.1>
    })

test:do_execsql_test(
    "tkt2822-1.2",
    [[
        SELECT a, CAST (b AS TEXT), c FROM t1 
          UNION ALL 
        SELECT a, b, c FROM t2 
          ORDER BY 2;
    ]], {
        -- <tkt2822-1.2>
        2, 6, 18, 4, 12, 36, 6, 18, 54, 5, "15", 45, 1, "3", 9, 3, "9", 27
        -- </tkt2822-1.2>
    })

-- Test the "ORDER BY <identifier>" syntax.
--
test:do_execsql_test(
    "tkt2822-2.1",
    [[
        SELECT a, b, c FROM t1 UNION ALL SELECT a, b, c FROM t2 ORDER BY a;
    ]], {
        -- <tkt2822-2.1>
        1, 3, 9, 2, 6, 18, 3, 9, 27, 4, 12, 36, 5, 15, 45, 6, 18, 54
        -- </tkt2822-2.1>
    })

test:do_execsql_test(
    "tkt2822-2.2",
    [[
        SELECT a, CAST (b AS TEXT) AS x, c FROM t1 
          UNION ALL 
        SELECT a, b, c FROM t2 
          ORDER BY x;
    ]], {
        -- <tkt2822-2.2>
        2, 6, 18, 4, 12, 36, 6, 18, 54, 5, "15", 45, 1, "3", 9, 3, "9", 27
        -- </tkt2822-2.2>
    })

test:do_execsql_test(
    "tkt2822-2.3",
    [[
        SELECT t1.a, b, c FROM t1 UNION ALL SELECT t2.a, b, c FROM t2 ORDER BY a;
    ]], {
        -- <tkt2822-2.3>
        1, 3, 9, 2, 6, 18, 3, 9, 27, 4, 12, 36, 5, 15, 45, 6, 18, 54
        -- </tkt2822-2.3>
    })

-- Test the "ORDER BY <expression>" syntax.
--
test:do_execsql_test(
    "tkt2822-3.1",
    [[
        SELECT a, CAST (b AS TEXT) AS x, c FROM t1 
          UNION ALL 
        SELECT a, b, c FROM t2 
          ORDER BY CAST (b AS TEXT);
    ]], {
        -- <tkt2822-3.1>
        2, 6, 18, 4, 12, 36, 6, 18, 54, 5, "15", 45, 1, "3", 9, 3, "9", 27
        -- </tkt2822-3.1>
    })

test:do_execsql_test(
    "tkt2822-3.2",
    [[
        SELECT t1.a, b, c FROM t1 UNION ALL SELECT t2.a, b, c FROM t2 ORDER BY t1.a;
    ]], {
        -- <tkt2822-3.2>
        1, 3, 9, 2, 6, 18, 3, 9, 27, 4, 12, 36, 5, 15, 45, 6, 18, 54
        -- </tkt2822-3.2>
    })

-- Test that if a match cannot be found in the leftmost SELECT, an
-- attempt is made to find a match in subsequent SELECT statements.
--
test:do_execsql_test(
    "tkt2822-3.3",
    [[
        SELECT a, b, c FROM t1 UNION ALL SELECT a AS x, b, c FROM t2 ORDER BY x;
    ]], {
        -- <tkt2822-3.3>
        1, 3, 9, 2, 6, 18, 3, 9, 27, 4, 12, 36, 5, 15, 45, 6, 18, 54
        -- </tkt2822-3.3>
    })

test:do_test(
    "tkt2822-3.4",
    function()
        -- But the leftmost SELECT takes precedence.
        return test:execsql [[
            SELECT a AS b, CAST (b AS TEXT) AS a, c FROM t1 
              UNION ALL 
            SELECT a, b, c FROM t2 
              ORDER BY a;
        ]]
    end, {
        -- <tkt2822-3.4>
        2, 6, 18, 4, 12, 36, 6, 18, 54, 5, "15", 45, 1, "3", 9, 3, "9", 27
        -- </tkt2822-3.4>
    })

test:do_execsql_test(
    "tkt2822-3.5",
    [[
        SELECT a, b, c FROM t2 
          UNION ALL 
        SELECT a AS b, CAST (b AS TEXT) AS a, c FROM t1 
          ORDER BY a;
    ]], {
        -- <tkt2822-3.5>
        1, "3", 9, 2, 6, 18, 3, "9", 27, 4, 12, 36, 5, "15", 45, 6, 18, 54
        -- </tkt2822-3.5>
    })

-- Test some error conditions (ORDER BY clauses that match no column).
--
test:do_catchsql_test(
    "tkt2822-4.1",
    [[
        SELECT a, b, c FROM t1 UNION ALL SELECT a, b, c FROM t2 ORDER BY x
    ]], {
        -- <tkt2822-4.1>
        1, "1st ORDER BY term does not match any column in the result set"
        -- </tkt2822-4.1>
    })

-- Tests for rule (2).
--
-- The "ORDER BY b" should match the column alias (rule 2), not the
-- the t3.b value (rule 3).  
--
test:do_execsql_test(
    "tkt2822-5.1",
    [[
        CREATE TABLE t3(a  INT primary key,b INT );
        INSERT INTO t3 VALUES(1,8);
        INSERT INTO t3 VALUES(9,2);

        SELECT a AS b FROM t3 ORDER BY b;
    ]], {
        -- <tkt2822-5.1>
        1, 9
        -- </tkt2822-5.1>
    })

test:do_test(
    "tkt2822-5.2",
    function()
        -- Case does not matter.  b should match B
        return test:execsql [[
            SELECT a AS b FROM t3 ORDER BY B;
        ]]
    end, {
        -- <tkt2822-5.2>
        1, 9
        -- </tkt2822-5.2>
    })

test:do_test(
    "tkt2822-5.3",
    function()
        -- Quoting should not matter
        return test:execsql [[
            SELECT a as b FROM t3 ORDER BY "B";
        ]]
    end, {
        -- <tkt2822-5.3>
        1, 9
        -- </tkt2822-5.3>
    })


-- In "ORDER BY +b" the term is now an expression rather than
-- a label.  It therefore matches by rule (3) instead of rule (2).
-- 
test:do_execsql_test(
    "tkt2822-5.5",
    [[
        SELECT a AS b FROM t3 ORDER BY +b;
    ]], {
        -- <tkt2822-5.5>
        9, 1
        -- </tkt2822-5.5>
    })

-- Tests for rule 2 in compound queries
--
test:do_execsql_test(
    "tkt2822-6.1",
    [[
        CREATE TABLE t6a(p  INT primary key,q INT );
        INSERT INTO t6a VALUES(1,8);
        INSERT INTO t6a VALUES(9,2);
        CREATE TABLE t6b(x  INT primary key,y INT );
        INSERT INTO t6b VALUES(1,7);
        INSERT INTO t6b VALUES(7,2);

        SELECT p, q FROM t6a UNION ALL SELECT x, y FROM t6b ORDER BY 1, 2
    ]], {
        -- <tkt2822-6.1>
        1, 7, 1, 8, 7, 2, 9, 2
        -- </tkt2822-6.1>
    })

test:do_execsql_test(
    "tkt2822-6.2",
    [[
        SELECT p PX, q QX FROM t6a UNION ALL SELECT x XX, y YX FROM t6b
        ORDER BY PX, YX
    ]], {
        -- <tkt2822-6.2>
        1, 7, 1, 8, 7, 2, 9, 2
        -- </tkt2822-6.2>
    })

test:do_execsql_test(
    "tkt2822-6.3",
    [[
        SELECT p PX, q QX FROM t6a UNION ALL SELECT x XX, y YX FROM t6b
        ORDER BY XX, QX
    ]], {
        -- <tkt2822-6.3>
        1, 7, 1, 8, 7, 2, 9, 2
        -- </tkt2822-6.3>
    })

test:do_execsql_test(
    "tkt2822-6.4",
    [[
        SELECT p PX, q QX FROM t6a UNION ALL SELECT x XX, y YX FROM t6b
        ORDER BY QX, XX
    ]], {
        -- <tkt2822-6.4>
        7, 2, 9, 2, 1, 7, 1, 8
        -- </tkt2822-6.4>
    })

test:do_execsql_test(
    "tkt2822-6.5",
    [[
        SELECT p PX, q QX FROM t6a UNION ALL SELECT x XX, y YX FROM t6b
        ORDER BY t6b.x, QX
    ]], {
        -- <tkt2822-6.5>
        1, 7, 1, 8, 7, 2, 9, 2
        -- </tkt2822-6.5>
    })

test:do_execsql_test(
    "tkt2822-6.6",
    [[
        SELECT p PX, q QX FROM t6a UNION ALL SELECT x XX, y YX FROM t6b
        ORDER BY t6a.q, XX
    ]], {
        -- <tkt2822-6.6>
        7, 2, 9, 2, 1, 7, 1, 8
        -- </tkt2822-6.6>
    })

-- More error message tests.  This is really more of a test of the
-- %r ordinal value formatting capablity added to sqlite3_snprintf()
-- by ticket #2822.
--
test:do_test(
    "tkt2822-7.1",
    function()
        test:execsql [[
            CREATE TABLE t7(a1  INT primary key,a2 INT ,a3 INT ,a4 INT ,a5 INT ,a6 INT ,a7 INT ,a8 INT ,a9 INT ,a10 INT ,a11 INT ,a12 INT ,a13 INT ,a14 INT ,
                            a15 INT ,a16 INT ,a17 INT ,a18 INT ,a19 INT ,a20 INT ,a21 INT ,a22 INT ,a23 INT ,a24 INT ,a25 INT );
        ]]
        return test:catchsql [[
            SELECT * FROM t7 ORDER BY 0;
        ]]
    end, {
        -- <tkt2822-7.1>
        1, "1st ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.1>
    })

test:do_catchsql_test(
    "tkt2822-7.2.1",
    [[
        SELECT * FROM t7 ORDER BY 1, 0;
    ]], {
        -- <tkt2822-7.2.1>
        1, "2nd ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.2.1>
    })

test:do_catchsql_test(
    "tkt2822-7.2.2",
    [[
        SELECT * FROM t7 ORDER BY 1, 26;
    ]], {
        -- <tkt2822-7.2.2>
        1, "2nd ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.2.2>
    })

test:do_catchsql_test(
    "tkt2822-7.2.3",
    [[
        SELECT * FROM t7 ORDER BY 1, 65536;
    ]], {
        -- <tkt2822-7.2.3>
        1, "2nd ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.2.3>
    })

test:do_catchsql_test(
    "tkt2822-7.3",
    [[
        SELECT * FROM t7 ORDER BY 1, 2, 0;
    ]], {
        -- <tkt2822-7.3>
        1, "3rd ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.3>
    })

test:do_catchsql_test(
    "tkt2822-7.4",
    [[
        SELECT * FROM t7 ORDER BY 1, 2, 3, 0;
    ]], {
        -- <tkt2822-7.4>
        1, "4th ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.4>
    })

test:do_catchsql_test(
    "tkt2822-7.9",
    [[
        SELECT * FROM t7 ORDER BY 1, 2, 3, 4, 5, 6, 7, 8, 0;
    ]], {
        -- <tkt2822-7.9>
        1, "9th ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.9>
    })

test:do_catchsql_test(
    "tkt2822-7.10",
    [[
        SELECT * FROM t7 ORDER BY 1, 2, 3, 4, 5, 6, 7, 8, 9, 0;
    ]], {
        -- <tkt2822-7.10>
        1, "10th ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.10>
    })

test:do_catchsql_test(
    "tkt2822-7.11",
    [[
        SELECT * FROM t7 ORDER BY 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0;
    ]], {
        -- <tkt2822-7.11>
        1, "11th ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.11>
    })

test:do_catchsql_test(
    "tkt2822-7.12",
    [[
        SELECT * FROM t7 ORDER BY 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 0;
    ]], {
        -- <tkt2822-7.12>
        1, "12th ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.12>
    })

test:do_catchsql_test(
    "tkt2822-7.13",
    [[
        SELECT * FROM t7 ORDER BY 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 0;
    ]], {
        -- <tkt2822-7.13>
        1, "13th ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.13>
    })

test:do_catchsql_test(
    "tkt2822-7.20",
    [[
        SELECT * FROM t7 ORDER BY 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                 11,12,13,14,15,16,17,18,19, 0
    ]], {
        -- <tkt2822-7.20>
        1, "20th ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.20>
    })

test:do_catchsql_test(
    "tkt2822-7.21",
    [[
        SELECT * FROM t7 ORDER BY 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                 11,12,13,14,15,16,17,18,19, 20, 0
    ]], {
        -- <tkt2822-7.21>
        1, "21st ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.21>
    })

test:do_catchsql_test(
    "tkt2822-7.22",
    [[
        SELECT * FROM t7 ORDER BY 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                 11,12,13,14,15,16,17,18,19, 20, 21, 0
    ]], {
        -- <tkt2822-7.22>
        1, "22nd ORDER BY term out of range - should be between 1 and 25"
        -- </tkt2822-7.22>
    })

test:finish_test()

