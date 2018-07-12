#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(9)

--!./tcltestrunner.lua
-- 2012 Sept 27
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
-- focus of this file is testing that the optimizations that disable
-- ORDER BY clauses when the natural order of a query is correct.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
testprefix = "orderby2"
-- Generate test data for a join.  Verify that the join gets the
-- correct answer.
--
test:do_test(
    1.0,
    function()
        return test:execsql [[
            CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT );
            INSERT INTO t1 VALUES(1,11), (2,22);
            CREATE TABLE t2(d INT , e TEXT , PRIMARY KEY(d,e));
            INSERT INTO t2 VALUES(10, 'ten'), (11,'eleven'), (12,'twelve'),
                                 (11, 'oneteen');
        ]]
    end, {
        -- <1.0>
        
        -- </1.0>
    })

test:do_test(
    "1.1a",
    function()
        return test:execsql [[
            SELECT e FROM t1, t2 WHERE a=1 AND d=b ORDER BY d, e;
        ]]
    end, {
        -- <1.1a>
        "eleven", "oneteen"
        -- </1.1a>
    })

-- Tarantool: no-rowid changes query plan to use ORDER BY.
-- Verified w/ vanilla SQLite. Comment the case.
--do_test 1.1b {
--  db eval {
--    EXPLAIN QUERY PLAN
--    SELECT e FROM t1, t2 WHERE a=1 AND d=b ORDER BY d, e;
--  }
--} {~/ORDER BY/}
test:do_test(
    "1.2a",
    function()
        return test:execsql [[
            SELECT e FROM t1, t2 WHERE a=1 AND d=b ORDER BY e;
        ]]
    end, {
        -- <1.2a>
        "eleven", "oneteen"
        -- </1.2a>
    })

-- since extra columns deleted from pk(#2289), "order by" is optimized awy
-- as e is getting from ordered index by sequential scan by a single "d" value
-- (howewer it was possible before because t1 was unique by a)
test:do_test(
    "1.2b",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT e FROM t1, t2 WHERE a=1 AND d=b ORDER BY e;
        ]]
    end, {
        -- <1.2b>
        --"/ORDER BY/"
        "~/ORDER BY/"
        -- </1.2b>
    })

test:do_test(
    "1.3a",
    function()
        return test:execsql [[
            SELECT e, b FROM t1, t2 WHERE a=1 ORDER BY d, e;
        ]]
    end, {
        -- <1.3a>
        "ten", 11, "eleven", 11, "oneteen", 11, "twelve", 11
        -- </1.3a>
    })

test:do_test(
    "1.3b",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT e, b FROM t1, t2 WHERE a=1 ORDER BY d, e;
        ]]
    end, {
        -- <1.3b>
        "~/ORDER BY/"
        -- </1.3b>
    })

-- The following tests derived from TH3 test module cov1/where34.test
--
test:do_test(
    2.0,
    function()
        return test:execsql [[
            CREATE TABLE t31(a INT ,b INT , PRIMARY KEY(a,b));
            CREATE TABLE t32(c INT ,d INT , PRIMARY KEY(c,d));
            CREATE TABLE t33(e INT ,f INT , PRIMARY KEY(e,f));
            CREATE TABLE t34(g INT ,h INT , PRIMARY KEY(g,h));

            INSERT INTO t31 VALUES(1,4), (2,3), (1,3);
            INSERT INTO t32 VALUES(4,5), (3,6), (3,7), (4,8);
            INSERT INTO t33 VALUES(5,9), (7,10), (6,11), (8,12), (8,13), (7,14);
            INSERT INTO t34 VALUES(11,20), (10,21), (12,22), (9,23), (13,24),
                                  (14,25), (12,26);
            SELECT CAST(a AS TEXT)||','||CAST(c AS TEXT)||','||CAST(e AS TEXT)||','||CAST(g as TEXT) FROM t31, t32, t33, t34
             WHERE c=b AND e=d AND g=f
             ORDER BY a ASC, c ASC, e DESC, g ASC;
        ]]
    end, {
        -- <2.0>
        "1,3,7,10", "1,3,7,14", "1,3,6,11", "1,4,8,12", "1,4,8,12", "1,4,8,13", "1,4,5,9", "2,3,7,10", "2,3,7,14", "2,3,6,11"
        -- </2.0>
    })

test:do_test(
    2.1,
    function()
        return test:execsql [[
            SELECT CAST(a AS TEXT)||','||CAST(c AS TEXT)||','||CAST(e AS TEXT)||','||CAST(g AS TEXT) FROM t31, t32, t33, t34
             WHERE c=b AND e=d AND g=f
             ORDER BY +a ASC, +c ASC, +e DESC, +g ASC;
        ]]
    end, {
        -- <2.1>
        "1,3,7,10", "1,3,7,14", "1,3,6,11", "1,4,8,12", "1,4,8,12", "1,4,8,13", "1,4,5,9", "2,3,7,10", "2,3,7,14", "2,3,6,11"
        -- </2.1>
    })

test:do_test(
    2.2,
    function()
        return test:execsql [[
            SELECT CAST(a AS TEXT)||','||CAST(c AS TEXT)||','||CAST(e AS TEXT)||','||CAST(g AS TEXT) FROM t31, t32, t33, t34
             WHERE c=b AND e=d AND g=f
             ORDER BY a ASC, c ASC, e ASC, g ASC;
        ]]
    end, {
        -- <2.2>
        "1,3,6,11", "1,3,7,10", "1,3,7,14", "1,4,5,9", "1,4,8,12", "1,4,8,12", "1,4,8,13", "2,3,6,11", "2,3,7,10", "2,3,7,14"
        -- </2.2>
    })


-- Tarantool: we're unable to switch off the optimization
--  Comment test so far
-- test:do_test(
--     2.3,
--     function()
--         X(109, "X!cmd", [=[["optimization_control","db","cover-idx-scan","off"]]=])
--         db("cache", "flush")
--         return test:execsql [[
--             SELECT a||','||c||','||e||','||g FROM t31, t32, t33, t34
--              WHERE c=b AND e=d AND g=f
--              ORDER BY a ASC, c ASC, e ASC, g ASC;
--         ]]
--     end, {
--         -- <2.3>
--         "1,3,6,11", "1,3,7,10", "1,3,7,14", "1,4,5,9", "1,4,8,12", "1,4,8,12", "1,4,8,13", "2,3,6,11", "2,3,7,10", "2,3,7,14"
--         -- </2.3>
--     })

-- X(116, "X!cmd", [=[["optimization_control","db","all","on"]]=])
-- db("cache", "flush")
test:finish_test()

