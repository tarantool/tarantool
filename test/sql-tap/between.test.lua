#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(11)

--!./tcltestrunner.lua
-- 2005 July 28
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
-- focus of this file is testing the use of indices in WHERE clauses
-- when the WHERE clause contains the BETWEEN operator.
--
-- $Id: between.test,v 1.2 2006/01/17 09:35:02 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Build some test data
--
test:do_test(
    "between-1.0",
    function()
        test:execsql([[
            CREATE TABLE t1(w int primary key, x int, y int, z int);
        ]])
        for i = 1, 100, 1 do
            local w, x, y, z
            w = i
            x = math.floor((math.log(i)/math.log(2)))
            y = (((i * i) + (2 * i)) + 1)
            z = (x + y)
            -- Random unplanned test of the $varname variable syntax.
            test:execsql(string.format("INSERT INTO t1 VALUES(%s,%s,%s,%s)", w, x, y, z))
        end
        return test:execsql([[
            CREATE INDEX i1xy ON t1(x,y);
            CREATE INDEX i1zyx ON t1(z,y,x);
        ]])
    end, {
        -- <between-1.0>

        -- </between-1.0>
    })

-- This procedure executes the SQL.  Then it appends to the result the
-- "sort" or "nosort" keyword depending on whether or not any sorting
-- is done.  Then it appends the names of the table and index used.
--
local function queryplan(sql)
    local sql_sort_count = box.stat.sql().sql_sort_count
    local data = test:execsql(sql)
    local x = "nosort"
    if box.stat.sql().sql_sort_count - sql_sort_count then
        x = "sort"
    end
    table.insert(data,x)
    local eqp = box.execute("EXPLAIN QUERY PLAN "..sql.."").rows
    -- puts eqp=$eqp
    for _, val in ipairs(eqp) do
        --local a = val[1]
        --local b = val[2]
        --local c = val[3]
        local x = val[4]
        local tab, idx = string.match(x, "TABLE (%w+) USING.* INDEX (%w+)")

        if tab then
            table.insert(data, tab)
            table.insert(data, idx)
        else
            tab = string.match(x, "TABLE (%w+)")
            if tab then
                table.insert(data, tab)
                table.insert(data, "*")
            end
        end
    end
    return data
end

test:do_test(
    "between-1.1.1",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE w BETWEEN 5 AND 6 ORDER BY +w
  ]])
    end, {
        -- <between-1.1.1>
        --5, 2, 36, 38, 6, 2, 49, 51, "sort", "t1", "i1w"
        5, 2, 36, 38, 6, 2, 49, 51, "sort", "T1", "*"
        -- </between-1.1.1>
    })

test:do_test(
    "between-1.1.2",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE +w BETWEEN 5 AND 6 ORDER BY +w
  ]])
    end, {
        -- <between-1.1.2>
        5, 2, 36, 38, 6, 2, 49, 51, "sort", "T1", "*"
        -- </between-1.1.2>
    })

test:do_test(
    "between-1.2.1",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE w BETWEEN 5 AND 65-y ORDER BY +w
  ]])
    end, {
        -- <between-1.2.1>
        --5, 2, 36, 38, 6, 2, 49, 51, "sort", "t1", "i1w"
        5, 2, 36, 38, 6, 2, 49, 51, "sort", "T1", "*"
        -- </between-1.2.1>
    })

test:do_test(
    "between-1.2.2",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE +w BETWEEN 5 AND 65-y ORDER BY +w
  ]])
    end, {
        -- <between-1.2.2>
        5, 2, 36, 38, 6, 2, 49, 51, "sort", "T1", "*"
        -- </between-1.2.2>
    })

test:do_test(
    "between-1.3.1",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE w BETWEEN 41-y AND 6 ORDER BY +w
  ]])
    end, {
        -- <between-1.3.1>
        --5, 2, 36, 38, 6, 2, 49, 51, "sort", "t1", "i1w"
        5, 2, 36, 38, 6, 2, 49, 51, "sort", "T1", "*"
        -- </between-1.3.1>
    })

test:do_test(
    "between-1.3.2",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE +w BETWEEN 41-y AND 6 ORDER BY +w
  ]])
    end, {
        -- <between-1.3.2>
        5, 2, 36, 38, 6, 2, 49, 51, "sort", "T1", "*"
        -- </between-1.3.2>
    })

test:do_test(
    "between-1.4",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE w BETWEEN 41-y AND 65-y ORDER BY +w
  ]])
    end, {
        -- <between-1.4>
        5, 2, 36, 38, 6, 2, 49, 51, "sort", "T1", "*"
        -- </between-1.4>
    })

test:do_test(
    "between-1.5.1",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE 26 BETWEEN y AND z ORDER BY +w
  ]])
    end, {
        -- <between-1.5.1>
        4, 2, 25, 27, "sort", "T1", "I1ZYX"
        -- </between-1.5.1>
    })

test:do_test(
    "between-1.5.2",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE 26 BETWEEN +y AND z ORDER BY +w
  ]])
    end, {
        -- <between-1.5.2>
        4, 2, 25, 27, "sort", "T1", "I1ZYX"
        -- </between-1.5.2>
    })

test:do_test(
    "between-1.5.3",
    function()
        return queryplan([[
    SELECT * FROM t1 WHERE 26 BETWEEN y AND +z ORDER BY +w
  ]])
    end, {
        -- <between-1.5.3>
        4, 2, 25, 27, "sort", "T1", "*"
        -- </between-1.5.3>
    })



test:finish_test()
