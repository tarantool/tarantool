#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(6)

--!./tcltestrunner.lua
-- 2005 September 11
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
-- focus of this script is the DISTINCT modifier on aggregate functions.
--
-- $Id: distinctagg.test,v 1.3 2009/02/09 13:19:28 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "distinctagg-1.1",
    [[
        CREATE TABLE t1(a INT,b INT,c INT primary key);
        INSERT INTO t1 VALUES(1,2,3);
        INSERT INTO t1 VALUES(1,3,4);
        INSERT INTO t1 VALUES(1,3,5);
        SELECT count(distinct a),
               count(distinct b),
               count(distinct c),
               count(all a) FROM t1;
    ]], {
        -- <distinctagg-1.1>
        1, 2, 3, 3
        -- </distinctagg-1.1>
    })

test:do_execsql_test(
    "distinctagg-1.2",
    [[
        SELECT b, count(distinct c) FROM t1 GROUP BY b ORDER BY b
    ]], {
        -- <distinctagg-1.2>
        2, 1, 3, 2
        -- </distinctagg-1.2>
    })

test:do_execsql_test(
    "distinctagg-1.3",
    [[
        INSERT INTO t1 SELECT a+1, b+3, c+5 FROM t1;
        INSERT INTO t1 SELECT a+2, b+6, c+10 FROM t1;
        INSERT INTO t1 SELECT a+4, b+12, c+20 FROM t1;
        SELECT count(*), count(distinct a), count(distinct b) FROM t1
    ]], {
        -- <distinctagg-1.3>
        24, 8, 16
        -- </distinctagg-1.3>
    })

test:do_execsql_test(
    "distinctagg-1.4",
    [[
        SELECT a, count(distinct c) FROM t1 GROUP BY a ORDER BY a
    ]], {
        -- <distinctagg-1.4>
        1, 3, 2, 3, 3, 3, 4, 3, 5, 3, 6, 3, 7, 3, 8, 3
        -- </distinctagg-1.4>
    })

test:do_catchsql_test(
    "distinctagg-2.1",
    [[
        SELECT count(distinct) FROM t1;
    ]], {
        -- <distinctagg-2.1>
        1, "DISTINCT aggregates must have exactly one argument"
        -- </distinctagg-2.1>
    })

test:do_catchsql_test(
    "distinctagg-2.2",
    [[
        SELECT group_concat(distinct a,b) FROM t1;
    ]], {
        -- <distinctagg-2.2>
        1, "DISTINCT aggregates must have exactly one argument"
        -- </distinctagg-2.2>
    })

test:finish_test()

