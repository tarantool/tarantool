#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(3)

--
-- Make sure that a blob as part of a tuple can be cast to NUMBER,
-- INTEGER and UNSIGNED. Prior to this patch, an error could
-- appear due to the absence of '\0' at the end of the BLOB.
--
test:do_execsql_test(
    "gh-4766-1",
    [[
        CREATE TABLE t1 (a VARBINARY PRIMARY KEY);
        INSERT INTO t1 VALUES (X'33'), (X'372020202020');
        SELECT a, CAST(a AS NUMBER), CAST(a AS INTEGER), CAST(a AS UNSIGNED) FROM t1;
    ]], {
        '3', 3, 3, 3, '7     ', 7, 7, 7
    })

--
-- Make sure that BLOB longer than 12287 bytes cannot be cast to
-- INTEGER.
--
local long_str = string.rep('0', 12284)
test:do_execsql_test(
    "gh-4766-2",
    "SELECT CAST('" .. long_str .. "123'" .. " AS INTEGER);", {
        123
    })


test:do_catchsql_test(
    "gh-4766-3",
    "SELECT CAST('" .. long_str .. "1234'" .. " AS INTEGER);", {
        1, "Type mismatch: can not convert string('0000000000000000000000000" ..
        "0000000000000000000000000000000000000000000000000000000000000000000" ..
        "000000000000000000000000000000000000...) to integer"
    })

test:finish_test()
