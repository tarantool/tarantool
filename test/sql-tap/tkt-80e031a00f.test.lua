#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(118)

--!./tcltestrunner.lua
-- 2010 July 14
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library. Specifically,
-- it tests that ticket [80e031a00f45dca877ed92b225209cfa09280f4f] has been
-- resolved.  That ticket is about IN and NOT IN operators with empty-set
-- right-hand sides.  Such expressions should always return TRUE or FALSE
-- even if the left-hand side is NULL.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- ["source",[["testdir"],"\/lock_common.tcl"]]
-- ["source",[["testdir"],"\/malloc_common.tcl"]]
-- EVIDENCE-OF: R-58875-56087 The IN and NOT IN operators take a single
-- scalar operand on the left and a vector operand on the right formed by
-- an explicit list of zero or more scalars or by a single subquery.
--
-- EVIDENCE-OF: R-52275-55503 When the right operand is an empty set, the
-- result of IN is false and the result of NOT IN is true, regardless of
-- the left operand and even if the left operand is NULL.
--
-- EVIDENCE-OF: R-13595-45863 Note that sql allows the parenthesized
-- list of scalar values on the right-hand side of an IN or NOT IN
-- operator to be an empty list but most other SQL database database
-- engines and the SQL92 standard require the list to contain at least
-- one element.
--
test:do_execsql_test(
    "tkt-80e031a00f.1",
    [[
        SELECT 1 IN ()
    ]], {
        -- <tkt-80e031a00f.1>
        false
        -- </tkt-80e031a00f.1>
    })

test:do_execsql_test(
    "tkt-80e031a00f.1b",
    [[
        SELECT 1 IN (2)
    ]], {
        -- <tkt-80e031a00f.1b>
        false
        -- </tkt-80e031a00f.1b>
    })

test:do_execsql_test(
    "tkt-80e031a00f.1c",
    [[
        SELECT 1 IN (2,3,4,5,6,7,8,9)
    ]], {
        -- <tkt-80e031a00f.1c>
        false
        -- </tkt-80e031a00f.1c>
    })

test:do_execsql_test(
    "tkt-80e031a00f.2",
    [[
        SELECT 1 NOT IN ()
    ]], {
        -- <tkt-80e031a00f.2>
        true
        -- </tkt-80e031a00f.2>
    })

test:do_execsql_test(
    "tkt-80e031a00f.2b",
    [[
        SELECT 1 NOT IN (2)
    ]], {
        -- <tkt-80e031a00f.2b>
        true
        -- </tkt-80e031a00f.2b>
    })

test:do_execsql_test(
    "tkt-80e031a00f.2c",
    [[
        SELECT 1 NOT IN (2,3,4,5,6,7,8,9)
    ]], {
        -- <tkt-80e031a00f.2c>
        true
        -- </tkt-80e031a00f.2c>
    })

test:do_execsql_test(
    "tkt-80e031a00f.3",
    [[
        SELECT null IN ()
    ]], {
        -- <tkt-80e031a00f.3>
        false
        -- </tkt-80e031a00f.3>
    })

test:do_execsql_test(
    "tkt-80e031a00f.4",
    [[
        SELECT null NOT IN ()
    ]], {
        -- <tkt-80e031a00f.4>
        true
        -- </tkt-80e031a00f.4>
    })

test:do_execsql_test(
    "tkt-80e031a00f.5",
    [[
        CREATE TABLE t1(x  INT PRIMARY key);
        SELECT 1 IN t1;
    ]], {
        -- <tkt-80e031a00f.5>
        false
        -- </tkt-80e031a00f.5>
    })

test:do_execsql_test(
    "tkt-80e031a00f.6",
    [[
        SELECT 1 NOT IN t1
    ]], {
        -- <tkt-80e031a00f.6>
        true
        -- </tkt-80e031a00f.6>
    })

test:do_execsql_test(
    "tkt-80e031a00f.7",
    [[
        SELECT null IN t1
    ]], {
        -- <tkt-80e031a00f.7>
        false
        -- </tkt-80e031a00f.7>
    })

