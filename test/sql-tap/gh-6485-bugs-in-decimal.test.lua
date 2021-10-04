#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(5)

-- Make sure DECIMAL is not truncated when used in an index.
test:do_execsql_test(
    "gh-6485-1",
    [[
        CREATE TABLE t(i INTEGER PRIMARY KEY);
        INSERT INTO t VALUES(1), (2);
        SELECT i FROM t WHERE i IN (CAST(1.1 AS DECIMAL), CAST(2.2 AS DECIMAL));
        DROP TABLE t;
    ]], {
    })

--
-- Make sure that DECIMAL greater than -1 and less than 0 are correctly cast to
-- INTEGER.
--
test:do_execsql_test(
    "gh-6485-2",
    [[
        SELECT CAST(CAST(-0.5 AS DECIMAL) AS INTEGER);
    ]], {
        0
    })

test:do_execsql_test(
    "gh-6485-3",
    [[
        CREATE TABLE t(i INTEGER PRIMARY KEY);
        INSERT INTO t VALUES(1);
        SELECT i FROM t WHERE i IN (CAST(-0.1 AS DECIMAL), CAST(2 AS DECIMAL));
        DROP TABLE t;
    ]], {
    })

-- Make sure DECIMAL is not truncated when used in LIMIT and OFFSET.
test:do_catchsql_test(
    "gh-6485-4",
    [[
        SELECT 1 LIMIT CAST(1.5 AS DECIMAL);
    ]], {
        1, [[Failed to execute SQL statement: ]]..
        [[Only positive integers are allowed in the LIMIT clause]]
    })

test:do_catchsql_test(
    "gh-6485-5",
    [[
        SELECT 1 LIMIT 1 OFFSET CAST(1.5 AS DECIMAL);
    ]], {
        1, [[Failed to execute SQL statement: ]]..
        [[Only positive integers are allowed in the OFFSET clause]]
    })

test:finish_test()
