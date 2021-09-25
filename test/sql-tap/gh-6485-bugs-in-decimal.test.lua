#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(1)

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

test:finish_test()
