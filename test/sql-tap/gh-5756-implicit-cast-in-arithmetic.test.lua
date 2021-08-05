#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(30)

--
-- Make sure that arithmetic operations can only accept numeric values. Also,
-- operation '%' can accept only INTEGER and UNSIGNED values.
--
test:do_execsql_test(
    "gh-5756-1.1",
    [[
        SELECT 9 + 2;
    ]], {
        11
    })

test:do_execsql_test(
    "gh-5756-1.2",
    [[
        SELECT 9 + -2;
    ]], {
        7
    })

test:do_execsql_test(
    "gh-5756-1.3",
    [[
        SELECT 9 + 2.0;
    ]], {
        11
    })

test:do_catchsql_test(
    "gh-5756-1.4",
    [[
        SELECT 9 + '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to number"
    })

test:do_catchsql_test(
    "gh-5756-1.5",
    [[
        SELECT 9 + x'32';
    ]], {
        1, "Type mismatch: can not convert varbinary(x'32') to number"
    })

test:do_catchsql_test(
    "gh-5756-1.6",
    [[
        SELECT 9 + true;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to number"
    })

test:do_execsql_test(
    "gh-5756-2.1",
    [[
        SELECT 9 - 2;
    ]], {
        7
    })

test:do_execsql_test(
    "gh-5756-2.2",
    [[
        SELECT 9 - -2;
    ]], {
        11
    })

test:do_execsql_test(
    "gh-5756-2.3",
    [[
        SELECT 9 - 2.0;
    ]], {
        7
    })

test:do_catchsql_test(
    "gh-5756-2.4",
    [[
        SELECT 9 - '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to number"
    })

test:do_catchsql_test(
    "gh-5756-2.5",
    [[
        SELECT 9 - x'32';
    ]], {
        1, "Type mismatch: can not convert varbinary(x'32') to number"
    })

test:do_catchsql_test(
    "gh-5756-2.6",
    [[
        SELECT 9 - true;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to number"
    })

test:do_execsql_test(
    "gh-5756-3.1",
    [[
        SELECT 9 * 2;
    ]], {
        18
    })

test:do_execsql_test(
    "gh-5756-3.2",
    [[
        SELECT 9 * -2;
    ]], {
        -18
    })

test:do_execsql_test(
    "gh-5756-3.3",
    [[
        SELECT 9 * 2.0;
    ]], {
        18
    })

test:do_catchsql_test(
    "gh-5756-3.4",
    [[
        SELECT 9 * '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to number"
    })

test:do_catchsql_test(
    "gh-5756-3.5",
    [[
        SELECT 9 * x'32';
    ]], {
        1, "Type mismatch: can not convert varbinary(x'32') to number"
    })

test:do_catchsql_test(
    "gh-5756-3.6",
    [[
        SELECT 9 * true;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to number"
    })

test:do_execsql_test(
    "gh-5756-4.1",
    [[
        SELECT 9 / 2;
    ]], {
        4
    })

test:do_execsql_test(
    "gh-5756-4.2",
    [[
        SELECT 9 / -2;
    ]], {
        -4
    })

test:do_execsql_test(
    "gh-5756-4.3",
    [[
        SELECT 9 / 2.0;
    ]], {
        4.5
    })

test:do_catchsql_test(
    "gh-5756-4.4",
    [[
        SELECT 9 / '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to number"
    })

test:do_catchsql_test(
    "gh-5756-4.5",
    [[
        SELECT 9 / x'32';
    ]], {
        1, "Type mismatch: can not convert varbinary(x'32') to number"
    })

test:do_catchsql_test(
    "gh-5756-4.6",
    [[
        SELECT 9 / true;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to number"
    })

test:do_execsql_test(
    "gh-5756-5.1",
    [[
        SELECT 9 % 2;
    ]], {
        1
    })

test:do_execsql_test(
    "gh-5756-5.2",
    [[
        SELECT 9 % -2;
    ]], {
        1
    })

test:do_catchsql_test(
    "gh-5756-5.3",
    [[
        SELECT 9 % 2.0;
    ]], {
        1, "Type mismatch: can not convert double(2.0) to integer"
    })

test:do_catchsql_test(
    "gh-5756-5.4",
    [[
        SELECT 9 % '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to integer"
    })

test:do_catchsql_test(
    "gh-5756-5.5",
    [[
        SELECT 9 % x'32';
    ]], {
        1, "Type mismatch: can not convert varbinary(x'32') to integer"
    })

test:do_catchsql_test(
    "gh-5756-5.6",
    [[
        SELECT 9 % true;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to integer"
    })

test:finish_test()
