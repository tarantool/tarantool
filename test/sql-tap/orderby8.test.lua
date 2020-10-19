#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(203)

--!./tcltestrunner.lua
-- 2015-01-19
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
-- focus of this file is testing ORDER BY and LIMIT on tables with
-- many columns.
--
-- These tests verify that ticket [f97c4637102a3ae72b7911167e1d4da12ce60722]
-- from 2015-01-19 has been fixed.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "orderby8"

local rs = ""
test:do_test(
    1.0,
    function()
        test:execsql [[
            CREATE TABLE t1(x INT primary key);
            INSERT INTO t1(x) VALUES(1),(5),(9),(7),(3),(2),(4),(6),(8);
        ]]
        rs = "x"
        return "x"
    end,
        -- <1.0>
        "x"
        -- </1.0>
    )

for i=1,200 do
    rs = rs..", x+"..i
    test:do_execsql_test(
        "1."..i,
        "SELECT x FROM (SELECT "..rs.." FROM t1 ORDER BY x LIMIT 100)",
        {
            1, 2, 3, 4, 5, 6, 7, 8, 9
    })
end

-- Tests to check that ORDER BY + LIMIT + (ASC + DESC, different
-- sorting orders) are forbidden. Such combination is forbidden
-- because currently it returns data in wrong sorting order.
-- It will be fixed when multi-directional would be introduced:
-- https://github.com/tarantool/tarantool/issues/3309

test:do_catchsql_test(
    "1.201",
    [[
        CREATE TABLE t2 (id INT PRIMARY KEY, a INT, b INT);
        INSERT INTO t2 VALUES (1, 2, 1), (2, -3, 5), (3, 2, -3), (4, 2, 12);
        SELECT * FROM t2 ORDER BY a ASC, b DESC LIMIT 5;
    ]],
    {1, "ORDER BY with LIMIT does not support different sorting orders"}
)

test:do_catchsql_test(
    "1.202",
    [[
        SELECT * FROM t2 ORDER BY a, b DESC LIMIT 5;
    ]],
    {1, "ORDER BY with LIMIT does not support different sorting orders"}

)

test:finish_test()