test:do_execsql_test(
    "tkt-80e031a00f.8",
    [[
        SELECT null NOT IN t1
    ]], {
        -- <tkt-80e031a00f.8>
        true
        -- </tkt-80e031a00f.8>
    })

test:do_execsql_test(
    "tkt-80e031a00f.9",
    [[
        CREATE TABLE t2(y INTEGER PRIMARY KEY);
        SELECT 1 IN t2;
    ]], {
        -- <tkt-80e031a00f.9>
        false
        -- </tkt-80e031a00f.9>
    })

test:do_execsql_test(
    "tkt-80e031a00f.10",
    [[
        SELECT 1 NOT IN t2
    ]], {
        -- <tkt-80e031a00f.10>
        true
        -- </tkt-80e031a00f.10>
    })

test:do_execsql_test(
    "tkt-80e031a00f.11",
    [[
        SELECT null IN t2
    ]], {
        -- <tkt-80e031a00f.11>
        false
        -- </tkt-80e031a00f.11>
    })

test:do_execsql_test(
    "tkt-80e031a00f.12",
    [[
        SELECT null NOT IN t2
    ]], {
        -- <tkt-80e031a00f.12>
        true
        -- </tkt-80e031a00f.12>
    })

test:do_execsql_test(
    "tkt-80e031a00f.13",
    [[
        CREATE TABLE t3(z INT PRIMARY KEY);
        SELECT 1 IN t3;
    ]], {
        -- <tkt-80e031a00f.13>
        false
        -- </tkt-80e031a00f.13>
    })

test:do_execsql_test(
    "tkt-80e031a00f.14",
    [[
        SELECT 1 NOT IN t3
    ]], {
        -- <tkt-80e031a00f.14>
        true
        -- </tkt-80e031a00f.14>
    })

test:do_execsql_test(
    "tkt-80e031a00f.15",
    [[
        SELECT null IN t3
    ]], {
        -- <tkt-80e031a00f.15>
        false
        -- </tkt-80e031a00f.15>
    })

test:do_execsql_test(
    "tkt-80e031a00f.16",
    [[
        SELECT null NOT IN t3
    ]], {
        -- <tkt-80e031a00f.16>
        true
        -- </tkt-80e031a00f.16>
    })

test:do_execsql_test(
    "tkt-80e031a00f.17",
    [[
        SELECT 1 IN (SELECT x+y FROM t1, t2)
    ]], {
        -- <tkt-80e031a00f.17>
        false
        -- </tkt-80e031a00f.17>
    })

test:do_execsql_test(
    "tkt-80e031a00f.18",
    [[
        SELECT 1 NOT IN (SELECT x+y FROM t1,t2)
    ]], {
        -- <tkt-80e031a00f.18>
        true
        -- </tkt-80e031a00f.18>
    })

test:do_execsql_test(
    "tkt-80e031a00f.19",
    [[
        SELECT null IN (SELECT x+y FROM t1,t2)
    ]], {
        -- <tkt-80e031a00f.19>
        false
        -- </tkt-80e031a00f.19>
    })

test:do_execsql_test(
    "tkt-80e031a00f.20",
    [[
        SELECT null NOT IN (SELECT x+y FROM t1,t2)
    ]], {
        -- <tkt-80e031a00f.20>
        true
        -- </tkt-80e031a00f.20>
    })

test:do_execsql_test(
    "tkt-80e031a00f.21",
    [[
        SELECT 1.23 IN ()
    ]], {
        -- <tkt-80e031a00f.21>
        false
        -- </tkt-80e031a00f.21>
    })

test:do_execsql_test(
    "tkt-80e031a00f.22",
    [[
        SELECT 1.23 NOT IN ()
    ]], {
        -- <tkt-80e031a00f.22>
        true
        -- </tkt-80e031a00f.22>
    })

test:do_execsql_test(
    "tkt-80e031a00f.23",
    [[
        SELECT 1.23 IN t1
    ]], {
        -- <tkt-80e031a00f.23>
        false
        -- </tkt-80e031a00f.23>
    })

