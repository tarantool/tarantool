#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(18)

testprefix = "analyzeF"

--!./tcltestrunner.lua
-- 2015-03-12
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- Test that deterministic scalar functions passed constant arguments
-- are used with stat4 data.

local function isqrt(i)
    return math.floor(math.sqrt(i))
end

box.internal.sql_create_function("isqrt", isqrt)

test:do_execsql_test(
    1.0,
    [[
    	DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(id PRIMARY KEY, x INTEGER, y INTEGER);
        WITH data(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM data) INSERT INTO t1 SELECT i, isqrt(i), isqrt(i) FROM data LIMIT 500;
        CREATE INDEX t1y ON t1(y);
        CREATE INDEX t1x ON t1(x);
        ANALYZE;
    ]])

-- Note: tests 7 to 12 might be unstable - as they assume SQLite will
-- prefer the expression to the right of the AND clause. Which of
-- course could change.
--
-- Note 2: tests 9 and 10 depend on the tcl interface creating functions
-- without the SQLITE_DETERMINISTIC flag set.
--

where_clauses_x = {"x = 4 AND y = 19", "x = '4' AND y = '19'", 
	"x = substr('145', 2, 1) AND y = substr('5195', 2, 2)"}

where_clauses_y = {"x = 19 AND y = 4", "x = '19' AND y = '4'", 
	"x = substr('5195', 2, 2) AND y = substr('145', 2, 1)",
	"x = substr('5195', 2, 2+0) AND y = substr('145', 2, 1+0)",
	"x = substr('145', 2, 1+0) AND y = substr('5195', 2, 2+0)",
	"x = '19' AND y = '4'",
	"x = nullif('19', 0) AND y = nullif('4', 0)",
	"x = nullif('4', 0) AND y = nullif('19', 0)"}


for test_number, where in ipairs(where_clauses_x) do
    res = {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1X (X=?)"}
    test:do_eqp_test(
        "1.1."..test_number,
        "SELECT * FROM t1 WHERE "..where.."", {
            res
        })

end


for test_number, where in ipairs(where_clauses_y) do
    res = {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1Y (Y=?)"}
    test:do_eqp_test(
        "1.2."..test_number,
        "SELECT * FROM t1 WHERE "..where.."", {
            res
        })
end

-- Test that functions that do not exist - "func()" - do not cause an error.
--
test:do_catchsql_test(
    2.1,
    [[
        SELECT * FROM t1 WHERE x = substr('145', 2, 1) AND y = func(1, 2, 3);
    ]], {
        -- <2.1>
        1, "no such function: FUNC"
        -- </2.1>
    })

test:do_catchsql_test(
    2.2,
    [[
        UPDATE t1 SET y=y+1 WHERE x = substr('145', 2, 1) AND y = func(1, 2, 3)
    ]], {
        -- <2.2>
        1, "no such function: FUNC"
        -- </2.2>
    })

-- Check that functions that accept zero arguments do not cause problems.
--

local function det4() 
    return 4
end

local function det19()
    return 19
end


box.internal.sql_create_function("det4", det4)

box.internal.sql_create_function("det19", det19)

where_clause_x = {"x = det4() AND y = det19()"}
where_clauses_y = {"x = det19() AND y = det4()"}

for test_number, where in ipairs(where_clauses_y) do
    res = {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1Y (Y=?)"}
    test:do_eqp_test(
        "3.1."..test_number,
        "SELECT * FROM t1 WHERE "..where.."", {
            res
        })
end

for test_number, where in ipairs(where_clauses_x) do
    res = {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1X (X=?)"}
    test:do_eqp_test(
        "3.2."..test_number,
        "SELECT * FROM t1 WHERE "..where.."", {
            res
        })
end


test:finish_test()
