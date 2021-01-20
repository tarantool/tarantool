#!/usr/bin/env tarantool
local test = require("sqltester")
--test:plan(2)
test:plan(0)

--!./tcltestrunner.lua
-- 2011 June 21
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file contains tests for sql. Specifically, it tests that sql
-- does not crash and an error is returned if localhost() fails. This
-- is the problem reported by ticket bd484a090c.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

-- Disabled until #3694 is resolved.
--
local is_gh_3694_closed = false
if is_gh_3694_closed then
test:do_test(
    1.1,
    function()
        return test:catchsql(" SELECT datetime('now', 'localtime') ")[1]
    end, 0)

test:do_test(
    1.2,
    function()
        return test:catchsql(" SELECT datetime('now', 'utc') ")[1]
    end, 0)
end -- if false
-- TBI to be implemented feature
--sql_test_control("sql_TESTCTRL_LOCALTIME_FAULT", 1)
--test:do_catchsql_test(
--    2.1,
--    [[
--        SELECT datetime('now', 'localtime')
--    ]], {
--        -- <2.1>
--        1, "local time unavailable"
--        -- </2.1>
--    })
--
--test:do_catchsql_test(
--    2.2,
--    [[
--        SELECT datetime('now', 'utc')
--    ]], {
--        -- <2.2>
--        1, "local time unavailable"
--        -- </2.2>
--    })

--sql_test_control("sql_TESTCTRL_LOCALTIME_FAULT", 0)
test:finish_test()

