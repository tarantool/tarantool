#!/usr/bin/env tarantool
local test = require("sqltester")
--test:plan(1)
test:plan(0)

--!./tcltestrunner.lua
-- 2009 April 2
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
-- Ticket #3791:  A segfault when inserting into a table that contains
-- an arbitrary expression as its default value.
--
-- $Id: tkt3791.test,v 1.1 2009/04/08 12:21:31 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- MUST_WORK_TEST
-- Disabled until #3694 is resolved.
--
if (0 > 0) then
test:do_test(
    "tkt3791-1.1",
    function()
        return test:execsql [[
            CREATE TABLE t1(x  INT primary key, y TEXT DEFAULT(datetime('now')));
            INSERT INTO t1(x) VALUES(1);
            SELECT x, length(y) FROM t1;
        ]]
    end, {
        -- <tkt3791-1.1>
        1, 19
        -- </tkt3791-1.1>
    })
end

test:finish_test()

