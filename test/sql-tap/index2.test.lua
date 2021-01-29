#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(7)

--!./tcltestrunner.lua
-- 2005-01-11
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.  The
-- focus of this file is testing the CREATE INDEX statement.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Create a table with a large number of columns
--
local COL_NUM = 123 -- was 1000
local ROW_NUM = 100 -- was 100

test:do_test(
    "index2-1.1",
    function()
        local sql_parts = {"CREATE TABLE t1(id  INT primary key"}
        for i = 1, COL_NUM, 1 do
            table.insert(sql_parts, "c"..i .. ' INT')
        end
        local sql = table.concat(sql_parts, ",")..");"
        return test:execsql(sql)
    end, {
        -- <index2-1.1>

        -- </index2-1.1>
    })

test:do_test(
    "index2-1.2",
    function()
        local sql_parts = {"INSERT INTO t1 VALUES(1"}
        for i = 1, COL_NUM, 1 do
            table.insert(sql_parts, ""..i)
        end
        local sql = table.concat(sql_parts, ",")..");"
        return test:execsql(sql)
    end, {
        -- <index2-1.2>

        -- </index2-1.2>
    })

test:do_execsql_test(
    "index2-1.3",
    [[
        SELECT c123 FROM t1
    ]], {
        -- <index2-1.3>
        123
        -- </index2-1.3>
    })

test:do_test(
    "index2-1.4",
    function()
        test:execsql("START TRANSACTION")
        for j = 1, ROW_NUM, 1 do
            local sql_parts = {"INSERT INTO t1 VALUES(".. ((j * 10000) + 1)}
            for i = 2, COL_NUM+1, 1 do
                table.insert(sql_parts, ""..((j * 10000) + i))
            end
            local sql = table.concat(sql_parts, ",")..");"
            test:execsql(sql)
        end
        test:execsql "COMMIT"
        return test:execsql "SELECT count(*) FROM t1"
    end, {
        -- <index2-1.4>
        ROW_NUM + 1
        -- </index2-1.4>
    })

test:do_execsql_test(
    "index2-1.5",
    "SELECT round(sum(c"..COL_NUM..")) FROM t1", {
        -- <index2-1.5>
        10000*ROW_NUM*(ROW_NUM+1)/2 + (COL_NUM+1)*(ROW_NUM+1)-1
        -- </index2-1.5>
    })

-- Create indices with many columns
--
test:do_test(
    "index2-2.1",
    function()
        local sql_parts = {"CREATE INDEX t1i1 ON t1(c1"}
        for i = 2, COL_NUM, 1 do
            table.insert(sql_parts, "c"..i)
        end
        local sql = table.concat(sql_parts, ",")..");"
        return test:execsql(sql)
    end, {
        -- <index2-2.1>

        -- </index2-2.1>
    })

test:do_test(
    "index2-2.2",
    function()
        test:execsql "EXPLAIN SELECT c9 FROM t1 ORDER BY c1, c2, c3, c4, c5"


        return test:execsql "SELECT c9 FROM t1 ORDER BY c1, c2, c3, c4, c5, c6 LIMIT 5"
    end, {
        -- <index2-2.2>
        9, 10010, 20010, 30010, 40010
        -- </index2-2.2>
    })



test:finish_test()
