#!/usr/bin/env tarantool

local test = require("sqltester")
test:plan(6)

test:execsql([[
    CREATE TABLE t1(a INT PRIMARY KEY, b INT UNIQUE, c INT, d VARCHAR(100));
    INSERT INTO t1 VALUES (1, 1, 1, 'abcd'), (2, 2, 2, 'abcde');
    INSERT INTO t1 VALUES (3, 3, 3, 'abcdef'), (4, 4, 4, 'abcdefg');
    CREATE INDEX a ON t1(a);
    CREATE INDEX abc ON t1(a, b, c);
    CREATE INDEX cba ON t1(c, b, a);
    CREATE INDEX d ON t1(d);
]])

-- Case: old index_xinfo pragma is banned.
test:do_catchsql_test(
    "index-info-1.1",
    "PRAGMA index_xinfo (t1.a);",
    {
        1, "Pragma 'INDEX_XINFO' does not exist",
    })

-- Case: old index_info syntax is banned.
test:do_catchsql_test(
    "index-info-1.2",
    "PRAGMA index_info = t1.a;",
    {
        1, "Syntax error at line 1 near '='",
    })

-- Case: single column index with an integer column.
test:do_execsql_test(
    "index-info-1.3",
    "PRAGMA index_info (t1.a);",
    {
        0, 0, 'A', 0, 'BINARY', 'integer',
    })

-- Case: multiple columns index with integer columns.
test:do_execsql_test(
    "index-info-1.4",
    "PRAGMA index_info (t1.abc);",
    {
        0, 0, 'A', 0, 'BINARY', 'integer',
        1, 1, 'B', 0, 'BINARY', 'integer',
        2, 2, 'C', 0, 'BINARY', 'integer',
    })

-- Case: multiple columns, reverse columns order.
test:do_execsql_test(
    "index-info-1.5",
    "PRAGMA index_info (t1.cba);",
    {
        0, 2, 'C', 0, 'BINARY', 'integer',
        1, 1, 'B', 0, 'BINARY', 'integer',
        2, 0, 'A', 0, 'BINARY', 'integer',
    })

-- Case: index with a string column.
test:do_execsql_test(
    "index-info-1.6",
    "PRAGMA index_info (t1.d);",
    {
        0, 3, 'D', 0, 'BINARY', 'string',
    })

test:finish_test()
