#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(1)

-- Check that OP_NextIdEphemeral generates unique ids.
--
test:execsql [[
    CREATE TABLE T1(A INT PRIMARY KEY);
    CREATE TABLE T2(A INT PRIMARY KEY, B INT);
    INSERT INTO T1 VALUES(12);
    INSERT INTO T2 VALUES(1, 5);
    INSERT INTO T2 VALUES(2, 2);
    INSERT INTO T2 VALUES(3, 2);
    INSERT INTO T2 VALUES(4, 2);
]]

test:do_execsql_test(
    "gh-3297-1",
    [[
        SELECT * FROM ( SELECT A FROM T1 LIMIT 1), (SELECT B FROM T2 LIMIT 10);
    ]],
    {
        12, 2,
        12, 2,
        12, 2,
        12, 5
    }
)

test:finish_test()
