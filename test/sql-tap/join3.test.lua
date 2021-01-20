#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(129)

--!./tcltestrunner.lua
-- 2002 May 24
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.
--
-- This file implements tests for joins, including outer joins, where
-- there are a large number of tables involved in the join.
--
-- $Id: join3.test,v 1.4 2005/01/19 23:24:51 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- An unrestricted join
--
-- X(25, "X!cmd", [=[["catch","unset ::result"]]=])

local bitmask_size = 64

local result = {}
--for _ in X(0, "X!for", [=[["set N 1","$N<=$bitmask_size","incr N"]]=]) do
for N=1, bitmask_size do
    table.insert(result,N) -- or result
    test:do_test(
        "join3-1."..N,
        function()
            test:execsql("CREATE TABLE t"..N.."(x INT primary key);")
            test:execsql(string.format("INSERT INTO t%s VALUES(%s)", N, N))
            local sql = "SELECT * FROM t1"
            -- for _ in X(0, "X!for", [=[["set i 2","$i<=$N","incr i"]]=]) do
            for i=2,N do
                sql = sql .. ", t"..i..""
            end
            return test:execsql(sql)
        end,
        result
        )

end
-- Joins with a comparison
--
local result = {}
--for _ in X(0, "X!for", [=[["set N 1","$N<=$bitmask_size","incr N"]]=]) do
for N=1, bitmask_size do
    table.insert(result,N)
    test:do_test(
        "join3-2."..N,
        function()
            local sql = "SELECT * FROM t1"
            --for _ in X(0, "X!for", [=[["set i 2","$i<=$N","incr i"]]=]) do
            for i=2, N do
                sql = sql .. ", t"..i..""
            end
            local sep = "WHERE"
            --for _ in X(0, "X!for", [=[["set i 1","$i<$N","incr i"]]=]) do
            for i=1, N - 1 do
                sql = sql .. string.format(" %s t%s.x==t%s.x+1", sep, (i + 1), i)
                sep = "AND"
            end
            return test:execsql(sql)
        end,
        result
        )

end
-- Error of too many tables in the join
--
test:do_test(
    "join3-3.1",
    function()
        local sql = "SELECT * FROM t1 AS t0, t1"
        for i=2, bitmask_size do
            sql = sql .. ", t"..i..""
        end
        return test:catchsql(sql)
    end, {
        -- <join3-3.1>
        1, "The number of tables in a join " .. bitmask_size + 1 .. " exceeds the limit (".. bitmask_size ..")"
        -- </join3-3.1>
    })

test:finish_test()
