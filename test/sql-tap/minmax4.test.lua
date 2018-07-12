#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(21)

--!./tcltestrunner.lua
-- 2012 February 02
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
-- Test for queries of the form:  
--
--    SELECT p, max(q) FROM t1;
--
-- Demonstration that the value returned for p is on the same row as 
-- the maximum q.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_test(
    "minmax4-1.1",
    function()
        return test:execsql [[
            CREATE TABLE t1(p INT primary key,q INT);
            SELECT p, max(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.1>
        "", ""
        -- </minmax4-1.1>
    })

test:do_test(
    "minmax4-1.2",
    function()
        return test:execsql [[
            SELECT p, min(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.2>
        "", ""
        -- </minmax4-1.2>
    })

test:do_test(
    "minmax4-1.3",
    function()
        return test:execsql [[
            INSERT INTO t1 VALUES(1,2);
            SELECT p, max(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.3>
        1, 2
        -- </minmax4-1.3>
    })

test:do_test(
    "minmax4-1.4",
    function()
        return test:execsql [[
            SELECT p, min(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.4>
        1, 2
        -- </minmax4-1.4>
    })

test:do_test(
    "minmax4-1.5",
    function()
        return test:execsql [[
            INSERT INTO t1 VALUES(3,4);
            SELECT p, max(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.5>
        3, 4
        -- </minmax4-1.5>
    })

test:do_test(
    "minmax4-1.6",
    function()
        local r = {test:execsql [[
            SELECT p, min(q) FROM t1;
        ]] }
        table.insert(r, test:execsql([[
            SELECT p FROM (SELECT p, min(q) FROM t1);
        ]]))
        return r
    end, {
        -- <minmax4-1.6>
        {1, 2},
        {1}
        -- </minmax4-1.6>
    })

test:do_test(
    "minmax4-1.7",
    function()
        local r = {test:execsql [[
            INSERT INTO t1 VALUES(5,0);
            SELECT p, max(q) FROM t1;
        ]] }
        table.insert(r, test:execsql([[
            SELECT p FROM (SELECT max(q), p FROM t1);
        ]]))
        return r
    end, {
        -- <minmax4-1.7>
        {3, 4}, {3}
        -- </minmax4-1.7>
    })

test:do_test(
    "minmax4-1.8",
    function()
        return test:execsql [[
            SELECT p, min(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.8>
        5, 0
        -- </minmax4-1.8>
    })

test:do_test(
    "minmax4-1.9",
    function()
        local r = {test:execsql [[
            INSERT INTO t1 VALUES(6,1);
            SELECT p, max(q) FROM t1;
        ]] }
        table.insert(r, test:execsql([[
            SELECT p FROM (SELECT max(q), p FROM t1);
        ]]))
        return r
    end, {
        -- <minmax4-1.9>
        {3, 4}, {3}
        -- </minmax4-1.9>
    })

test:do_test(
    "minmax4-1.10",
    function()
        return test:execsql [[
            SELECT p, min(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.10>
        5, 0
        -- </minmax4-1.10>
    })

test:do_test(
    "minmax4-1.11",
    function()
        return test:execsql [[
            INSERT INTO t1 VALUES(7,NULL);
            SELECT p, max(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.11>
        3, 4
        -- </minmax4-1.11>
    })

test:do_test(
    "minmax4-1.12",
    function()
        return test:execsql [[
            SELECT p, min(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.12>
        5, 0
        -- </minmax4-1.12>
    })

test:do_test(
    "minmax4-1.13",
    function()
        return test:execsql [[
            DELETE FROM t1 WHERE q IS NOT NULL;
            SELECT p, max(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.13>
        7, ""
        -- </minmax4-1.13>
    })

test:do_test(
    "minmax4-1.14",
    function()
        return test:execsql [[
            SELECT p, min(q) FROM t1;
        ]]
    end, {
        -- <minmax4-1.14>
        7, ""
        -- </minmax4-1.14>
    })

test:do_test(
    "minmax4-2.1",
    function()
        return test:execsql [[
            CREATE TABLE t2(a INT,b INT,c INT primary key);
            INSERT INTO t2 VALUES
                 (1,null,2),
                 (1,2,3),
                 (1,1,4),
                 (2,3,5);
            SELECT a, max(b), c FROM t2 GROUP BY a ORDER BY a;
        ]]
    end, {
        -- <minmax4-2.1>
        1, 2, 3, 2, 3, 5
        -- </minmax4-2.1>
    })

test:do_test(
    "minmax4-2.2",
    function()
        return test:execsql [[
            SELECT a, min(b), c FROM t2 GROUP BY a ORDER BY a;
        ]]
    end, {
        -- <minmax4-2.2>
        1, 1, 4, 2, 3, 5
        -- </minmax4-2.2>
    })

test:do_test(
    "minmax4-2.3",
    function()
        return test:execsql [[
            SELECT a, min(b), avg(b), count(b), c FROM t2 GROUP BY a ORDER BY a DESC;
        ]]
    end, {
        -- <minmax4-2.3>
        2, 3, 3.0, 1, 5, 1, 1, 1.5, 2, 4
        -- </minmax4-2.3>
    })

test:do_test(
    "minmax4-2.4",
    function()
        return test:execsql [[
            SELECT a, min(b), max(b), c FROM t2 GROUP BY a ORDER BY a;
        ]]
    end, {
        -- <minmax4-2.4>
        1, 1, 2, 3, 2, 3, 3, 5
        -- </minmax4-2.4>
    })

test:do_test(
    "minmax4-2.5",
    function()
        return test:execsql [[
            SELECT a, max(b), min(b), c FROM t2 GROUP BY a ORDER BY a;
        ]]
    end, {
        -- <minmax4-2.5>
        1, 2, 1, 4, 2, 3, 3, 5
        -- </minmax4-2.5>
    })

test:do_test(
    "minmax4-2.6",
    function()
        return test:execsql [[
            SELECT a, max(b), b, max(c), c FROM t2 GROUP BY a ORDER BY a;
        ]]
    end, {
        -- <minmax4-2.6>
        1, 2, 1, 4, 4, 2, 3, 3, 5, 5
        -- </minmax4-2.6>
    })

test:do_test(
    "minmax4-2.7",
    function()
        return test:execsql [[
            SELECT a, min(b), b, min(c), c FROM t2 GROUP BY a ORDER BY a;
        ]]
    end, {
        -- <minmax4-2.7>
        1, 1, "", 2, 2, 2, 3, 3, 5, 5
        -- </minmax4-2.7>
    })



test:finish_test()
