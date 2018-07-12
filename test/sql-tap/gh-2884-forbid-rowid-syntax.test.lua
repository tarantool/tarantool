#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(1)

local ok = pcall(test.execsql, test, [[
    DROP TABLE IF EXISTS t1;
    CREATE TABLE t1(a INT, b INT, c INT, d TEXT, PRIMARY KEY (c, d)) WITHOUT ROWID;
]])

test:ok(not ok, 'rowid syntax must be forbidden')

test:finish_test()
