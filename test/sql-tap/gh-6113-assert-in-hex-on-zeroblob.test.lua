#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(1)

test:do_execsql_test(
    "gh-6113",
    [[
        SELECT HEX(ZEROBLOB(0)), HEX(ZEROBLOB(10));
    ]], {
        '', '00000000000000000000'
    })

test:finish_test()
