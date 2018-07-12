#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(9)

--!./tcltestrunner.lua
-- 2009 November 10
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- Additional test cases for the COALESCE() and IFNULL() functions.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "coalesce-1.0",
    function()
        return test:execsql [[
            CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT, c INT, d INT);
            INSERT INTO t1 VALUES(1, null, null, null);
            INSERT INTO t1 VALUES(2, 2, 99, 99);
            INSERT INTO t1 VALUES(3, null, 3, 99);
            INSERT INTO t1 VALUES(4, null, null, 4);
            INSERT INTO t1 VALUES(5, null, null, null);
            INSERT INTO t1 VALUES(6, 22, 99, 99);
            INSERT INTO t1 VALUES(7, null, 33, 99);
            INSERT INTO t1 VALUES(8, null, null, 44);

            SELECT coalesce(b,c,d) FROM t1 ORDER BY a;
        ]]
    end, {
        -- <coalesce-1.0>
        "", 2, 3, 4, "", 22, 33, 44
        -- </coalesce-1.0>
    })

test:do_test(
    "coalesce-1.1",
    function()
        return test:execsql [[
            SELECT coalesce(d+c+b,d+c,d) FROM t1 ORDER BY a;
        ]]
    end, {
        -- <coalesce-1.1>
        "", 200, 102, 4, "", 220, 132, 44
        -- </coalesce-1.1>
    })

test:do_test(
    "coalesce-1.2",
    function()
        return test:execsql [[
            SELECT ifnull(d+c+b,ifnull(d+c,d)) FROM t1 ORDER BY a;
        ]]
    end, {
        -- <coalesce-1.2>
        "", 200, 102, 4, "", 220, 132, 44
        -- </coalesce-1.2>
    })

test:do_test(
    "coalesce-1.3",
    function()
        return test:execsql [[
            SELECT ifnull(ifnull(d+c+b,d+c),d) FROM t1 ORDER BY a;
        ]]
    end, {
        -- <coalesce-1.3>
        "", 200, 102, 4, "", 220, 132, 44
        -- </coalesce-1.3>
    })

test:do_test(
    "coalesce-1.4",
    function()
        return test:execsql [[
            SELECT ifnull(ifnull(b,c),d) FROM t1 ORDER BY a;
        ]]
    end, {
        -- <coalesce-1.4>
        "", 2, 3, 4, "", 22, 33, 44
        -- </coalesce-1.4>
    })

test:do_test(
    "coalesce-1.5",
    function()
        return test:execsql [[
            SELECT ifnull(b,ifnull(c,d)) FROM t1 ORDER BY a;
        ]]
    end, {
        -- <coalesce-1.5>
        "", 2, 3, 4, "", 22, 33, 44
        -- </coalesce-1.5>
    })

test:do_test(
    "coalesce-1.6",
    function()
        return test:execsql [[
            SELECT coalesce(b,NOT b,-b,abs(b),lower(b),length(b),min(b,5),b*123,c)
              FROM t1 ORDER BY a;
        ]]
    end, {
        -- <coalesce-1.6>
        "", 2, 3, "", "", 22, 33, ""
        -- </coalesce-1.6>
    })

test:do_test(
    "coalesce-1.7",
    function()
        return test:execsql [[
            SELECT ifnull(nullif(a,4),99)
              FROM t1 ORDER BY a;
        ]]
    end, {
        -- <coalesce-1.7>
        1, 2, 3, 99, 5, 6, 7, 8
        -- </coalesce-1.7>
    })

test:do_test(
    "coalesce-1.8",
    function()
        return test:execsql [[
            SELECT coalesce(
              CASE WHEN b=2 THEN 123 END,
              CASE WHEN b=3 THEN 234 END,
              CASE WHEN c=3 THEN 345 WHEN c=33 THEN 456 END,
              d
            )
            FROM t1 ORDER BY a;
        ]]
    end, {
        -- <coalesce-1.8>
        "", 123, 345, 4, "", 99, 456, 44
        -- </coalesce-1.8>
    })



test:finish_test()
