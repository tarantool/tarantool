#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(83)

--!./tcltestrunner.lua
-- 2008 Feb 6
--
-- The author disclaims copyright to this source code. In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file is to test that ticket #2927 is fixed.
--
-- $Id: tkt2927.test,v 1.4 2008/08/04 03:51:24 danielk1977 Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- Create a database.
--
test:do_test(
    "tkt2927-1.1",
    function()
        return test:execsql [[
            CREATE TABLE t1(a  INT primary key, b INT );
            INSERT INTO t1 VALUES(1,11);
            INSERT INTO t1 VALUES(2,22);
            INSERT INTO t1 VALUES(3,33);
            INSERT INTO t1 VALUES(4,44);
            INSERT INTO t1 VALUES(5,55);
            SELECT * FROM t1;
        ]]
    end, {
        -- <tkt2927-1.1>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-1.1>
    })

test:do_test(
    "tkt2927-2.1",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            UNION ALL
            SELECT a, b FROM t1
        ]]
    end, {
        -- <tkt2927-2.1>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.1>
    })

test:do_test(
    "tkt2927-2.2",
    function()
        --set sql_addop_trace 1
        return test:execsql [[
            SELECT a, b FROM t1
            UNION ALL
            SELECT a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-2.2>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.2>
    })

test:do_test(
    "tkt2927-2.3",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            UNION ALL
            SELECT abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-2.3>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.3>
    })

test:do_test(
    "tkt2927-2.4",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            UNION ALL
            SELECT abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-2.4>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.4>
    })

test:do_test(
    "tkt2927-2.5",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            UNION ALL
            SELECT a, b FROM t1
        ]]
    end, {
        -- <tkt2927-2.5>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.5>
    })

test:do_test(
    "tkt2927-2.6",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            UNION ALL
            SELECT a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-2.6>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.6>
    })

test:do_test(
    "tkt2927-2.7",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            UNION ALL
            SELECT abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-2.7>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.7>
    })

test:do_test(
    "tkt2927-2.8",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            UNION ALL
            SELECT abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-2.8>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.8>
    })

test:do_test(
    "tkt2927-2.9",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            UNION ALL
            SELECT a, b FROM t1
        ]]
    end, {
        -- <tkt2927-2.9>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.9>
    })

test:do_test(
    "tkt2927-2.10",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            UNION ALL
            SELECT a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-2.10>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.10>
    })

test:do_test(
    "tkt2927-2.11",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            UNION ALL
            SELECT abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-2.11>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.11>
    })

test:do_test(
    "tkt2927-2.12",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            UNION ALL
            SELECT abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-2.12>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.12>
    })

test:do_test(
    "tkt2927-2.13",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            UNION ALL
            SELECT a, b FROM t1
        ]]
    end, {
        -- <tkt2927-2.13>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.13>
    })

test:do_test(
    "tkt2927-2.14",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            UNION ALL
            SELECT a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-2.14>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.14>
    })

test:do_test(
    "tkt2927-2.15",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            UNION ALL
            SELECT abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-2.15>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.15>
    })

test:do_test(
    "tkt2927-2.16",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            UNION ALL
            SELECT abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-2.16>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55, 1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-2.16>
    })

test:do_test(
    "tkt2927-3.1",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            UNION 
            SELECT a, b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.1>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.1>
    })

test:do_test(
    "tkt2927-3.2",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            UNION 
            SELECT a, abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.2>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.2>
    })

test:do_test(
    "tkt2927-3.3",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            UNION 
            SELECT abs(a), b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.3>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.3>
    })

test:do_test(
    "tkt2927-3.4",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            UNION 
            SELECT abs(a), abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.4>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.4>
    })

test:do_test(
    "tkt2927-3.5",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            UNION 
            SELECT a, b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.5>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.5>
    })

test:do_test(
    "tkt2927-3.6",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            UNION 
            SELECT a, abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.6>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.6>
    })

test:do_test(
    "tkt2927-3.7",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            UNION 
            SELECT abs(a), b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.7>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.7>
    })

test:do_test(
    "tkt2927-3.8",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            UNION 
            SELECT abs(a), abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.8>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.8>
    })

