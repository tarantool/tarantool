#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(22)

--!./tcltestrunner.lua
-- 2014-04-21
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May y ou do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
---------------------------------------------------------------------------
--
-- Test that ticket [b75a9ca6b0] has been fixed.
--
-- Ticket [b75a9ca6b0] concerns queries that have both a GROUP BY
-- and an ORDER BY.  This code verifies that SQLite is able to
-- optimize out the ORDER BY in some circumstances, but retains the
-- ORDER BY when necessary.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
testprefix = "tkt-b75a9ca6b0"
test:do_execsql_test(
    1,
    [[
        CREATE TABLE t1 (id INT primary key, x INT, y INT);
        INSERT INTO t1 VALUES (1, 1, 3);
        INSERT INTO t1 VALUES (2, 2, 2);
        INSERT INTO t1 VALUES (3, 3, 1);
    ]])

test:do_execsql_test(
    1.1,
    [[
        CREATE INDEX i1 ON t1(x, y);
    ]])

local idxscan = {0, 0, 0, "SCAN TABLE T1 USING COVERING INDEX I1"}
local tblscan = {0, 0, 0, "SCAN TABLE T1"}
local grpsort = {0, 0, 0, "USE TEMP B-TREE FOR GROUP BY"}
local sort = {0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"}
local eqps = {
    {"SELECT x,y FROM t1 GROUP BY x, y ORDER BY x,y", {1, 3,  2, 2,  3, 1}, {idxscan}},
    {"SELECT x,y FROM t1 GROUP BY x, y ORDER BY x", {1, 3, 2, 2, 3, 1}, {idxscan, sort}},
    {"SELECT x,y FROM t1 GROUP BY y, x ORDER BY y, x", {3, 1, 2, 2, 1, 3}, {idxscan, sort}},
    {"SELECT x,y FROM t1 GROUP BY x ORDER BY x", {1, 3, 2, 2, 3, 1}, {idxscan}},
    -- idxscan->tblscan after reorderind indexes list
    -- but it does not matter
    {"SELECT x,y FROM t1 GROUP BY y ORDER BY y", {3, 1, 2, 2, 1, 3}, {tblscan, grpsort}},
    -- idxscan->tblscan after reorderind indexes list
    -- but it does not matter (because it does full scan)
    {"SELECT x,y FROM t1 GROUP BY y ORDER BY x", {1, 3, 2, 2, 3, 1}, {tblscan, grpsort, sort}},
    {"SELECT x,y FROM t1 GROUP BY x, y ORDER BY x, y DESC", {1, 3, 2, 2, 3, 1}, {idxscan, sort}},
    {"SELECT x,y FROM t1 GROUP BY x, y ORDER BY x DESC, y DESC", {3, 1, 2, 2, 1, 3}, {idxscan, sort}},
    {"SELECT x,y FROM t1 GROUP BY x, y ORDER BY x ASC, y ASC", {1, 3, 2, 2, 3, 1}, {idxscan}},
    {"SELECT x,y FROM t1 GROUP BY x, y ORDER BY x COLLATE \"unicode_ci\", y", {1, 3, 2, 2, 3, 1}, {idxscan, sort}},
}
for tn, val in ipairs(eqps) do
    local q = val[1]
    local res = val[2]
    local eqp = val[3]
    test:do_execsql_test(
        "1."..tn..".1",
        q, res)

    test:do_eqp_test(
        "1."..tn..".2",
        q, eqp)

end
test:finish_test()

