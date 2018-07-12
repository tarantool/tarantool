#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(1)

--!./tcltestrunner.lua
-- 2006 February 27
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
-- This file implements tests to make sure abusively large triggers
-- (triggers with 100s or 1000s of statements) work.
--
-- $Id: trigger8.test,v 1.2 2008/09/17 16:14:10 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- Set variable $nStatement to the number of statements to include in the
-- body of the trigger. On a workstation with virtually unlimited memory,
-- use 10000. But on symbian, which allows each application at most a 32MB
-- heap, use 1000.
--
nStatement = 10000

test:do_test(
    "trigger8-1.1",
    function()
        test:execsql [[
            CREATE TABLE t1(x INT PRIMARY KEY);
            CREATE TABLE t2(y INT PRIMARY KEY);
        ]]
        sql = string.format([[CREATE TRIGGER r%s AFTER INSERT ON t1 BEGIN
]], nStatement)
        -- for _ in X(0, "X!for", [=[["set i 0","$i<$nStatement","incr i"]]=]) do
        for i=1, nStatement do
            sql = sql .. string.format([[  INSERT INTO t2 VALUES(%s);
]], i)
        end
        sql = sql .. "END;"
        test:execsql(sql)
        return test:execsql [[
            INSERT INTO t1 VALUES(5);
            SELECT COUNT(*) FROM t2;
        ]]
    end, {
        -- <trigger8-1.1>
        nStatement
        -- </trigger8-1.1>
    })

-- MUST_WORK_TEST
test:finish_test()

