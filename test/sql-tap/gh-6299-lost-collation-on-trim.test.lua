#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(4)

--
-- Make sure that collation is not lost when TRIM called with BOTH, LEADING, or
-- TRAILING keywords specified.
--

test:execsql[[
    CREATE TABLE t (i INT PRIMARY KEY, s STRING COLLATE "unicode_ci");
    INSERT INTO t VALUES (1,'A'), (2,'a');
]]

test:do_execsql_test(
    "gh-6299-2",
    [[
        SELECT DISTINCT TRIM(LEADING FROM s) FROM t;
    ]], {
        'A'
    })

test:do_execsql_test(
    "gh-6299-3",
    [[
        SELECT DISTINCT TRIM(TRAILING FROM s) FROM t;
    ]], {
        'A'
    })

test:do_execsql_test(
    "gh-6299-4",
    [[
        SELECT DISTINCT TRIM(BOTH FROM s) FROM t;
    ]], {
        'A'
    })

test:do_execsql_test(
    "gh-6299-1",
    [[
        SELECT DISTINCT TRIM(s) FROM t;
    ]], {
        'A'
    })

test:finish_test()
