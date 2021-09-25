#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(26)

local dec = require('decimal')

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
-- This file implements regression tests for sql library.
-- This particular file does testing of casting strings into numeric
-- values.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

-- MUST_WORK_TEST should we use
-- for _, enc in ipairs({"utf8", }) do
for _, enc in ipairs({"utf8"}) do
    test:do_test(
        "numcast-"..enc..".0",
        function()
            --test:execsql("PRAGMA encoding='"..enc.."'")
            local x = "utf8"--test:execsql("PRAGMA encoding")[1]
            x = string.lower(x)
            x = string.gsub(x, "-", "")
            return x
        end, enc)
    local data = {
        {"1", "12345.0", dec.new(12345.0), 12345},
        {"2", "12345.0e0", 12345.0, 12345},
        {"3", "-12345.0e0", -12345.0, -12345},
        {"4", "-12345.25", dec.new(-12345.25), -12345},
        {"5", "-12345.0", dec.new(-12345.0), -12345},
    }
    for _, val in ipairs(data) do
        local idx = val[1]
        local str = val[2]
        local rval = val[3]
        local ival = val[4]
        test:do_test(
            string.format("numcast-%s.%s.1", enc, idx),
            function()
                return test:execsql("SELECT CAST("..str.." AS NUMBER)")
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

--
-- gh-4526: make sure that DOUBLE values that more than
-- 9223372036854775296 and less than 18446744073709551616 can be
-- converted to INTEGER or UNSIGNED.
--
test:do_execsql_test(
    "cast-2.1",
    [[
        SELECT CAST((9223372036854775297.01e0) AS INTEGER);
    ]], {
        9223372036854775808ULL
    })

test:do_execsql_test(
    "cast-2.2",
    [[
        SELECT CAST((18000000000000000000e0) AS INTEGER);
    ]], {
        18000000000000000000ULL
    })

test:do_execsql_test(
    "cast-2.3",
    [[
        SELECT CAST((9223372036854775297.01e0) AS UNSIGNED);
    ]], {
        9223372036854775808ULL
    })

test:do_execsql_test(
    "cast-2.4",
    [[
        SELECT CAST((18000000000000000000e0) AS UNSIGNED);
    ]], {
        18000000000000000000ULL
    })

test:do_catchsql_test(
    "cast-2.5",
    [[
        SELECT CAST((20000000000000000000e0) AS UNSIGNED);
    ]], {
        1,"Type mismatch: can not convert double(2.0e+19) to unsigned"
    })

test:do_execsql_test(
    "cast-2.6",
    [[
        CREATE TABLE t (i INTEGER PRIMARY KEY);
        INSERT INTO t VALUES(9223372036854775297.01e0);
        SELECT * FROM t;
    ]], {
        9223372036854775808ULL
    })

test:do_execsql_test(
    "cast-2.7",
    [[
        INSERT INTO t VALUES(18000000000000000000.01e0);
        SELECT * FROM t;
    ]], {
        9223372036854775808ULL,18000000000000000000ULL
    })

test:do_catchsql_test(
    "cast-2.8",
    [[
        INSERT INTO t VALUES(20000000000000000000.01e0);
        SELECT * FROM t;
    ]], {
        1,"Type mismatch: can not convert double(2.0e+19) to integer"
    })

test:do_catchsql_test(
    "cast-2.9",
    [[
        INSERT INTO t VALUES(2.1e0);
    ]], {
        1, "Type mismatch: can not convert double(2.1) to integer"
    })

test:do_execsql_test(
    "numcast-3.8",
    [[
        SELECT (1 + 0) / 3, (1 + 0e0) / 3, (1 + 0) / 3e0;
    ]], {
        0, 0.33333333333333, 0.33333333333333
})

test:do_execsql_test(
    "numcast-3.9",
    [[
        SELECT (1 - 0) / 3, (1 - 0e0) / 3, (1 - 0) / 3e0;
    ]], {
        0, 0.33333333333333, 0.33333333333333
})

test:do_execsql_test(
    "numcast-3.10",
    [[
        SELECT (1 * 1) / 3, (1 * 1e0) / 3, (1 * 1) / 3e0;
    ]], {
        0, 0.33333333333333, 0.33333333333333
})

test:do_execsql_test(
    "numcast-3.11",
    [[
        SELECT (1 / 1) / 3, (1 / 1e0) / 3, (1 / 1) / 3e0;
    ]], {
        0 , 0.33333333333333, 0.33333333333333
})

--
-- gh-6010: Make sure that a negative DOUBLE value cannot be explicitly cast to
-- UNSIGNED.
--
test:do_catchsql_test(
    "numcast-4",
    [[
        SELECT CAST(-2.5e0 AS UNSIGNED);
    ]], {
        1, "Type mismatch: can not convert double(-2.5) to unsigned"
})

--
-- gh-6225: Make sure negative DOUBLE is greater than -1.0 and less than 0.0 can
-- be converted to INTEGER and UNSIGNED.
--
test:do_execsql_test(
    "numcast-5",
    [[
        SELECT CAST(-0.999e0 AS INTEGER), CAST(-0.111e0 AS UNSIGNED);
    ]], {
        0, 0
})

test:finish_test()