test:do_test(
    "tkt2927-3.9",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            UNION 
            SELECT a, b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.9>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.9>
    })

test:do_test(
    "tkt2927-3.10",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            UNION 
            SELECT a, abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.10>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.10>
    })

test:do_test(
    "tkt2927-3.11",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            UNION 
            SELECT abs(a), b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.11>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.11>
    })

test:do_test(
    "tkt2927-3.12",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            UNION 
            SELECT abs(a), abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.12>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.12>
    })

test:do_test(
    "tkt2927-3.13",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            UNION 
            SELECT a, b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.13>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.13>
    })

test:do_test(
    "tkt2927-3.14",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            UNION 
            SELECT a, abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.14>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.14>
    })

test:do_test(
    "tkt2927-3.15",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            UNION 
            SELECT abs(a), b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.15>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.15>
    })

test:do_test(
    "tkt2927-3.16",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            UNION 
            SELECT abs(a), abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-3.16>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-3.16>
    })

test:do_test(
    "tkt2927-4.1",
    function()
        return test:execsql [[
            SELECT a+b, a-b, a, b FROM t1
            UNION ALL
            SELECT a+b, a-b, a, b FROM t1
        ]]
    end, {
        -- <tkt2927-4.1>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.1>
    })

test:do_test(
    "tkt2927-4.2",
    function()
        return test:execsql [[
            SELECT a+b, a-b, a, b FROM t1
            UNION ALL
            SELECT a+b, a-b, a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-4.2>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.2>
    })

test:do_test(
    "tkt2927-4.3",
    function()
        return test:execsql [[
            SELECT a+b, a-b, a, b FROM t1
            UNION ALL
            SELECT a+b, a-b, abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-4.3>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.3>
    })

test:do_test(
    "tkt2927-4.4",
    function()
        return test:execsql [[
            SELECT a+b, a-b, a, b FROM t1
            UNION ALL
            SELECT a+b, a-b, abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-4.4>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.4>
    })

test:do_test(
    "tkt2927-4.5",
    function()
        return test:execsql [[
            SELECT a+b, a-b, a, abs(b) FROM t1
            UNION ALL
            SELECT a+b, a-b, a, b FROM t1
        ]]
    end, {
        -- <tkt2927-4.5>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.5>
    })

test:do_test(
    "tkt2927-4.6",
    function()
        return test:execsql [[
            SELECT a+b, a-b, a, abs(b) FROM t1
            UNION ALL
            SELECT a+b, a-b, a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-4.6>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.6>
    })

test:do_test(
    "tkt2927-4.7",
    function()
        return test:execsql [[
            SELECT a+b, a-b, a, abs(b) FROM t1
            UNION ALL
            SELECT a+b, a-b, abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-4.7>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.7>
    })

test:do_test(
    "tkt2927-4.8",
    function()
        return test:execsql [[
            SELECT a+b, a-b, a, abs(b) FROM t1
            UNION ALL
            SELECT a+b, a-b, abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-4.8>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.8>
    })

test:do_test(
    "tkt2927-4.9",
    function()
        return test:execsql [[
            SELECT a+b, a-b, abs(a), b FROM t1
            UNION ALL
            SELECT a+b, a-b, a, b FROM t1
        ]]
    end, {
        -- <tkt2927-4.9>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.9>
    })

test:do_test(
    "tkt2927-4.10",
    function()
        return test:execsql [[
            SELECT a+b, a-b, abs(a), b FROM t1
            UNION ALL
            SELECT a+b, a-b, a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-4.10>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.10>
    })

test:do_test(
    "tkt2927-4.11",
    function()
        return test:execsql [[
            SELECT a+b, a-b, abs(a), b FROM t1
            UNION ALL
            SELECT a+b, a-b, abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-4.11>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.11>
    })

test:do_test(
    "tkt2927-4.12",
    function()
        return test:execsql [[
            SELECT a+b, a-b, abs(a), b FROM t1
            UNION ALL
            SELECT a+b, a-b, abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-4.12>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.12>
    })

test:do_test(
    "tkt2927-4.13",
    function()
        return test:execsql [[
            SELECT a+b, a-b, abs(a), abs(b) FROM t1
            UNION ALL
            SELECT a+b, a-b, a, b FROM t1
        ]]
    end, {
        -- <tkt2927-4.13>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.13>
    })

test:do_test(
    "tkt2927-4.14",
    function()
        return test:execsql [[
            SELECT a+b, a-b, abs(a), abs(b) FROM t1
            UNION ALL
            SELECT a+b, a-b, a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-4.14>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.14>
    })

test:do_test(
    "tkt2927-4.15",
    function()
        return test:execsql [[
            SELECT a+b, a-b, abs(a), abs(b) FROM t1
            UNION ALL
            SELECT a+b, a-b, abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-4.15>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.15>
    })

test:do_test(
    "tkt2927-4.16",
    function()
        return test:execsql [[
            SELECT a+b, a-b, abs(a), abs(b) FROM t1
            UNION ALL
            SELECT a+b, a-b, abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-4.16>
        12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55, 12, -10, 1, 11, 24, -20, 2, 22, 36, -30, 3, 33, 48, -40, 4, 44, 60, -50, 5, 55
        -- </tkt2927-4.16>
    })

test:do_test(
    "tkt2927-5.1",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            EXCEPT
            SELECT a, b FROM t1
        ]]
    end, {
        -- <tkt2927-5.1>
        
        -- </tkt2927-5.1>
    })

test:do_test(
    "tkt2927-5.2",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            EXCEPT
            SELECT a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-5.2>
        
        -- </tkt2927-5.2>
    })

test:do_test(
    "tkt2927-5.3",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            EXCEPT
            SELECT abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-5.3>
        
        -- </tkt2927-5.3>
    })

test:do_test(
    "tkt2927-5.4",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            EXCEPT
            SELECT abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-5.4>
        
        -- </tkt2927-5.4>
    })

test:do_test(
    "tkt2927-5.5",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            EXCEPT
            SELECT a, b FROM t1
        ]]
    end, {
        -- <tkt2927-5.5>
        
        -- </tkt2927-5.5>
    })

test:do_test(
    "tkt2927-5.6",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            EXCEPT
            SELECT a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-5.6>
        
        -- </tkt2927-5.6>
    })

test:do_test(
    "tkt2927-5.7",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            EXCEPT
            SELECT abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-5.7>
        
        -- </tkt2927-5.7>
    })

test:do_test(
    "tkt2927-5.8",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            EXCEPT
            SELECT abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-5.8>
        
        -- </tkt2927-5.8>
    })

test:do_test(
    "tkt2927-5.9",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            EXCEPT
            SELECT a, b FROM t1
        ]]
    end, {
        -- <tkt2927-5.9>
        
        -- </tkt2927-5.9>
    })