test:do_execsql_test(
    "tkt-80e031a00f.24",
    [[
        SELECT 1.23 NOT IN t1
    ]], {
        -- <tkt-80e031a00f.24>
        true
        -- </tkt-80e031a00f.24>
    })

test:do_execsql_test(
    "tkt-80e031a00f.25",
    [[
        SELECT 'hello' IN ()
    ]], {
        -- <tkt-80e031a00f.25>
        false
        -- </tkt-80e031a00f.25>
    })

test:do_execsql_test(
    "tkt-80e031a00f.26",
    [[
        SELECT 'hello' NOT IN ()
    ]], {
        -- <tkt-80e031a00f.26>
        true
        -- </tkt-80e031a00f.26>
    })

test:do_catchsql_test(
    "tkt-80e031a00f.27",
    [[
        SELECT 'hello' IN t1
    ]], {
        -- <tkt-80e031a00f.27>
        1, 'Type mismatch: can not convert hello to integer'
        -- </tkt-80e031a00f.27>
    })

test:do_catchsql_test(
    "tkt-80e031a00f.28",
    [[
        SELECT 'hello' NOT IN t1
    ]], {
        -- <tkt-80e031a00f.28>
        1, 'Type mismatch: can not convert hello to integer'
        -- </tkt-80e031a00f.28>
    })

test:do_execsql_test(
    "tkt-80e031a00f.29",
    [[
        SELECT x'303132' IN ()
    ]], {
        -- <tkt-80e031a00f.29>
        false
        -- </tkt-80e031a00f.29>
    })

test:do_execsql_test(
    "tkt-80e031a00f.30",
    [[
        SELECT x'303132' NOT IN ()
    ]], {
        -- <tkt-80e031a00f.30>
        true
        -- </tkt-80e031a00f.30>
    })

test:do_catchsql_test(
    "tkt-80e031a00f.31",
    [[
        SELECT x'303132' IN t1
    ]], {
        -- <tkt-80e031a00f.31>
        1, 'Type mismatch: can not convert varbinary to integer'
        -- </tkt-80e031a00f.31>
    })

test:do_catchsql_test(
    "tkt-80e031a00f.32",
    [[
        SELECT x'303132' NOT IN t1
    ]], {
        -- <tkt-80e031a00f.32>
        1, 'Type mismatch: can not convert varbinary to integer'
        -- </tkt-80e031a00f.32>
    })

-- EVIDENCE-OF: R-50221-42915 The result of an IN or NOT IN operator is
-- determined by the following matrix: Left operand is NULL Right operand
-- contains NULL Right operand is an empty set Left operand found within
-- right operand Result of IN operator Result of NOT IN operator no no no
-- no false true does not matter no yes no false true no does not matter
-- no yes true false no yes no no NULL NULL yes does not matter no does
-- not matter NULL NULL
--
-- Row 1:
test:do_execsql_test(
    "tkt-80e031a00f.100",
    [[
        SELECT 1 IN (2,3,4)
    ]], {
        -- <tkt-80e031a00f.100>
        false
        -- </tkt-80e031a00f.100>
    })

test:do_execsql_test(
    "tkt-80e031a00f.101",
    [[
        SELECT 1 NOT IN (2,3,4)
    ]], {
        -- <tkt-80e031a00f.101>
        true
        -- </tkt-80e031a00f.101>
    })

test:do_execsql_test(
    "tkt-80e031a00f.102",
    [[
        SELECT 'a' IN ('b','c','d')
    ]], {
        -- <tkt-80e031a00f.102>
        false
        -- </tkt-80e031a00f.102>
    })

test:do_execsql_test(
    "tkt-80e031a00f.103",
    [[
        SELECT 'a' NOT IN ('b','c','d')
    ]], {
        -- <tkt-80e031a00f.103>
        true
        -- </tkt-80e031a00f.103>
    })

