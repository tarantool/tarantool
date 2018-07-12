#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(319)

--!./tcltestrunner.lua
-- 2013 March 13
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
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
testprefix = "tkt-4dd95f6943"
test:do_execsql_test(
    1.0,
    [[
        CREATE TABLE t1(id INT primary key, x INT);
        INSERT INTO t1 VALUES (1, 3), (2, 4), (3, 2), (4, 1), (5, 5), (6, 6);
    ]])

local idxs = {
    "CREATE INDEX i1 ON t1(x ASC)",
    "CREATE INDEX i1 ON t1(x DESC)",
}

for tn1, idx in ipairs(idxs) do
    test:do_execsql_test(
        "1."..tn1..".1",
        [[
            DROP INDEX IF EXISTS i1 ON t1;
        ]])

    test:do_execsql_test(
        "1."..tn1..".2",
        idx)

    test:do_execsql_test(
        "1."..tn1..".3",
        [[
            SELECT x FROM t1 WHERE x IN(2, 4, 5) ORDER BY x ASC;
        ]], {
            2, 4, 5
        })

    test:do_execsql_test(
        "1."..tn1..".4",
        [[
            SELECT x FROM t1 WHERE x IN(2, 4, 5) ORDER BY x DESC;
        ]], {
            5, 4, 2
        })

end

test:do_execsql_test(
    2.0,
    [[
        CREATE TABLE t2(id INT primary key, x INT, y INT);
        INSERT INTO t2 VALUES (1, 5, 3), (2, 5, 4), (3, 5, 2), (4, 5, 1), (5, 5, 5), (6, 5, 6);
        INSERT INTO t2 VALUES (7, 1, 3), (8, 1, 4), (9, 1, 2), (10, 1, 1), (11, 1, 5), (12, 1, 6);
        INSERT INTO t2 VALUES (13, 3, 3), (14, 3, 4), (15, 3, 2), (16, 3, 1), (17, 3, 5), (18, 3, 6);
        INSERT INTO t2 VALUES (19, 2, 3), (20, 2, 4), (21, 2, 2), (22, 2, 1), (23, 2, 5), (24, 2, 6);
        INSERT INTO t2 VALUES (25, 4, 3), (26, 4, 4), (27, 4, 2), (28, 4, 1), (29, 4, 5), (30, 4, 6);
        INSERT INTO t2 VALUES (31, 6, 3), (32, 6, 4), (33, 6, 2), (34, 6, 1), (35, 6, 5), (36, 6, 6);

        CREATE TABLE t3(a INT primary key, b INT);
        INSERT INTO t3 VALUES (2, 2), (4, 4), (5, 5);
        CREATE UNIQUE INDEX t3i1 ON t3(a ASC);
        CREATE UNIQUE INDEX t3i2 ON t3(b DESC);
    ]])

local indexes = {
    "CREATE INDEX i1 ON t2(x ASC,  y ASC)",
    "CREATE INDEX i1 ON t2(x ASC,  y DESC)",
    "CREATE INDEX i1 ON t2(x DESC, y ASC)",
    "CREATE INDEX i1 ON t2(x DESC, y DESC)",
    "CREATE INDEX i1 ON t2(y ASC,  x ASC)",
    "CREATE INDEX i1 ON t2(y ASC,  x DESC)",
    "CREATE INDEX i1 ON t2(y DESC, x ASC)",
    "CREATE INDEX i1 ON t2(y DESC, x DESC)",
}

