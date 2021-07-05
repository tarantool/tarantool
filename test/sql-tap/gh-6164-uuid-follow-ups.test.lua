#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(1)

-- Make sure that function quote() can work with uuid.
test:do_execsql_test(
    "gh-6164-1",
    [[
        SELECT quote(cast('11111111-1111-1111-1111-111111111111' as uuid));
    ]], {
        '11111111-1111-1111-1111-111111111111'
    })

test:finish_test()