test:do_test(
    "tkt-80e031a00f.104",
    function()
        test:execsql [[
            CREATE TABLE t4(a  INT PRIMARY KEY);
            CREATE TABLE t5(b INTEGER PRIMARY KEY);
            CREATE TABLE t6(c  INT PRIMARY KEY);
            INSERT INTO t4 VALUES(2);
            INSERT INTO t4 VALUES(3);
            INSERT INTO t4 VALUES(4);
            INSERT INTO t5 SELECT * FROM t4;
            INSERT INTO t6 SELECT * FROM t4;
            CREATE TABLE t4n(a INT , b  INT PRIMARY KEY);
            INSERT INTO t4n VALUES(2, 1),
                            (3, 2),
                            (4, 3),
                            (null, 4);
            CREATE TABLE t6n(c INT , b  INT PRIMARY KEY);
            INSERT INTO t6n select * from t4n;
            CREATE TABLE t7(a TEXT PRIMARY KEY);
            CREATE TABLE t8(c TEXT PRIMARY KEY);
            INSERT INTO t7 VALUES('b');
            INSERT INTO t7 VALUES('c');
            INSERT INTO t7 VALUES('d');
            INSERT INTO t8 SELECT * FROM t7;
            CREATE TABLE t7n(a TEXT, b  INT PRIMARY KEY);
            CREATE TABLE t8n(c TEXT, b  INT PRIMARY KEY);
            INSERT INTO t7n VALUES('b', 1),
                                  ('c', 2),
                                  ('d', 3),
                                  (null, 4);
            INSERT INTO t8n SELECT * FROM t7n;
        ]]
        return test:execsql "SELECT 1 IN t4"
    end, {
        -- <tkt-80e031a00f.104>
        false
        -- </tkt-80e031a00f.104>
    })

test:do_execsql_test(
    "tkt-80e031a00f.105",
    [[
        SELECT 1 NOT IN t4
    ]], {
        -- <tkt-80e031a00f.105>
        true
        -- </tkt-80e031a00f.105>
    })

test:do_execsql_test(
    "tkt-80e031a00f.106",
    [[
        SELECT 1 IN t5
    ]], {
        -- <tkt-80e031a00f.106>
        false
        -- </tkt-80e031a00f.106>
    })

test:do_execsql_test(
    "tkt-80e031a00f.107",
    [[
        SELECT 1 NOT IN t5
    ]], {
        -- <tkt-80e031a00f.107>
        true
        -- </tkt-80e031a00f.107>
    })

test:do_execsql_test(
    "tkt-80e031a00f.108",
    [[
        SELECT 1 IN t6
    ]], {
        -- <tkt-80e031a00f.108>
        false
        -- </tkt-80e031a00f.108>
    })

test:do_execsql_test(
    "tkt-80e031a00f.109",
    [[
        SELECT 1 NOT IN t6
    ]], {
        -- <tkt-80e031a00f.109>
        true
        -- </tkt-80e031a00f.109>
    })

test:do_execsql_test(
    "tkt-80e031a00f.110",
    [[
        SELECT 'a' IN t7
    ]], {
        -- <tkt-80e031a00f.110>
        false
        -- </tkt-80e031a00f.110>
    })

test:do_execsql_test(
    "tkt-80e031a00f.111",
    [[
        SELECT 'a' NOT IN t7
    ]], {
        -- <tkt-80e031a00f.111>
        true
        -- </tkt-80e031a00f.111>
    })

test:do_execsql_test(
    "tkt-80e031a00f.112",
    [[
        SELECT 'a' IN t8
    ]], {
        -- <tkt-80e031a00f.112>
        false
        -- </tkt-80e031a00f.112>
    })

test:do_execsql_test(
    "tkt-80e031a00f.113",
    [[
        SELECT 'a' NOT IN t8
    ]], {
        -- <tkt-80e031a00f.113>
        true
        -- </tkt-80e031a00f.113>
    })

--
-- Row 2 is tested by cases 1-32 above.
-- Row 3:
test:do_execsql_test(
    "tkt-80e031a00f.300",
    [[
        SELECT 2 IN (2,3,4,null)
    ]], {
        -- <tkt-80e031a00f.300>
        true
        -- </tkt-80e031a00f.300>
    })

test:do_execsql_test(
    "tkt-80e031a00f.301",
    [[
        SELECT 3 NOT IN (2,3,4,null)
    ]], {
        -- <tkt-80e031a00f.301>
        false
        -- </tkt-80e031a00f.301>
    })

