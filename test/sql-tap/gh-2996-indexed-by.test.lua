#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(13)

-- gh-2996 - INDEXED BY clause wasn't working.
-- This functional test ensures that execution of that type of
-- statement is correct.

test:execsql [[
    CREATE TABLE t1(a INT PRIMARY KEY, b INT);
    CREATE INDEX t1ix2 on t1(b);
    CREATE INDEX t1ix1 on t1(b);
]]

local sample_size = 1000
local query = "INSERT INTO t1 VALUES "

for i = 1, sample_size do
    query = query  .. "(" .. i .. ", " .. i .. ")"
    if (i ~= sample_size) then
        query = query .. ","
    end
end

-- Fill our space with data
test:execsql(query)

test:do_execsql_test(
    "indexed-by-1.0",
    "SELECT b FROM t1 INDEXED BY t1ix1 WHERE b <= 5", {
        -- <indexed-by-1.0>
        1, 2, 3, 4, 5
    })

-- Make sure that SELECT works correctly when index exists.
test:do_eqp_test(
    "indexed-by-1.1",
    "SELECT b FROM t1 WHERE b <= 5", {
        -- <indexed-by-1.1>
        { 0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1IX2 (B<?) (~262144 rows)' }
        -- <indexed-by-1.1>
    })

test:do_eqp_test(
    "indexed-by-1.2",
    "SELECT b FROM t1 INDEXED BY t1ix1 WHERE b <= 5", {
        -- <indexed-by-1.2>
        { 0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1IX1 (B<?) (~262144 rows)' }
        -- <indexed-by-1.2>
    })

test:execsql [[
    DROP INDEX t1ix1 ON t1;
    DROP INDEX t1ix2 ON t1;
]]

-- Now make sure that when schema was changed (t1ix1 was dropped),
-- SELECT statement won't work.
test:do_catchsql_test(
    "indexed-by-1.3",
    "SELECT b FROM t1 INDEXED BY t1ix1 WHERE b <= 5", {
        -- <indexed-by-1.3>
        1, "No index 'T1IX1' is defined in space 'T1'"
        -- <indexed-by-1.3>
    })

test:do_catchsql_test(
    "indexed-by-1.4",
    "SELECT b FROM t1 INDEXED BY t1ix2 WHERE b <= 5", {
        -- <indexed-by-1.4>
        1, "No index 'T1IX2' is defined in space 'T1'"
        -- <indexed-by-1.4>
    })

-- Make sure that DELETE statement works correctly with INDEXED BY.
test:execsql [[
    CREATE INDEX t1ix2 ON t1(b);
    CREATE INDEX t1ix1 on t1(b);
]]

test:do_eqp_test(
    "indexed-by-1.5",
    "DELETE FROM t1 WHERE b <= 5", {
        -- <indexed-by-1.5>
        { 0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1IX2 (B<?) (~262144 rows)' }
        -- <indexed-by-1.5>
    })

test:do_eqp_test(
    "indexed-by-1.6",
    "DELETE FROM t1 INDEXED BY t1ix1  WHERE b <= 5", {
        -- <indexed-by-1.6>
        { 0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1IX1 (B<?) (~262144 rows)' }
        -- <indexed-by-1.6>
    })

test:execsql [[
    DROP INDEX t1ix1 ON t1;
    DROP INDEX t1ix2 ON t1;
]]

test:do_catchsql_test(
    "indexed-by-1.7",
    "DELETE FROM t1 INDEXED BY t1ix1 WHERE b <= 5", {
        -- <indexed-by-1.7>
        1, "No index 'T1IX1' is defined in space 'T1'"
        -- <indexed-by-1.7>
    })

test:do_catchsql_test(
    "indexed-by-1.8",
    "DELETE FROM t1 INDEXED BY t1ix2 WHERE b <= 5", {
        -- <indexed-by-1.8>
        1, "No index 'T1IX2' is defined in space 'T1'"
        -- <indexed-by-1.8>
    })

test:execsql [[
   CREATE INDEX t1ix2 ON t1(b);
   CREATE INDEX t1ix1 ON t1(b);
]]

test:do_eqp_test(
    "indexed-by-1.9",
    "UPDATE t1 SET b = 20 WHERE b = 10", {
        -- <indexed-by-1.9>
        { 0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1IX2 (B=?) (~10 rows)' }
        -- <indexed-by-1.9>
    })

test:do_eqp_test(
    "indexed-by-1.10",
    "UPDATE t1 INDEXED BY t1ix1 SET b = 20 WHERE b = 10", {
        -- <indexed-by-1.10>
        { 0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1IX1 (B=?) (~10 rows)' }
        -- <indexed-by-1.10>
    })

test:execsql [[
    DROP INDEX t1ix1 ON t1;
    DROP INDEX t1ix2 ON t1;
]]

test:do_catchsql_test(
    "indexed-by-1.11",
    "UPDATE t1 INDEXED BY t1ix1 SET b = 20 WHERE b = 10", {
        -- <indexed-by-1.11>
        1, "No index 'T1IX1' is defined in space 'T1'"
        -- <indexed-by-1.11>
    })

test:do_catchsql_test(
    "indexed-by-1.12",
    "UPDATE t1 INDEXED BY t1ix2 SET b = 20 WHERE b = 10", {
        -- <indexed-by-1.12>
        1, "No index 'T1IX2' is defined in space 'T1'"
        -- <indexed-by-1.12>
    })

test:finish_test()