test:do_test(
    "tkt2927-5.10",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            EXCEPT
            SELECT a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-5.10>
        
        -- </tkt2927-5.10>
    })

test:do_test(
    "tkt2927-5.11",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            EXCEPT
            SELECT abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-5.11>
        
        -- </tkt2927-5.11>
    })

test:do_test(
    "tkt2927-5.12",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            EXCEPT
            SELECT abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-5.12>
        
        -- </tkt2927-5.12>
    })

test:do_test(
    "tkt2927-5.13",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            EXCEPT
            SELECT a, b FROM t1
        ]]
    end, {
        -- <tkt2927-5.13>
        
        -- </tkt2927-5.13>
    })

test:do_test(
    "tkt2927-5.14",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            EXCEPT
            SELECT a, abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-5.14>
        
        -- </tkt2927-5.14>
    })

test:do_test(
    "tkt2927-5.15",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            EXCEPT
            SELECT abs(a), b FROM t1
        ]]
    end, {
        -- <tkt2927-5.15>
        
        -- </tkt2927-5.15>
    })

test:do_test(
    "tkt2927-5.16",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            EXCEPT
            SELECT abs(a), abs(b) FROM t1
        ]]
    end, {
        -- <tkt2927-5.16>
        
        -- </tkt2927-5.16>
    })