test:do_execsql_test(
    "tkt-80e031a00f.302",
    [[
        SELECT 4 IN (2,3,4)
    ]], {
        -- <tkt-80e031a00f.302>
        true
        -- </tkt-80e031a00f.302>
    })

test:do_execsql_test(
    "tkt-80e031a00f.303",
    [[
        SELECT 2 NOT IN (2,3,4)
    ]], {
        -- <tkt-80e031a00f.303>
        false
        -- </tkt-80e031a00f.303>
    })

test:do_execsql_test(
    "tkt-80e031a00f.304",
    [[
        SELECT 'b' IN ('b','c','d')
    ]], {
        -- <tkt-80e031a00f.304>
        true
        -- </tkt-80e031a00f.304>
    })

test:do_execsql_test(
    "tkt-80e031a00f.305",
    [[
        SELECT 'c' NOT IN ('b','c','d')
    ]], {
        -- <tkt-80e031a00f.305>
        false
        -- </tkt-80e031a00f.305>
    })

test:do_execsql_test(
    "tkt-80e031a00f.306",
    [[
        SELECT 'd' IN ('b','c',null,'d')
    ]], {
        -- <tkt-80e031a00f.306>
        true
        -- </tkt-80e031a00f.306>
    })

test:do_execsql_test(
    "tkt-80e031a00f.307",
    [[
        SELECT 'b' NOT IN (null,'b','c','d')
    ]], {
        -- <tkt-80e031a00f.307>
        false
        -- </tkt-80e031a00f.307>
    })

test:do_execsql_test(
    "tkt-80e031a00f.308",
    [[
        SELECT 2 IN t4
    ]], {
        -- <tkt-80e031a00f.308>
        true
        -- </tkt-80e031a00f.308>
    })

test:do_execsql_test(
    "tkt-80e031a00f.309",
    [[
        SELECT 3 NOT IN t4
    ]], {
        -- <tkt-80e031a00f.309>
        false
        -- </tkt-80e031a00f.309>
    })

test:do_execsql_test(
    "tkt-80e031a00f.310",
    [[
        SELECT 4 IN (select a from t4n)
    ]], {
        -- <tkt-80e031a00f.310>
        true
        -- </tkt-80e031a00f.310>
    })

test:do_execsql_test(
    "tkt-80e031a00f.311",
    [[
        SELECT 2 NOT IN (select a from t4n)
    ]], {
        -- <tkt-80e031a00f.311>
        false
        -- </tkt-80e031a00f.311>
    })

test:do_execsql_test(
    "tkt-80e031a00f.312",
    [[
        SELECT 2 IN t5
    ]], {
        -- <tkt-80e031a00f.312>
        true
        -- </tkt-80e031a00f.312>
    })

test:do_execsql_test(
    "tkt-80e031a00f.313",
    [[
        SELECT 3 NOT IN t5
    ]], {
        -- <tkt-80e031a00f.313>
        false
        -- </tkt-80e031a00f.313>
    })

test:do_execsql_test(
    "tkt-80e031a00f.314",
    [[
        SELECT 2 IN t6
    ]], {
        -- <tkt-80e031a00f.314>
        true
        -- </tkt-80e031a00f.314>
    })

test:do_execsql_test(
    "tkt-80e031a00f.315",
    [[
        SELECT 3 NOT IN t6
    ]], {
        -- <tkt-80e031a00f.315>
        false
        -- </tkt-80e031a00f.315>
    })

test:do_execsql_test(
    "tkt-80e031a00f.316",
    [[
        SELECT 4 IN (select b from t6n)
    ]], {
        -- <tkt-80e031a00f.316>
        true
        -- </tkt-80e031a00f.316>
    })

test:do_execsql_test(
    "tkt-80e031a00f.317",
    [[
        SELECT 2 NOT IN (select b from t6n)
    ]], {
        -- <tkt-80e031a00f.317>
        false
        -- </tkt-80e031a00f.317>
    })

test:do_execsql_test(
    "tkt-80e031a00f.318",
    [[
        SELECT 'b' IN t7
    ]], {
        -- <tkt-80e031a00f.318>
        true
        -- </tkt-80e031a00f.318>
    })

