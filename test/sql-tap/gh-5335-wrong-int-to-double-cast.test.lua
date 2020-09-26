#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(2)

test:execsql([[
    CREATE TABLE t1 (i NUMBER PRIMARY KEY, n NUMBER);
    CREATE TABLE t2 (i NUMBER PRIMARY KEY, n NUMBER);
    -- This trigger is only needed to reproduce the error.
    CREATE TRIGGER r AFTER INSERT ON t1 FOR EACH ROW BEGIN UPDATE t1 SET n = new.n; END;
    INSERT INTO t1 VALUES (1, 1);
    INSERT INTO t2 VALUES (1, 1);
]])

--
-- Make sure that there are no unnecesary INTEGER to DOUBLE implicit cast in
-- field of type NUMBER.
--
test:do_execsql_test(
    "gh-5335-1",
    [[
        SELECT i / 2, n / 2 FROM t1;
    ]], {
        0, 0
    })

test:do_execsql_test(
    "gh-5335-2",
    [[
        SELECT i / 2, n / 2 FROM t2 GROUP BY n;
    ]], {
        0, 0
    })

test:execsql([[
    DROP TRIGGER r;
    DROP TABLE t1;
    DROP TABLE t2;
]])

test:finish_test()