test:do_test(
    "tkt2927-6.1",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            INTERSECT
            SELECT a, b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.1>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.1>
    })

test:do_test(
    "tkt2927-6.2",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            INTERSECT
            SELECT a, abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.2>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.2>
    })

test:do_test(
    "tkt2927-6.3",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            INTERSECT
            SELECT abs(a), b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.3>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.3>
    })

test:do_test(
    "tkt2927-6.4",
    function()
        return test:execsql [[
            SELECT a, b FROM t1
            INTERSECT
            SELECT abs(a), abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.4>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.4>
    })

test:do_test(
    "tkt2927-6.5",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            INTERSECT
            SELECT a, b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.5>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.5>
    })

test:do_test(
    "tkt2927-6.6",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            INTERSECT
            SELECT a, abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.6>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.6>
    })

test:do_test(
    "tkt2927-6.7",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            INTERSECT
            SELECT abs(a), b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.7>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.7>
    })

test:do_test(
    "tkt2927-6.8",
    function()
        return test:execsql [[
            SELECT a, abs(b) FROM t1
            INTERSECT
            SELECT abs(a), abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.8>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.8>
    })

test:do_test(
    "tkt2927-6.9",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            INTERSECT
            SELECT a, b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.9>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.9>
    })

test:do_test(
    "tkt2927-6.10",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            INTERSECT
            SELECT a, abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.10>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.10>
    })

test:do_test(
    "tkt2927-6.11",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            INTERSECT
            SELECT abs(a), b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.11>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.11>
    })

test:do_test(
    "tkt2927-6.12",
    function()
        return test:execsql [[
            SELECT abs(a), b FROM t1
            INTERSECT
            SELECT abs(a), abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.12>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.12>
    })

test:do_test(
    "tkt2927-6.13",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            INTERSECT
            SELECT a, b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.13>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.13>
    })

test:do_test(
    "tkt2927-6.14",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            INTERSECT
            SELECT a, abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.14>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.14>
    })

test:do_test(
    "tkt2927-6.15",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            INTERSECT
            SELECT abs(a), b FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.15>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.15>
    })

test:do_test(
    "tkt2927-6.16",
    function()
        return test:execsql [[
            SELECT abs(a), abs(b) FROM t1
            INTERSECT
            SELECT abs(a), abs(b) FROM t1
            ORDER BY 1
        ]]
    end, {
        -- <tkt2927-6.16>
        1, 11, 2, 22, 3, 33, 4, 44, 5, 55
        -- </tkt2927-6.16>
    })

-- Ticket #3092 is the same bug.  But another test case never hurts.
--
test:do_test(
    "tkt2927-7.1",
    function()
        return test:execsql [[
            CREATE TABLE host (
             hostname text not null primary key,
             consoleHost text,
             consolePort int
            );
            INSERT INTO host VALUES('aald04','aalp03',4);
            INSERT INTO host VALUES('aald17','aalp01',1);
            CREATE VIEW consolemap1a as
              select hostname, consolehost, '/dev/cuaD0.' || cast(consoleport-1 as text) consoleport
                from host where consolehost='aalp01';
            CREATE VIEW consolemap1b as
              select hostname hostname, consolehost consolehost, '/dev/cuaD' ||
                     substr('01',1+((consoleport-1)/16),1) ||
                     substr('0123456789abcdef',1+((consoleport-1)%16),1) consoleport
                from host where consolehost='aalp03';
            CREATE VIEW consolemap1 as
              select * from consolemap1a
              union
              select * from consolemap1b;
            SELECT * from consolemap1b;
        ]]
    end, {
        -- <tkt2927-7.1>
        "aald04", "aalp03", "/dev/cuaD03"
        -- </tkt2927-7.1>
    })

test:do_test(
    "tkt2927-7.2",
    function()
        return test:execsql [[
            SELECT * FROM consolemap1
        ]]
    end, {
        -- <tkt2927-7.2>
        "aald04", "aalp03", "/dev/cuaD03", "aald17", "aalp01", "/dev/cuaD0.0"
        -- </tkt2927-7.2>
    })

test:finish_test()