for tn1, idx in ipairs(indexes) do
    test:do_execsql_test(
        "2."..tn1..".1",
        [[
            DROP INDEX IF EXISTS i1 ON t2;
        ]])

    test:do_execsql_test(
        "2."..tn1..".2",
        idx)
    local inexprs = {
        {3, "(2, 4, 5)"},
        {4, "(SELECT a FROM t3)"},
        {5, "(SELECT b FROM t3)"},
    }
    for _, val in ipairs(inexprs) do
        local tn2 = val[1]
        local inexpr = val[2]
        test:do_execsql_test(
            string.format("2.%s.%s.1", tn1, tn2),
            string.format([[
                SELECT x, y FROM t2 WHERE x = 1 AND y IN %s ORDER BY x ASC, y ASC;
            ]], inexpr), {
                1, 2, 1, 4, 1, 5
            })

        test:do_execsql_test(
            string.format("2.%s.%s.2", tn1, tn2),
            string.format([[
                SELECT x, y FROM t2 WHERE x = 2 AND y IN %s ORDER BY x ASC, y DESC;
            ]], inexpr), {
                2, 5, 2, 4, 2, 2
            })

        test:do_execsql_test(
            string.format("2.%s.%s.3", tn1, tn2),
            string.format([[
                SELECT x, y FROM t2 WHERE x = 3 AND y IN %s ORDER BY x DESC, y ASC;
            ]], inexpr), {
                3, 2, 3, 4, 3, 5
            })

        test:do_execsql_test(
            string.format("2.%s.%s.4", tn1, tn2),
            string.format([[
                SELECT x, y FROM t2 WHERE x = 4 AND y IN %s ORDER BY x DESC, y DESC;
            ]], inexpr), {
                4, 5, 4, 4, 4, 2
            })

        test:do_execsql_test(
            string.format("2.%s.%s.5", tn1, tn2),
            string.format([[
                SELECT a, x, y FROM t2, t3 WHERE a = 4 AND x = 1 AND y IN %s 
                ORDER BY a, x ASC, y ASC;
            ]], inexpr), {
                4, 1, 2, 4, 1, 4, 4, 1, 5
            })

        test:do_execsql_test(
            string.format("2.%s.%s.6", tn1, tn2),
            string.format([[
                SELECT a, x, y FROM t2, t3 WHERE a = 2 AND x = 1 AND y IN %s 
                ORDER BY x ASC, y ASC;
            ]], inexpr), {
                2, 1, 2, 2, 1, 4, 2, 1, 5
            })

        test:do_execsql_test(
            string.format("2.%s.%s.7", tn1, tn2),
            string.format([[
                SELECT a, x, y FROM t2, t3 WHERE a = 4 AND x = 1 AND y IN %s 
                ORDER BY a, x ASC, y DESC;
            ]], inexpr), {
                4, 1, 5, 4, 1, 4, 4, 1, 2
            })

        test:do_execsql_test(
            "2."..tn1..".8",
            string.format([[
                SELECT a, x, y FROM t2, t3 WHERE a = 2 AND x = 1 AND y IN %s 
                ORDER BY x ASC, y DESC;
            ]], inexpr), {
                2, 1, 5, 2, 1, 4, 2, 1, 2
            })

        test:do_execsql_test(
            string.format("2.%s.%s.9", tn1, tn2),
            string.format([[
                SELECT a, x, y FROM t2, t3 WHERE a = 4 AND x = 1 AND y IN %s 
                ORDER BY a, x DESC, y ASC;
            ]], inexpr), {
                4, 1, 2, 4, 1, 4, 4, 1, 5
            })

        test:do_execsql_test(
            "2."..tn1..".10",
            string.format([[
                SELECT a, x, y FROM t2, t3 WHERE a = 2 AND x = 1 AND y IN %s 
                ORDER BY x DESC, y ASC;
            ]], inexpr), {
                2, 1, 2, 2, 1, 4, 2, 1, 5
            })

        test:do_execsql_test(
            string.format("2.%s.%s.11", tn1, tn2),
            string.format([[
                SELECT a, x, y FROM t2, t3 WHERE a = 4 AND x = 1 AND y IN %s 
                ORDER BY a, x DESC, y DESC;
            ]], inexpr), {
                4, 1, 5, 4, 1, 4, 4, 1, 2
            })

        test:do_execsql_test(
            string.format("2.%s.%s.12", tn1, tn2),
            string.format([[
                SELECT a, x, y FROM t2, t3 WHERE a = 2 AND x = 1 AND y IN %s 
                ORDER BY x DESC, y DESC;
            ]], inexpr), {
                2, 1, 5, 2, 1, 4, 2, 1, 2
            })

    end
end
test:do_execsql_test(
    3.0,
    [[
        CREATE TABLE t7(x INT primary key);
        INSERT INTO t7 VALUES (1), (2), (3);
        CREATE INDEX i7 ON t7(x);

        CREATE TABLE t8(y INT primary key);
        INSERT INTO t8 VALUES (1), (2), (3);
    ]])

local data = {
    {"ASC",  "ASC",  {1, 2, 3}},
    {"ASC",  "DESC", {3, 2, 1}},
    {"DESC", "ASC",  {1, 2, 3}},
    {"ASC", "DESC",  {3, 2, 1}},
}

for tn, val in ipairs(data) do
    local idxdir = val[1]
    local sortdir = val[2]
    local sortdata = val[3]
    test:do_execsql_test(
        "3."..tn,
        string.format([[
            DROP INDEX IF EXISTS i8 ON t8;
            CREATE UNIQUE INDEX i8 ON t8(y %s);
            SELECT x FROM t7 WHERE x IN (SELECT y FROM t8) ORDER BY x %s;
        ]], idxdir, sortdir),
        sortdata)

end

test:finish_test()

