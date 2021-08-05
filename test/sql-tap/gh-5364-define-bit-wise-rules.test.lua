#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(24)

--
-- Make sure that bit-wise operations can only accept UNSIGNED and posivite
-- INTEGER values.
--
test:do_execsql_test(
    "gh-5364-1.1",
    [[
        SELECT 9 >> 2;
    ]], {
        2
    })

test:do_catchsql_test(
    "gh-5364-1.2",
    [[
        SELECT 9 >> -2;
    ]], {
        1, "Type mismatch: can not convert integer(-2) to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-1.3",
    [[
        SELECT 9 >> 2.0;
    ]], {
        1, "Type mismatch: can not convert double(2.0) to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-1.4",
    [[
        SELECT 9 >> '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-1.5",
    [[
        SELECT 9 >> x'32';
    ]], {
        1, "Type mismatch: can not convert varbinary(x'32') to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-1.6",
    [[
        SELECT 9 >> true;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to unsigned"
    })

test:do_execsql_test(
    "gh-5364-2.1",
    [[
        SELECT 9 << 2;
    ]], {
        36
    })

test:do_catchsql_test(
    "gh-5364-2.2",
    [[
        SELECT 9 << -2;
    ]], {
        1, "Type mismatch: can not convert integer(-2) to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-2.3",
    [[
        SELECT 9 << 2.0;
    ]], {
        1, "Type mismatch: can not convert double(2.0) to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-2.4",
    [[
        SELECT 9 << '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-2.5",
    [[
        SELECT 9 << x'32';
    ]], {
        1, "Type mismatch: can not convert varbinary(x'32') to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-2.6",
    [[
        SELECT 9 << true;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to unsigned"
    })

test:do_execsql_test(
    "gh-5364-3.1",
    [[
        SELECT 9 & 2;
    ]], {
        0
    })

test:do_catchsql_test(
    "gh-5364-3.2",
    [[
        SELECT 9 & -2;
    ]], {
        1, "Type mismatch: can not convert integer(-2) to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-3.3",
    [[
        SELECT 9 & 2.0;
    ]], {
        1, "Type mismatch: can not convert double(2.0) to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-3.4",
    [[
        SELECT 9 & '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-3.5",
    [[
        SELECT 9 & x'32';
    ]], {
        1, "Type mismatch: can not convert varbinary(x'32') to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-3.6",
    [[
        SELECT 9 & true;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to unsigned"
    })

test:do_execsql_test(
    "gh-5364-4.1",
    [[
        SELECT 9 | 2;
    ]], {
        11
    })

test:do_catchsql_test(
    "gh-5364-4.2",
    [[
        SELECT 9 | -2;
    ]], {
        1, "Type mismatch: can not convert integer(-2) to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-4.3",
    [[
        SELECT 9 | 2.0;
    ]], {
        1, "Type mismatch: can not convert double(2.0) to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-4.4",
    [[
        SELECT 9 | '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-4.5",
    [[
        SELECT 9 | x'32';
    ]], {
        1, "Type mismatch: can not convert varbinary(x'32') to unsigned"
    })

test:do_catchsql_test(
    "gh-5364-4.6",
    [[
        SELECT 9 | true;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to unsigned"
    })

test:finish_test()
