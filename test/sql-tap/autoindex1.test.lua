#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(8)

--
-- This file implements regression tests for sql library. The focus of this
-- script is testing ephemeral index creation logic.
--

test:execsql([[
    CREATE TABLE t1(a INT, b INT PRIMARY KEY);
    INSERT INTO t1 VALUES(1, 11);
    INSERT INTO t1 VALUES(2, 22);
    INSERT INTO t1 SELECT a + 2, b + 22 FROM t1;
    INSERT INTO t1 SELECT a + 4, b + 44 FROM t1;
    CREATE TABLE t2(c INT, d INT PRIMARY KEY);
]])

test:do_eqp_test(
    "autoindex-1.0", [[
       SELECT b, (SELECT d FROM t2 WHERE c = a) FROM t1;
    ]], {
        {0,0,0,"SCAN TABLE T1 (~1048576 rows)"},
        {0,0,0,"EXECUTE CORRELATED SCALAR SUBQUERY 1"},
        {1,0,0,"SCAN TABLE T2 (~262144 rows)"}
    })

for i = 1, 10240 do test:execsql("INSERT INTO t2 VALUES ("..i..", "..i..");") end

test:do_eqp_test(
    "autoindex-1.1", [[
        SELECT b, (SELECT d FROM t2 WHERE c = a) FROM t1;
    ]], {
        {0,0,0,"SCAN TABLE T1 (~1048576 rows)"},
        {0,0,0,"EXECUTE CORRELATED SCALAR SUBQUERY 1"},
        {1,0,0,"SEARCH TABLE T2 USING EPHEMERAL INDEX (C=?) (~20 rows)"}
    })

local result = test:execsql([[SELECT b, (SELECT d FROM t2 WHERE c = a) FROM t1;]])

test:do_eqp_test(
    "autoindex-1.2", [[
        SELECT b, d FROM t1 JOIN t2 ON a = c ORDER BY b;
    ]], {
        {0,0,0,"SCAN TABLE T1 (~1048576 rows)"},
        {0,1,1,"SEARCH TABLE T2 USING EPHEMERAL INDEX (C=?) (~20 rows)"}
    })

test:do_execsql_test(
    "autoindex-1.3", [[
        SELECT b, d FROM t1 JOIN t2 ON a = c ORDER BY b;
    ]], result)

test:do_eqp_test(
    "autoindex-1.4", [[
        SELECT b, d FROM t1 CROSS JOIN t2 ON (c = a);
    ]], {
        {0,0,0,"SCAN TABLE T1 (~1048576 rows)"},
        {0,1,1,"SEARCH TABLE T2 USING EPHEMERAL INDEX (C=?) (~20 rows)"}
    })

test:do_execsql_test(
    "autoindex-1.5", [[
        SELECT b, d FROM t1 CROSS JOIN t2 ON (c = a);
    ]], result)

test:execsql([[
    CREATE TABLE t3(i INT PRIMARY KEY, a INT, b INT);
]])

--
-- This query is quite slow in case ephemeral index is not used. The main idea
-- behind this test is that ephemeral index allows to use index instead of
-- fullscan in cases below. In this test construction of the index is faster
-- that fullscan.
--
for i = 1, 10240 do test:execsql("INSERT INTO t3 VALUES ("..i..", "..i..", "..(i + 1)..");") end

test:do_execsql_test(
    "autoindex-1.6", [[
        SELECT count(*)
          FROM t3 AS x1
          JOIN t3 AS x2 ON x2.a=x1.b
          JOIN t3 AS x3 ON x3.a=x2.b
          JOIN t3 AS x4 ON x4.a=x3.b
          JOIN t3 AS x5 ON x5.a=x4.b
          JOIN t3 AS x6 ON x6.a=x5.b
          JOIN t3 AS x7 ON x7.a=x6.b
          JOIN t3 AS x8 ON x8.a=x7.b
          JOIN t3 AS x9 ON x9.a=x8.b
          JOIN t3 AS x10 ON x10.a=x9.b;
    ]], {
        10231
    })

test:do_eqp_test(
    "autoindex-1.7", [[
        SELECT count(*)
          FROM t3 AS x1
          JOIN t3 AS x2 ON x2.a=x1.b
          JOIN t3 AS x3 ON x3.a=x2.b
          JOIN t3 AS x4 ON x4.a=x3.b
          JOIN t3 AS x5 ON x5.a=x4.b
          JOIN t3 AS x6 ON x6.a=x5.b
          JOIN t3 AS x7 ON x7.a=x6.b
          JOIN t3 AS x8 ON x8.a=x7.b
          JOIN t3 AS x9 ON x9.a=x8.b
          JOIN t3 AS x10 ON x10.a=x9.b;
    ]], {
        {0,0,0,"SCAN TABLE T3 AS X1 (~1048576 rows)"},
        {0,1,1,"SEARCH TABLE T3 AS X2 USING EPHEMERAL INDEX (A=?) (~20 rows)"},
        {0,2,2,"SEARCH TABLE T3 AS X3 USING EPHEMERAL INDEX (A=?) (~20 rows)"},
        {0,3,3,"SEARCH TABLE T3 AS X4 USING EPHEMERAL INDEX (A=?) (~20 rows)"},
        {0,4,4,"SEARCH TABLE T3 AS X5 USING EPHEMERAL INDEX (A=?) (~20 rows)"},
        {0,5,5,"SEARCH TABLE T3 AS X6 USING EPHEMERAL INDEX (A=?) (~20 rows)"},
        {0,6,6,"SEARCH TABLE T3 AS X7 USING EPHEMERAL INDEX (A=?) (~20 rows)"},
        {0,7,7,"SEARCH TABLE T3 AS X8 USING EPHEMERAL INDEX (A=?) (~20 rows)"},
        {0,8,8,"SEARCH TABLE T3 AS X9 USING EPHEMERAL INDEX (A=?) (~20 rows)"},
        {0,9,9,"SEARCH TABLE T3 AS X10 USING EPHEMERAL INDEX (A=?) (~20 rows)"}
    })

test:finish_test()
