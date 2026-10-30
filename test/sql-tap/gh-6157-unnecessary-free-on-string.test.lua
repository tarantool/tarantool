#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(1)

--
-- Make sure there is no assert due to an incorrectly set error in mem_copy().
-- How this test works: We have 128 mempool cells in SQL ("lookaside"), and
-- until those 128 cells are filled in, the error cannot be reproduced. Also, we
-- have to get '' from somewhere because if we just enter it, it will be of type
-- STATIC and no memory will be allocated.
--
local query = "NULLIF(SUBSTR('123', 1, 0), NULL)"
for _ = 1, 7 do query = query..', '..query end
query = "SELECT "..query..", NULLIF(SUBSTR('123', 1, 0), NULL);"
-- The "query" variable contains 129 expressions.
local result = {}
for _ = 1, 129 do table.insert(result, '') end

test:do_execsql_test("gh-6157", query, result)

test:finish_test()
