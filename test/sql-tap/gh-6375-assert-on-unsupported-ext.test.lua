#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(2)

--
-- Make sure that the correct error occurs when unsupported extension is
-- received by SQL.
--
test:do_catchsql_test(
    "gh-6375-1",
    [[
        SELECT LUA('return box.space._vindex:select()[1]');
    ]], {
        1, "Failed to execute SQL statement: Unsupported type passed from Lua"
    })

test:do_catchsql_test(
    "gh-6375-2",
    [[
        SELECT 1 > LUA('return box.space._vindex:select()[1]');
    ]], {
        1, "Failed to execute SQL statement: Unsupported type passed from Lua"
    })

test:finish_test()