test:do_execsql_test(
    "tkt-80e031a00f.319",
    [[
        SELECT 'c' NOT IN t7
    ]], {
        -- <tkt-80e031a00f.319>
        false
        -- </tkt-80e031a00f.319>
    })

test:do_execsql_test(
    "tkt-80e031a00f.320",
    [[
        SELECT 'c' IN (select a from t7n)
    ]], {
        -- <tkt-80e031a00f.320>
        true
        -- </tkt-80e031a00f.320>
    })

test:do_execsql_test(
    "tkt-80e031a00f.321",
    [[
        SELECT 'd' NOT IN (select a from t7n)
    ]], {
        -- <tkt-80e031a00f.321>
        false
        -- </tkt-80e031a00f.321>
    })

test:do_execsql_test(
    "tkt-80e031a00f.322",
    [[
        SELECT 'b' IN t8
    ]], {
        -- <tkt-80e031a00f.322>
        true
        -- </tkt-80e031a00f.322>
    })

test:do_execsql_test(
    "tkt-80e031a00f.323",
    [[
        SELECT 'c' NOT IN t8
    ]], {
        -- <tkt-80e031a00f.323>
        false
        -- </tkt-80e031a00f.323>
    })

test:do_execsql_test(
    "tkt-80e031a00f.324",
    [[
        SELECT 'c' IN (select c from t8n)
    ]], {
        -- <tkt-80e031a00f.324>
        true
        -- </tkt-80e031a00f.324>
    })

test:do_execsql_test(
    "tkt-80e031a00f.325",
    [[
        SELECT 'd' NOT IN (select c from t8n)
    ]], {
        -- <tkt-80e031a00f.325>
        false
        -- </tkt-80e031a00f.325>
    })

test:do_execsql_test(
    "tkt-80e031a00f.326",
    [[
        SELECT 'a' IN (NULL,'a')
    ]], {
        -- <tkt-80e031a00f.326>
        true
        -- </tkt-80e031a00f.326>
    })

test:do_execsql_test(
    "tkt-80e031a00f.327",
    [[
        SELECT 'a' IN (NULL,'b')
    ]], {
        -- <tkt-80e031a00f.327>
        ""
        -- </tkt-80e031a00f.327>
    })

test:do_execsql_test(
    "tkt-80e031a00f.328",
    [[
        SELECT 'a' NOT IN (NULL,'a')
    ]], {
        -- <tkt-80e031a00f.328>
        false
        -- </tkt-80e031a00f.328>
    })

test:do_execsql_test(
    "tkt-80e031a00f.329",
    [[
        SELECT 'a' NOT IN (NULL,'b')
    ]], {
        -- <tkt-80e031a00f.329>
        ""
        -- </tkt-80e031a00f.329>
    })

--
-- Row 4:
test:do_execsql_test(
    "tkt-80e031a00f.400",
    [[
        SELECT 1 IN (2,3,4,null)
    ]], {
        -- <tkt-80e031a00f.400>
        ""
        -- </tkt-80e031a00f.400>
    })

test:do_execsql_test(
    "tkt-80e031a00f.401",
    [[
        SELECT 1 NOT IN (2,3,4,null)
    ]], {
        -- <tkt-80e031a00f.401>
        ""
        -- </tkt-80e031a00f.401>
    })

test:do_execsql_test(
    "tkt-80e031a00f.402",
    [[
        SELECT 'a' IN ('b','c',null,'d')
    ]], {
        -- <tkt-80e031a00f.402>
        ""
        -- </tkt-80e031a00f.402>
    })

test:do_execsql_test(
    "tkt-80e031a00f.403",
    [[
        SELECT 'a' NOT IN (null,'b','c','d')
    ]], {
        -- <tkt-80e031a00f.403>
        ""
        -- </tkt-80e031a00f.403>
    })

test:do_execsql_test(
    "tkt-80e031a00f.404",
    [[
        SELECT 1 IN (select a from t4n)
    ]], {
        -- <tkt-80e031a00f.404>
        ""
        -- </tkt-80e031a00f.404>
    })

