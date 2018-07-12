#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(2)

--!./tcltestrunner.lua
-- 2008 December 16
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
-- This file is a verification that the bugs identified in ticket
-- #3541 have been fixed.
--
-- $Id: tkt3541.test,v 1.1 2008/12/15 15:27:52 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "tkt3541-1.1",
    function()
        return test:execsql [[
            CREATE TABLE t1(x INT primary key);
            INSERT INTO t1 VALUES(123);
            SELECT CASE ~max(x) WHEN min(x) THEN 1 ELSE max(x) END FROM t1;
        ]]
    end, {
        -- <tkt3541-1.1>
        123
        -- </tkt3541-1.1>
    })

test:do_test(
    "tkt3541-1.2",
    function()
        return test:execsql [[
            SELECT CASE NOT max(x) WHEN min(x) THEN 1 ELSE max(x) END FROM t1;
        ]]
    end, {
        -- <tkt3541-1.2>
        123
        -- </tkt3541-1.2>
    })

test:finish_test()

