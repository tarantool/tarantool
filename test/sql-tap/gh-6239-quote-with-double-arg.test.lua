#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(1)

-- Make sure that QUOTE() returns DOUBLE in cast it receives DOUBLE.
test:do_execsql_test(
    "gh-6239",
    [[
        SELECT QUOTE(1.5e0), TYPEOF(QUOTE(1.5e0));
    ]], {
        1.5, "double"
    })

test:finish_test()
