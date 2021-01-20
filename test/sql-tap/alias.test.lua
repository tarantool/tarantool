#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(9)

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
-- This file implements regression tests for sql library.  The
-- focus of this script is correct code generation of aliased result-set
-- values.  See ticket #3343.
--
-- Aliases are currently evaluated twice.  We might try to change this
-- in the future.  But not now.

-- A procedure to return a sequence of increasing integers.
--

_G.counter = 0

-- Function is declared as deterministic deliberately.
-- Otherwise it would be called as much as it occurs in a query.
box.schema.func.create('SEQUENCE', {language = 'Lua', is_deterministic = true,
                       returns = 'unsigned',
                       body = 'function() counter = counter + 1 return counter end',
                       exports = {'LUA', 'SQL'}})

test:do_test(
    "alias-1.1",
    function()
        return test:execsql([[
            CREATE TABLE t1(x INT primary key);
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

-- Additional call of sequence() appears due to check sequence() > 0.
-- It happens because of deterministic nature of function.
test:do_test(
    "alias-1.2",
    function()
        _G.counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 WHERE y> 0 order by x desc
        ]])
    end, {
        -- <alias-1.2>
        9, 2, 8, 3, 7, 4
        -- </alias-1.2>
    })

test:do_test(
    "alias-1.3",
    function()
        _G.counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 WHERE y>0 AND y<99  order by x desc
        ]])
    end, {
        -- <alias-1.3>
        9, 2, 8, 3, 7, 4
        -- </alias-1.3>
    })

test:do_test(
    "alias-1.4",
    function()
        _G.counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 WHERE y>0 AND y<99 AND y!=55  order by x desc
        ]])
    end, {
        -- <alias-1.4>
        9, 2, 8, 3, 7, 4
        -- </alias-1.4>
    })

-- test:do_test(
--     "alias-1.5",
--     function()
--         counter = 0
--         return test:execsql([[
--             SELECT x, sequence() AS y FROM t1
--              WHERE y>0 AND y<99 AND y!=55 AND y NOT IN (56,57,58)
--                AND y NOT LIKE 'abc%' AND y%10==2 order by x desc
--         ]])
--     end, {
--         -- <alias-1.5>
--         8, 2
--         -- </alias-1.5>
--     })

test:do_test(
    "alias-1.6",
    function()
        _G.counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 WHERE y BETWEEN 0 AND 99 order by x desc
        ]])
    end, {
        -- <alias-1.6>
        9, 2, 8, 3, 7, 4
        -- </alias-1.6>
    })

test:do_test(
    "alias-1.7",
    function()
        _G.counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 WHERE y BETWEEN 0 AND 99 order by x desc
        ]])
    end, {
        -- <alias-1.7>
        9, 2, 8, 3, 7, 4
        -- </alias-1.7>
    })

-- test:do_test(
--     "alias-1.8",
--     function()
--         counter = 1
--         return test:execsql([[
--             SELECT x, 1-sequence() AS y FROM t1 ORDER BY y;
--         ]])
--     end, {
--         -- <alias-1.8>
--         7, 0, 8, 0, 9, 0
--         -- </alias-1.8>
--     })

test:do_test(
    "alias-1.9",
    function()
        _G.counter = 0
        return test:execsql([[
            SELECT x, sequence() AS y FROM t1 ORDER BY -y
        ]])
    end, {
        -- <alias-1.9>
        7, 2, 8, 3, 9, 4
        -- </alias-1.9>
    })

-- Tests below are disabled due to incapability of sorting two or more
-- key columns with different orders (DESC/ASC). As soon as Tarantool
-- supports this feature, these tests will be uncommented.
-- #3016
-- test:do_test(
--     "alias-1.10",
--     function()
--         counter = 0
--         return test:execsql([[
--             SELECT x, sequence() AS y FROM t1 ORDER BY x%2, y
--         ]])
--     end, {
--         -- <alias-1.10>
--         8, 2, 7, 1, 9, 3
--         -- </alias-1.10>
--     })

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

-- Aliases in the GROUP BY clause cause the expression to be evaluated
-- twice in the current implementation.  This might change in the future.
--

-- Tests below are disabled due to incapability of sorting two or more
-- key columns with different orders (DESC/ASC). As soon as Tarantool
-- supports this feature, these tests will be uncommented.
-- #3016
-- test:do_test(
--     "alias-3.1",
--     function()
--         counter = 0
--         return test:execsql([[
--             SELECT sequence(*) AS y, count(*) AS z FROM t1 GROUP BY y ORDER BY z, y
--         ]])
--     end, {
--         -- <alias-3.1>
--         4, 1, 5, 1, 6, 1
--         -- </alias-3.1>
--     })

box.func.SEQUENCE:drop()

test:finish_test()
