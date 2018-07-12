#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(1)

--!./tcltestrunner.lua
-- 2005 November 16
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.
--
-- This file implements tests to verify that ticket #1514 is
-- fixed.  
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_catchsql_test(
    "tkt1514-1.1",
    [[
        CREATE TABLE t1(a INT primary key,b INT);
        SELECT a FROM t1 WHERE max(b)<10 GROUP BY a;
    ]], {
        -- <tkt1514-1.1>
        1, "misuse of aggregate: MAX()"
        -- </tkt1514-1.1>
    })

test:finish_test()

