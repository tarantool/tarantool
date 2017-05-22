#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(1)

--!./tcltestrunner.lua
-- 2015-01-05
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
-- This file verifies that INSERT operations with a very large number of
-- VALUE terms works and does not hit the SQLITE_LIMIT_COMPOUND_SELECT limit.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
testprefix = "selectG"
-- Do an INSERT with a VALUES clause that contains 100,000 entries.  Verify
-- that this insert happens quickly (in less than 10 seconds).  Actually, the
-- insert will normally happen in less than 0.5 seconds on a workstation, but
-- we allow plenty of overhead for slower machines.  The speed test checks
-- for an O(N*N) inefficiency that was once in the code and that would make
-- the insert run for over a minute.
--
test:do_test(
    100,
    function()
        local sql_arr = {[[CREATE TABLE t1(x primary key);
            INSERT INTO t1(x) VALUES]]}
        local i
        for i = 1, 100000-1, 1 do
            table.insert(sql_arr, "("..i.."),")
        end
        table.insert(sql_arr, "(100000);")
        local sql = table.concat(sql_arr, "")
        local start_time = os.time()
        test:execsql(sql)
        local end_time = os.time()
        return test:execsql( "SELECT count(x), sum(x), avg(x), "..(end_time - start_time).."<10 FROM t1;")
    end, {
        -- <100>
        100000, 5000050000, 50000.5, 1
        -- </100>
    })

test:finish_test()