test:do_execsql_test(
    "tkt-80e031a00f.405",
    [[
        SELECT 5 NOT IN (select a from t4n)
    ]], {
        -- <tkt-80e031a00f.405>
        ""
        -- </tkt-80e031a00f.405>
    })

test:do_execsql_test(
    "tkt-80e031a00f.406",
    [[
        SELECT 6 IN (select c from t6n)
    ]], {
        -- <tkt-80e031a00f.406>
        ""
        -- </tkt-80e031a00f.406>
    })

test:do_execsql_test(
    "tkt-80e031a00f.407",
    [[
        SELECT 7 NOT IN (select c from t6n)
    ]], {
        -- <tkt-80e031a00f.407>
        ""
        -- </tkt-80e031a00f.407>
    })

test:do_execsql_test(
    "tkt-80e031a00f.408",
    [[
        SELECT 'a' IN (select a from t7n)
    ]], {
        -- <tkt-80e031a00f.408>
        ""
        -- </tkt-80e031a00f.408>
    })

test:do_execsql_test(
    "tkt-80e031a00f.409",
    [[
        SELECT 'e' NOT IN (select a from t7n)
    ]], {
        -- <tkt-80e031a00f.409>
        ""
        -- </tkt-80e031a00f.409>
    })

test:do_execsql_test(
    "tkt-80e031a00f.410",
    [[
        SELECT 'f' IN (select c from t8n)
    ]], {
        -- <tkt-80e031a00f.410>
        ""
        -- </tkt-80e031a00f.410>
    })

test:do_execsql_test(
    "tkt-80e031a00f.411",
    [[
        SELECT 'g' NOT IN (select c from t8n)
    ]], {
        -- <tkt-80e031a00f.411>
        ""
        -- </tkt-80e031a00f.411>
    })

--
-- Row 5:
test:do_execsql_test(
    "tkt-80e031a00f.500",
    [[
        SELECT null IN (2,3,4,null)
    ]], {
        -- <tkt-80e031a00f.500>
        ""
        -- </tkt-80e031a00f.500>
    })

test:do_execsql_test(
    "tkt-80e031a00f.501",
    [[
        SELECT null NOT IN (2,3,4,null)
    ]], {
        -- <tkt-80e031a00f.501>
        ""
        -- </tkt-80e031a00f.501>
    })

test:do_execsql_test(
    "tkt-80e031a00f.502",
    [[
        SELECT null IN (2,3,4)
    ]], {
        -- <tkt-80e031a00f.502>
        ""
        -- </tkt-80e031a00f.502>
    })

test:do_execsql_test(
    "tkt-80e031a00f.503",
    [[
        SELECT null NOT IN (2,3,4)
    ]], {
        -- <tkt-80e031a00f.503>
        ""
        -- </tkt-80e031a00f.503>
    })

test:do_execsql_test(
    "tkt-80e031a00f.504",
    [[
        SELECT null IN ('b','c','d')
    ]], {
        -- <tkt-80e031a00f.504>
        ""
        -- </tkt-80e031a00f.504>
    })

test:do_execsql_test(
    "tkt-80e031a00f.505",
    [[
        SELECT null NOT IN ('b','c','d')
    ]], {
        -- <tkt-80e031a00f.505>
        ""
        -- </tkt-80e031a00f.505>
    })

test:do_execsql_test(
    "tkt-80e031a00f.506",
    [[
        SELECT null IN ('b','c',null,'d')
    ]], {
        -- <tkt-80e031a00f.506>
        ""
        -- </tkt-80e031a00f.506>
    })

test:do_execsql_test(
    "tkt-80e031a00f.507",
    [[
        SELECT null NOT IN (null,'b','c','d')
    ]], {
        -- <tkt-80e031a00f.507>
        ""
        -- </tkt-80e031a00f.507>
    })

test:do_execsql_test(
    "tkt-80e031a00f.508",
    [[
        SELECT null IN t4
    ]], {
        -- <tkt-80e031a00f.508>
        ""
        -- </tkt-80e031a00f.508>
    })

