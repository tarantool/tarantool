#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(0)

--!./tcltestrunner.lua
-- 2008 August 28
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
-- This file implements regression tests for SQLite library.  The
-- focus of this script is correct code generation of aliased result-set
-- values.  See ticket #3343.
--
-- $Id: alias.test,v 1.3 2009/04/23 13:22:44 drh Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Aliases are currently evaluated twice.  We might try to change this
-- in the future.  But not now.

-- A procedure to return a sequence of increasing integers.
--
--X(29, "X!cmd", "namespace","eval","::seq","\n  variable counter 0\n

-- MUST_WORK_TEST issuing sequence method more times than requiried
if 0>0 then
local counter = 0
local function sequence (args)
    counter = counter + 1
    return counter
end
box.internal.sql_create_function("sequence", sequence)
--db("function", "sequence", "::seq::value")
test:do_test(
    "alias-1.1",
    function()
        return test:execsql([[
            CREATE TABLE t1(x primary key);
            INSERT INTO t1 VALUES(9);
            INSERT INTO t1 VALUES(8);
            INSERT INTO t1 VALUES(7);
            SELECT x, sequence() FROM t1 order by x desc;
        ]])
    end, {
        -- <alias-1.1>
        9, 1, 8, 2, 7, 3
        -- </alias-1.1>
    })

counter = 0
test:do_test(
    "alias-1.2",
    function()
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 WHERE y> 0 order by x desc
        ]])
    end, {
        -- <alias-1.2>
        9, 1, 8, 2, 7, 3
        -- </alias-1.2>
    })

test:do_test(
    "alias-1.3",
    function()
        counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 WHERE y>0 AND y<99  order by x desc
        ]])
    end, {
        -- <alias-1.3>
        9, 1, 8, 2, 7, 3
        -- </alias-1.3>
    })

test:do_test(
    "alias-1.4",
    function()
        counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 WHERE y>0 AND y<99 AND y!=55  order by x desc
        ]])
    end, {
        -- <alias-1.4>
        9, 1, 8, 2, 7, 3
        -- </alias-1.4>
    })

test:do_test(
    "alias-1.5",
    function()
        counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1
             WHERE y>0 AND y<99 AND y!=55 AND y NOT IN (56,57,58)
               AND y NOT LIKE 'abc%' AND y%10==2 order by x desc
        ]])
    end, {
        -- <alias-1.5>
        8, 2
        -- </alias-1.5>
    })

test:do_test(
    "alias-1.6",
    function()
        counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 WHERE y BETWEEN 0 AND 99 order by x desc
        ]])
    end, {
        -- <alias-1.6>
        9, 1, 8, 2, 7, 3
        -- </alias-1.6>
    })

--do_test alias-1.7 {
--  ::seq::reset
--  db eval {
--    SELECT x, sequence() AS y FROM t1 WHERE y IN (55,66,3)
--  }
--} {7 3}
test:do_test(
    "alias-1.8",
    function()
        counter = 0
        return test:execsql([[
            SELECT x, 1-sequence() AS y FROM t1 ORDER BY y
        ]])
    end, {
        -- <alias-1.8>
        7, -2, 8, -1, 9, 0
        -- </alias-1.8>
    })

test:do_test(
    "alias-1.9",
    function()
        counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 ORDER BY -y
        ]])
    end, {
        -- <alias-1.9>
        7, 3, 8, 2, 9, 1
        -- </alias-1.9>
    })

test:do_test(
    "alias-1.10",
    function()
        counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 ORDER BY x%2, y
        ]])
    end, {
        -- <alias-1.10>
        8, 2, 9, 1, 7, 3
        -- </alias-1.10>
    })

-- ["unset","-nocomplain","random_int_list"]
local random_int_list = test:execsql([[
    SELECT random()&2147483647 AS r FROM t1, t1, t1, t1 ORDER BY r
]])
local random_int_list_copy = table.deepcopy(random_int_list)
test:do_test(
    "alias-1.11",
    function()
        table.sort(random_int_list)
        return random_int_list
    end, random_int_list_copy)

test:do_test(
    "alias-2.1",
    function()
        return test:execsql([[
            SELECT 4 UNION SELECT 1 ORDER BY 1
        ]])
    end, {
        -- <alias-2.1>
        1, 4
        -- </alias-2.1>
    })

test:do_test(
    "alias-2.2",
    function()
        return test:execsql([[
            SELECT 4 UNION SELECT 1 UNION SELECT 9 ORDER BY 1
        ]])
    end, {
        -- <alias-2.2>
        1, 4, 9
        -- </alias-2.2>
    })

if 0 then
    -- Aliases in the GROUP BY clause cause the expression to be evaluated
    -- twice in the current implementation.  This might change in the future.
    --
    test:do_test(
        "alias-3.1",
        function()
            counter = 0
            return test:execsql([[
                SELECT sequence(*) AS y, count(*) AS z FROM t1 GROUP BY y ORDER BY z, y
            ]])
        end, {
            -- <alias-3.1>
            1, 1, 2, 1, 3, 1
            -- </alias-3.1>
        })

end
end

test:finish_test()
