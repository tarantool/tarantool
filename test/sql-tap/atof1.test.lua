#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(9999)

--!./tcltestrunner.lua
-- 2012 June 18
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
-- Tests of the sqlAtoF() function.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


for i = 1, 10000 - 1, 1 do
    local pow = math.random(100)
    local x = math.pow((math.random()-0.5)*2*math.random(), pow)

-- Pointless test
--    local xf = string.format("%.32e", x)
--    print("\nxf "..xf.." x "..x)
--    -- Verify that text->real conversions get exactly same ieee754 floating-
--    -- point value in sql as they do in TCL.
--    --
--    test:do_test(
--        "atof1-1."..i..".1",
--        function()
--            local y = test:execsql("SELECT "..xf.."="..x)
--            return y
--        end, {
--            1
--        })

    -- Verify that round-trip real->text->real conversions using the quote()
    -- function preserve the bits of the numeric value exactly.
    --
    test:do_test(
        "atof1-1."..i..".2",
        function()
            local y = test:execsql(string.format("SELECT %s=CAST(quote(%s) AS NUMBER)",x, x))
            return y
        end, {
            true
        })

end


test:finish_test()