test:do_execsql_test(
    "tkt-80e031a00f.509",
    [[
        SELECT null NOT IN t4
    ]], {
        -- <tkt-80e031a00f.509>
        ""
        -- </tkt-80e031a00f.509>
    })

test:do_execsql_test(
    "tkt-80e031a00f.510",
    [[
        SELECT null IN (select a from t4n)
    ]], {
        -- <tkt-80e031a00f.510>
        ""
        -- </tkt-80e031a00f.510>
    })

test:do_execsql_test(
    "tkt-80e031a00f.511",
    [[
        SELECT null NOT IN (select a from t4n)
    ]], {
        -- <tkt-80e031a00f.511>
        ""
        -- </tkt-80e031a00f.511>
    })

test:do_execsql_test(
    "tkt-80e031a00f.512",
    [[
        SELECT null IN t5
    ]], {
        -- <tkt-80e031a00f.512>
        ""
        -- </tkt-80e031a00f.512>
    })

test:do_execsql_test(
    "tkt-80e031a00f.513",
    [[
        SELECT null NOT IN t5
    ]], {
        -- <tkt-80e031a00f.513>
        ""
        -- </tkt-80e031a00f.513>
    })

test:do_execsql_test(
    "tkt-80e031a00f.514",
    [[
        SELECT null IN t6
    ]], {
        -- <tkt-80e031a00f.514>
        ""
        -- </tkt-80e031a00f.514>
    })

test:do_execsql_test(
    "tkt-80e031a00f.515",
    [[
        SELECT null NOT IN t6
    ]], {
        -- <tkt-80e031a00f.515>
        ""
        -- </tkt-80e031a00f.515>
    })

test:do_execsql_test(
    "tkt-80e031a00f.516",
    [[
        SELECT null IN (select c from t6n)
    ]], {
        -- <tkt-80e031a00f.516>
        ""
        -- </tkt-80e031a00f.516>
    })

test:do_execsql_test(
    "tkt-80e031a00f.517",
    [[
        SELECT null NOT IN (select c from t6n)
    ]], {
        -- <tkt-80e031a00f.517>
        ""
        -- </tkt-80e031a00f.517>
    })

test:do_execsql_test(
    "tkt-80e031a00f.518",
    [[
        SELECT null IN t7
    ]], {
        -- <tkt-80e031a00f.518>
        ""
        -- </tkt-80e031a00f.518>
    })

test:do_execsql_test(
    "tkt-80e031a00f.519",
    [[
        SELECT null NOT IN t7
    ]], {
        -- <tkt-80e031a00f.519>
        ""
        -- </tkt-80e031a00f.519>
    })

test:do_execsql_test(
    "tkt-80e031a00f.520",
    [[
        SELECT null IN (select a from t7n)
    ]], {
        -- <tkt-80e031a00f.520>
        ""
        -- </tkt-80e031a00f.520>
    })

test:do_execsql_test(
    "tkt-80e031a00f.521",
    [[
        SELECT null NOT IN (select a from t7n)
    ]], {
        -- <tkt-80e031a00f.521>
        ""
        -- </tkt-80e031a00f.521>
    })

test:do_execsql_test(
    "tkt-80e031a00f.522",
    [[
        SELECT null IN t8
    ]], {
        -- <tkt-80e031a00f.522>
        ""
        -- </tkt-80e031a00f.522>
    })

test:do_execsql_test(
    "tkt-80e031a00f.523",
    [[
        SELECT null NOT IN t8
    ]], {
        -- <tkt-80e031a00f.523>
        ""
        -- </tkt-80e031a00f.523>
    })

test:do_execsql_test(
    "tkt-80e031a00f.524",
    [[
        SELECT null IN (select c from t8n)
    ]], {
        -- <tkt-80e031a00f.524>
        ""
        -- </tkt-80e031a00f.524>
    })

test:do_execsql_test(
    "tkt-80e031a00f.525",
    [[
        SELECT null NOT IN (select c from t8n)
    ]], {
        -- <tkt-80e031a00f.525>
        ""
        -- </tkt-80e031a00f.525>
    })

test:finish_test()

