#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(4)

-- Make sure that the comparison between DECIMAL and large DOUBLE is correct.
test:do_execsql_test(
    "gh-6376-1",
    [[
        SELECT CAST(1 AS DECIMAL) < -1e40;
    ]], {
        false
    })

test:do_execsql_test(
    "gh-6376-2",
    [[
        SELECT CAST(-1 AS DECIMAL) > -1e400;
    ]], {
        true
    })

test:do_execsql_test(
    "gh-6376-3",
    [[
        SELECT CAST(1 AS DECIMAL) <= 1e40;
    ]], {
        true
    })

test:do_execsql_test(
    "gh-6376-4",
    [[
        SELECT CAST(1 AS DECIMAL) >= 1e400;
    ]], {
        false
    })

test:finish_test()
