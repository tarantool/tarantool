#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(17)

--!./tcltestrunner.lua
-- 2013 March 20
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
-- This particular file does testing of casting strings into numeric
-- values.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

-- MUST_WORK_TEST should we use
-- for _, enc in ipairs({"utf8", "utf16le", "utf16be"}) do
for _, enc in ipairs({"utf8"}) do
    test:do_test(
        "numcast-"..enc..".0",
        function()
            --db("close")
            --sqlite3("db", ":memory:")
            test:execsql("PRAGMA encoding='"..enc.."'")
            local x = test:execsql("PRAGMA encoding")[1]
            x = string.lower(x)
            x = string.gsub(x, "-", "")
            return x
        end, enc)
    local data = {
        {"1", "12345.0", 12345.0, 12345},
        {"2", "12345.0e0", 12345.0, 12345},
        {"3", "-12345.0e0", -12345.0, -12345},
        {"4", "-12345.25", -12345.25, -12345},
        {"5", "-12345.0", -12345.0, -12345},
        {"6", "'876xyz'", 876.0, 876},
        {"7", "'456ķ89'", 456.0, 456},
        {"8", "'Ġ 321.5'", 0.0, 0},
    }
    for _, val in ipairs(data) do
        local idx = val[1]
        local str = val[2]
        local rval = val[3]
        local ival = val[4]
        test:do_test(
            string.format("numcast-%s.%s.1", enc, idx),
            function()
                return test:execsql("SELECT CAST("..str.." AS real)")
            end, {
                rval
            })

        test:do_test(
            string.format("numcast-%s.%s.2", enc, idx),
            function()
                return test:execsql("SELECT CAST("..str.." AS integer)")
            end, {
                ival
            })

    end
end


test:finish_test()
