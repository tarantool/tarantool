#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(10)

--
-- Make sure that number of arguments check is checked properly for SQL built-in
-- functions with variable number of arguments.
--
test:do_catchsql_test(
    "builtins-1.1",
    [[
        SELECT COUNT(1, 2);
    ]],
    {
        1, [[Wrong number of arguments is passed to COUNT(): ]]..
           [[expected from 0 to 1, got 2]]
    }
)

test:do_catchsql_test(
    "builtins-1.2",
    [[
        SELECT GREATEST();
    ]],
    {
        1, [[Wrong number of arguments is passed to GREATEST(): ]]..
           [[expected at least 2, got 0]]
    }
)

test:do_catchsql_test(
    "builtins-1.3",
    [[
        SELECT GROUP_CONCAT();
    ]],
    {
        1, [[Wrong number of arguments is passed to GROUP_CONCAT(): ]]..
           [[expected from 1 to 2, got 0]]
    }
)

test:do_catchsql_test(
    "builtins-1.4",
    [[
        SELECT GROUP_CONCAT(1, 2, 3);
    ]],
    {
        1, [[Wrong number of arguments is passed to GROUP_CONCAT(): ]]..
           [[expected from 1 to 2, got 3]]
    }
)

test:do_catchsql_test(
    "builtins-1.5",
    [[
        SELECT LEAST();
    ]],
    {
        1, [[Wrong number of arguments is passed to LEAST(): ]]..
           [[expected at least 2, got 0]]
    }
)

test:do_catchsql_test(
    "builtins-1.6",
    [[
        SELECT ROUND();
    ]],
    {
        1, [[Wrong number of arguments is passed to ROUND(): ]]..
           [[expected from 1 to 2, got 0]]
    }
)

test:do_catchsql_test(
    "builtins-1.7",
    [[
        SELECT ROUND(1, 2, 3);
    ]],
    {
        1, [[Wrong number of arguments is passed to ROUND(): ]]..
           [[expected from 1 to 2, got 3]]
    }
)

test:do_catchsql_test(
    "builtins-1.8",
    [[
        SELECT SUBSTR('1');
    ]],
    {
        1, [[Wrong number of arguments is passed to SUBSTR(): ]]..
           [[expected from 2 to 3, got 1]]
    }
)

test:do_catchsql_test(
    "builtins-1.9",
    [[
        SELECT SUBSTR('1', '2', '3', '4');
    ]],
    {
        1, [[Wrong number of arguments is passed to SUBSTR(): ]]..
           [[expected from 2 to 3, got 4]]
    }
)

test:do_catchsql_test(
    "builtins-1.10",
    [[
        SELECT UUID(1, 2);
    ]],
    {
        1, [[Wrong number of arguments is passed to UUID(): ]]..
           [[expected from 0 to 1, got 2]]
    }
)

test:finish_test()
