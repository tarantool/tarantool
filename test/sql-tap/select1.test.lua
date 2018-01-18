#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(173)

--!./tcltestrunner.lua
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Try to select on a non-existant table.
--
test:do_catchsql_test(
    "select1-1.1",
    [[
        SELECT * FROM test1
    ]], {
        -- <select1-1.1>
        1, "no such table: TEST1"
        -- </select1-1.1>
    })

test:execsql "DROP TABLE IF EXISTS test1"
test:execsql "CREATE TABLE test1(f1 int, f2 int, PRIMARY KEY(f1))"
test:do_catchsql_test(
    "select1-1.2",
    [[
        SELECT * FROM test1, test2
    ]], {
        -- <select1-1.2>
        1, "no such table: TEST2"
        -- </select1-1.2>
    })

test:do_catchsql_test(
    "select1-1.3",
    [[
        SELECT * FROM test2, test1
    ]], {
        -- <select1-1.3>
        1, "no such table: TEST2"
        -- </select1-1.3>
    })

test:execsql "INSERT INTO test1(f1,f2) VALUES(11,22)"
-- Make sure the columns are extracted correctly.
--
test:do_execsql_test(
    "select1-1.4",
    [[
        SELECT f1 FROM test1
    ]], {
        -- <select1-1.4>
        11
        -- </select1-1.4>
    })

test:do_execsql_test(
    "select1-1.5",
    [[
        SELECT f2 FROM test1
    ]], {
        -- <select1-1.5>
        22
        -- </select1-1.5>
    })

test:do_execsql_test(
    "select1-1.6",
    [[
        SELECT f2, f1 FROM test1
    ]], {
        -- <select1-1.6>
        22, 11
        -- </select1-1.6>
    })

test:do_execsql_test(
    "select1-1.7",
    [[
        SELECT f1, f2 FROM test1
    ]], {
        -- <select1-1.7>
        11, 22
        -- </select1-1.7>
    })

test:do_execsql_test(
    "select1-1.8",
    [[
        SELECT * FROM test1
    ]], {
        -- <select1-1.8>
        11, 22
        -- </select1-1.8>
    })

test:do_execsql_test(
    "select1-1.8.1",
    [[
        SELECT *, * FROM test1
    ]], {
        -- <select1-1.8.1>
        11, 22, 11, 22
        -- </select1-1.8.1>
    })

test:do_execsql_test(
    "select1-1.8.2",
    [[
        SELECT *, min(f1,f2), max(f1,f2) FROM test1
    ]], {
        -- <select1-1.8.2>
        11, 22, 11, 22
        -- </select1-1.8.2>
    })

test:do_execsql_test(
    "select1-1.8.3",
    [[
        SELECT 'one', *, 'two', * FROM test1
    ]], {
        -- <select1-1.8.3>
        "one", 11, 22, "two", 11, 22
        -- </select1-1.8.3>
    })

test:execsql "DROP TABLE IF EXISTS test2"
test:execsql "CREATE TABLE test2(r1 int, r2 int, PRIMARY KEY(r1))"
test:execsql "INSERT INTO test2(r1,r2) VALUES(1,2)"
test:do_execsql_test(
    "select1-1.9",
    [[
        SELECT * FROM test1, test2
    ]], {
        -- <select1-1.9>
        11, 22, 1, 2
        -- </select1-1.9>
    })

test:do_execsql_test(
    "select1-1.9.1",
    [[
        SELECT *, 'hi' FROM test1, test2
    ]], {
        -- <select1-1.9.1>
        11, 22, 1, 2, "hi"
        -- </select1-1.9.1>
    })

test:do_execsql_test(
    "select1-1.9.2",
    [[
        SELECT 'one', *, 'two', * FROM test1, test2
    ]], {
        -- <select1-1.9.2>
        "one", 11, 22, 1, 2, "two", 11, 22, 1, 2
        -- </select1-1.9.2>
    })

test:do_execsql_test(
    "select1-1.10",
    [[
        SELECT test1.f1, test2.r1 FROM test1, test2
    ]], {
        -- <select1-1.10>
        11, 1
        -- </select1-1.10>
    })

test:do_execsql_test(
    "select1-1.11.0",
    [[
        SELECT test1.f1, test2.r1 FROM test2, test1
    ]], {
        -- <select1-1.11.0>
        11, 1
        -- </select1-1.11.0>
    })

test:do_execsql_test(
    "select1-1.11.0.1",
    [[
        SELECT * FROM test2, test1
    ]], {
        -- <select1-1.11.0.1>
        1, 2, 11, 22
        -- </select1-1.11.0.1>
    })

test:do_execsql_test(
    "select1-1.11.0.2",
    [[
        SELECT * FROM test1 AS a, test1 AS b
    ]], {
        -- <select1-1.11.0.2>
        11, 22, 11, 22
        -- </select1-1.11.0.2>
    })

test:do_execsql_test(
    "select1-1.12",
    [[SELECT max(test1.f1,test2.r1), min(test1.f2,test2.r2)
           FROM test2, test1]], {
        -- <select1-1.12>
        11, 2
        -- </select1-1.12>
    })

test:do_execsql_test(
    "select1-1.13",
    [[SELECT min(test1.f1,test2.r1), max(test1.f2,test2.r2)
           FROM test1, test2]], {
        -- <select1-1.13>
        1, 22
        -- </select1-1.13>
    })

long = "This is a string that is too big to fit inside a NBFS buffer"
test:do_execsql_test(
    "select1-2.0",
string.format([[
        DROP TABLE test2;
        DELETE FROM test1;
        INSERT INTO test1 VALUES(11,22);
        INSERT INTO test1 VALUES(33,44);
        DROP TABLE IF EXISTS t3;
        CREATE TABLE t3(id INT, a, b, PRIMARY KEY(id));
        INSERT INTO t3 VALUES(1, 'abc',NULL);
        INSERT INTO t3 VALUES(2, NULL,'xyz');
        INSERT INTO t3 SELECT f1, * FROM test1;
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t4(id INT, a, b, PRIMARY KEY(id));
        INSERT INTO t4 VALUES(1, NULL,'%s');
        SELECT * FROM t3;
    ]], long), {
        -- <select1-2.0>
        1, "abc", "", 2, "", "xyz", 11, 11, 22, 33, 33, 44
        -- </select1-2.0>
    })

-- Error messges from sqliteExprCheck
--
test:do_catchsql_test(
    "select1-2.1",
    [[
        SELECT count(f1,f2) FROM test1
    ]], {
        -- <select1-2.1>
        1, "wrong number of arguments to function COUNT()"
        -- </select1-2.1>
    })

test:do_catchsql_test(
    "select1-2.2",
    [[
        SELECT count(f1) FROM test1
    ]], {
        -- <select1-2.2>
        0, {2}
        -- </select1-2.2>
    })

test:do_catchsql_test(
    "select1-2.3",
    [[
        SELECT Count() FROM test1
    ]], {
        -- <select1-2.3>
        0, {2}
        -- </select1-2.3>
    })

test:do_catchsql_test(
    "select1-2.4",
    [[
        SELECT COUNT(*) FROM test1
    ]], {
        -- <select1-2.4>
        0, {2}
        -- </select1-2.4>
    })

test:do_catchsql_test(
    "select1-2.5",
    [[
        SELECT COUNT(*)+1 FROM test1
    ]], {
        -- <select1-2.5>
        0, {3}
        -- </select1-2.5>
    })

test:do_execsql_test(
    "select1-2.5.1",
    [[
        SELECT count(*),count(a),count(b) FROM t3
    ]], {
        -- <select1-2.5.1>
        4, 3, 3
        -- </select1-2.5.1>
    })

test:do_execsql_test(
    "select1-2.5.2",
    [[
        SELECT count(*),count(a),count(b) FROM t4
    ]], {
        -- <select1-2.5.2>
        1, 0, 1
        -- </select1-2.5.2>
    })

test:do_execsql_test(
    "select1-2.5.3",
    [[
        SELECT count(*),count(a),count(b) FROM t4 WHERE b=5
    ]], {
        -- <select1-2.5.3>
        0, 0, 0
        -- </select1-2.5.3>
    })

test:do_catchsql_test(
    "select1-2.6",
    [[
        SELECT min(*) FROM test1
    ]], {
        -- <select1-2.6>
        1, "wrong number of arguments to function MIN()"
        -- </select1-2.6>
    })

test:do_catchsql_test(
    "select1-2.7",
    [[
        SELECT Min(f1) FROM test1
    ]], {
        -- <select1-2.7>
        0, {11}
        -- </select1-2.7>
    })

test:do_test(
    "select1-2.8",
    function()
        local msg
        local v = pcall(function()
            msg = test:execsql "SELECT MIN(f1,f2) FROM test1"
            end)
        v = v == true and {0} or {1} 
        return table.insert(v,table.sort(msg) or msg) or v
    end, {
        -- <select1-2.8>
        0, {11, 33}
        -- </select1-2.8>
    })

test:do_execsql_test(
    "select1-2.8.1",
    [[
        SELECT coalesce(min(a),'xyzzy') FROM t3
    ]], {
        -- <select1-2.8.1>
        11
        -- </select1-2.8.1>
    })

test:do_execsql_test(
    "select1-2.8.2",
    [[
        SELECT min(coalesce(a,'xyzzy')) FROM t3
    ]], {
        -- <select1-2.8.2>
        11
        -- </select1-2.8.2>
    })

test:do_execsql_test(
    "select1-2.8.3",
    [[
        SELECT min(b), min(b) FROM t4
    ]], {
        -- <select1-2.8.3>
        long, long
        -- </select1-2.8.3>
    })

test:do_catchsql_test(
    "select1-2.9",
    [[
        SELECT MAX(*) FROM test1
    ]], {
        -- <select1-2.9>
        1, "wrong number of arguments to function MAX()"
        -- </select1-2.9>
    })

test:do_catchsql_test(
    "select1-2.10",
    [[
        SELECT Max(f1) FROM test1
    ]], {
        -- <select1-2.10>
        0, {33}
        -- </select1-2.10>
    })

test:do_test(
    "select1-2.11",
    function()
        local msg
        local v = pcall(function()
            msg = test:execsql "SELECT max(f1,f2) FROM test1"
            end)
        v = v == true and {0} or {1} 
        return table.insert(v,table.sort(msg) or msg) or v
    end, {
        -- <select1-2.11>
        0, {22, 44}
        -- </select1-2.11>
    })

test:do_test(
    "select1-2.12",
    function()
        local msg
        local v = pcall(function()
            msg = test:execsql "SELECT MAX(f1,f2)+1 FROM test1"
            end)
        v = v == true and {0} or {1} 
        return table.insert(v,table.sort(msg) or msg) or v
    end, {
        -- <select1-2.12>
        0, {23, 45}
        -- </select1-2.12>
    })

test:do_catchsql_test(
    "select1-2.13",
    [[
        SELECT MAX(f1)+1 FROM test1
    ]], {
        -- <select1-2.13>
        0, {34}
        -- </select1-2.13>
    })

test:do_execsql_test(
    "select1-2.13.1",
    [[
        SELECT coalesce(max(a),'xyzzy') FROM t3
    ]], {
        -- <select1-2.13.1>
        "abc"
        -- </select1-2.13.1>
    })

test:do_execsql_test(
    "select1-2.13.2",
    [[
        SELECT max(coalesce(a,'xyzzy')) FROM t3
    ]], {
        -- <select1-2.13.2>
        "xyzzy"
        -- </select1-2.13.2>
    })

test:do_catchsql_test(
    "select1-2.14",
    [[
        SELECT SUM(*) FROM test1
    ]], {
        -- <select1-2.14>
        1, "wrong number of arguments to function SUM()"
        -- </select1-2.14>
    })

test:do_catchsql_test(
    "select1-2.15",
    [[
        SELECT Sum(f1) FROM test1
    ]], {
        -- <select1-2.15>
        0, {44}
        -- </select1-2.15>
    })

test:do_catchsql_test(
    "select1-2.16",
    [[
        SELECT sum(f1,f2) FROM test1
    ]], {
        -- <select1-2.16>
        1, "wrong number of arguments to function SUM()"
        -- </select1-2.16>
    })

test:do_catchsql_test(
    "select1-2.17",
    [[
        SELECT SUM(f1)+1 FROM test1
    ]], {
        -- <select1-2.17>
        0, {45}
        -- </select1-2.17>
    })

test:do_execsql_test(
    "select1-2.17.1",
    [[
        SELECT sum(a) FROM t3
    ]], {
        -- <select1-2.17.1>
        44.0
        -- </select1-2.17.1>
    })

test:do_catchsql_test(
    "select1-2.18",
    [[
        SELECT XYZZY(f1) FROM test1
    ]], {
        -- <select1-2.18>
        1, "no such function: XYZZY"
        -- </select1-2.18>
    })

test:do_catchsql_test(
    "select1-2.19",
    [[
        SELECT SUM(min(f1,f2)) FROM test1
    ]], {
        -- <select1-2.19>
        0, {44}
        -- </select1-2.19>
    })

test:do_catchsql_test(
    "select1-2.20",
    [[
        SELECT SUM(min(f1)) FROM test1
    ]], {
        -- <select1-2.20>
        1, "misuse of aggregate function MIN()"
        -- </select1-2.20>
    })

-- Ticket #2526
--
test:do_catchsql_test(
    "select1-2.21",
    [[
        SELECT min(f1) AS m FROM test1 GROUP BY f1 HAVING max(m+5)<10
    ]], {
        -- <select1-2.21>
        1, "misuse of aliased aggregate M"
        -- </select1-2.21>
    })

test:do_catchsql_test(
    "select1-2.22",
    [[
        SELECT coalesce(min(f1)+5,11) AS m FROM test1
         GROUP BY f1
        HAVING max(m+5)<10
    ]], {
        -- <select1-2.22>
        1, "misuse of aliased aggregate M"
        -- </select1-2.22>
    })

-- MUST_WORK_TEST
-- do_test select1-2.23 {
--   execsql {
--     CREATE TABLE tkt2526(a,b,c PRIMARY KEY);
--     INSERT INTO tkt2526 VALUES('x','y',NULL);
--     INSERT INTO tkt2526 VALUES('x','z',NULL);
--   }
--   catchsql {
--     SELECT count(a) AS cn FROM tkt2526 GROUP BY a HAVING cn<max(cn)
--   }
-- } {1 {misuse of aliased aggregate cn}}
-- WHERE clause expressions
--
test:do_catchsql_test(
    "select1-3.1",
    [[
        SELECT f1 FROM test1 WHERE f1<11
    ]], {
        -- <select1-3.1>
        0, {}
        -- </select1-3.1>
    })

test:do_catchsql_test(
    "select1-3.2",
    [[
        SELECT f1 FROM test1 WHERE f1<=11
    ]], {
        -- <select1-3.2>
        0, {11}
        -- </select1-3.2>
    })

test:do_catchsql_test(
    "select1-3.3",
    [[
        SELECT f1 FROM test1 WHERE f1=11
    ]], {
        -- <select1-3.3>
        0, {11}
        -- </select1-3.3>
    })

test:do_test(
    "select1-3.4",
    function()
        local msg
        local v = pcall(function()
            msg = test:execsql "SELECT f1 FROM test1 WHERE f1>=11"
            end)
        v = v == true and {0} or {1} 
        return table.insert(v,table.sort(msg) or msg) or v
    end, {
        -- <select1-3.4>
        0, {11, 33}
        -- </select1-3.4>
    })

test:do_test(
    "select1-3.5",
    function()
        local msg
        local v = pcall(function()
            msg = test:execsql "SELECT f1 FROM test1 WHERE f1>11"
            end)
        v = v == true and {0} or {1} 
        return table.insert(v,table.sort(msg) or msg) or v
    end, {
        -- <select1-3.5>
        0, {33}
        -- </select1-3.5>
    })

test:do_test(
    "select1-3.6",
    function()
        local msg
        local v = pcall(function()
            msg = test:execsql "SELECT f1 FROM test1 WHERE f1!=11"
            end)
        v = v == true and {0} or {1} 
        return table.insert(v,table.sort(msg) or msg) or v
    end, {
        -- <select1-3.6>
        0, {33}
        -- </select1-3.6>
    })

test:do_test(
    "select1-3.7",
    function()
        local msg
        local v = pcall(function()
            msg = test:execsql "SELECT f1 FROM test1 WHERE min(f1,f2)!=11"
            end)
        v = v == true and {0} or {1} 
        return table.insert(v,table.sort(msg) or msg) or v
    end, {
        -- <select1-3.7>
        0, {33}
        -- </select1-3.7>
    })

test:do_test(
    "select1-3.8",
    function()
        local msg
        local v = pcall(function()
            msg = test:execsql "SELECT f1 FROM test1 WHERE max(f1,f2)!=11"
            end)
        v = v == true and {0} or {1} 
        return table.insert(v,table.sort(msg) or msg) or v
    end, {
        -- <select1-3.8>
        0, {11, 33}
        -- </select1-3.8>
    })

test:do_catchsql_test(
    "select1-3.9",
    [[
        SELECT f1 FROM test1 WHERE count(f1,f2)!=11
    ]], {
        -- <select1-3.9>
        1, "wrong number of arguments to function COUNT()"
        -- </select1-3.9>
    })

-- ORDER BY expressions
--
test:do_catchsql_test(
    "select1-4.1",
    [[
        SELECT f1 FROM test1 ORDER BY f1
    ]], {
        -- <select1-4.1>
        0, {11, 33}
        -- </select1-4.1>
    })

test:do_catchsql_test(
    "select1-4.2",
    [[
        SELECT f1 FROM test1 ORDER BY -f1
    ]], {
        -- <select1-4.2>
        0, {33, 11}
        -- </select1-4.2>
    })

test:do_catchsql_test(
    "select1-4.3",
    [[
        SELECT f1 FROM test1 ORDER BY min(f1,f2)
    ]], {
        -- <select1-4.3>
        0, {11, 33}
        -- </select1-4.3>
    })

test:do_catchsql_test(
    "select1-4.4",
    [[
        SELECT f1 FROM test1 ORDER BY min(f1)
    ]], {
        -- <select1-4.4>
        1, "misuse of aggregate: MIN()"
        -- </select1-4.4>
    })

test:do_catchsql_test(
    "select1-4.5",
    [[
        INSERT INTO test1(f1) SELECT f1 FROM test1 ORDER BY min(f1);
    ]], {
        -- <select1-4.5>
        1, "misuse of aggregate: MIN()"
        -- </select1-4.5>
    })

-- The restriction not allowing constants in the ORDER BY clause
-- has been removed.  See ticket #1768
-- do_test select1-4.5 {
--  catchsql {
--    SELECT f1 FROM test1 ORDER BY 8.4;
--  }
-- } {1 {ORDER BY terms must not be non-integer constants}}
-- do_test select1-4.6 {
--  catchsql {
--    SELECT f1 FROM test1 ORDER BY '8.4';
--  }
-- } {1 {ORDER BY terms must not be non-integer constants}}
-- do_test select1-4.7.1 {
--  catchsql {
--    SELECT f1 FROM test1 ORDER BY 'xyz';
--  }
-- } {1 {ORDER BY terms must not be non-integer constants}}
--do_test select1-4.7.2 {
--  catchsql {
--    SELECT f1 FROM test1 ORDER BY -8.4;
--  }
--} {1 {ORDER BY terms must not be non-integer constants}}
--do_test select1-4.7.3 {
--  catchsql {
--    SELECT f1 FROM test1 ORDER BY +8.4;
--  }
--} {1 {ORDER BY terms must not be non-integer constants}}
-- do_test select1-4.7.4 {
--  catchsql {
--    SELECT f1 FROM test1 ORDER BY 4294967296; -- constant larger than 32 bits
--  }
-- } {1 {ORDER BY terms must not be non-integer constants}}
test:do_execsql_test(
    "select1-4.5",
    [[
        SELECT f1 FROM test1 ORDER BY 8.4
    ]], {
        -- <select1-4.5>
        11, 33
        -- </select1-4.5>
    })

test:do_execsql_test(
    "select1-4.6",
    [[
        SELECT f1 FROM test1 ORDER BY '8.4'
    ]], {
        -- <select1-4.6>
        11, 33
        -- </select1-4.6>
    })

test:do_execsql_test(
    "select1-4.8",
    [[
        DROP TABLE IF EXISTS t5;
        CREATE TABLE t5(a primary key,b);
        INSERT INTO t5 VALUES(1,10);
        INSERT INTO t5 VALUES(2,9);
        SELECT * FROM t5 ORDER BY 1;
    ]], {
        -- <select1-4.8>
        1, 10, 2, 9
        -- </select1-4.8>
    })

-- MUST_WORK_TEST
test:do_execsql_test(
    "select1-4.9.1",
    [[
        SELECT * FROM t5 ORDER BY 2;
    ]], {
        -- <select1-4.9.1>
        2, 9, 1, 10
        -- </select1-4.9.1>
    })

test:do_execsql_test(
    "select1-4.9.2",
    [[
        SELECT * FROM t5 ORDER BY +2;
    ]], {
        -- <select1-4.9.2>
        2, 9, 1, 10
        -- </select1-4.9.2>
    })

test:do_catchsql_test(
    "select1-4.10.1",
    [[
        SELECT * FROM t5 ORDER BY 3;
    ]], {
        -- <select1-4.10.1>
        1, "1st ORDER BY term out of range - should be between 1 and 2"
        -- </select1-4.10.1>
    })

test:do_catchsql_test(
    "select1-4.10.2",
    [[
        SELECT * FROM t5 ORDER BY -1;
    ]], {
        -- <select1-4.10.2>
        1, "1st ORDER BY term out of range - should be between 1 and 2"
        -- </select1-4.10.2>
    })

-- MUST_WORK_TEST
test:do_execsql_test(
    "select1-4.11",
    [[
        INSERT INTO t5 VALUES(3,10);
        SELECT * FROM t5 ORDER BY 2, 1 DESC;
    ]], {
        -- <select1-4.11>
        2, 9, 3, 10, 1, 10
        -- </select1-4.11>
    })

test:do_execsql_test(
    "select1-4.12",
    [[
        SELECT * FROM t5 ORDER BY 1 DESC, b;
    ]], {
        -- <select1-4.12>
        3, 10, 2, 9, 1, 10
        -- </select1-4.12>
    })

test:do_execsql_test(
    "select1-4.13",
    [[
        SELECT * FROM t5 ORDER BY b DESC, 1;
    ]], {
        -- <select1-4.13>
        1, 10, 3, 10, 2, 9
        -- </select1-4.13>
    })

-- ORDER BY ignored on an aggregate query
--
test:do_catchsql_test(
    "select1-5.1",
    [[
        SELECT max(f1) FROM test1 ORDER BY f2
    ]], {
        -- <select1-5.1>
        0, {33}
        -- </select1-5.1>
    })

test:execsql " DROP TABLE IF EXISTS test2 "
test:execsql "CREATE TABLE test2(t1 text primary key, t2 text)"
test:execsql "INSERT INTO test2 VALUES('abc','xyz')"
-- Check for column naming
--
test:do_catchsql2_test(
    "select1-6.1",
    [[
        SELECT f1 FROM test1 ORDER BY f2
    ]], {
        -- <select1-6.1>
        0, {"F1", 11, "F1", 33}
        -- </select1-6.1>
    })

test:do_test(
    "select1-6.1.1",
    function()
        test:execsql "PRAGMA full_column_names=on"
        return test:catchsql2 "SELECT f1 FROM test1 ORDER BY f2"
    end, {
        -- <select1-6.1.1>
        0, {"TEST1.F1", 11, "TEST1.F1", 33}
        -- </select1-6.1.1>
    })

test:do_catchsql2_test(
    "select1-6.1.2",
    [[
        SELECT f1 as "f1" FROM test1 ORDER BY f2
    ]], {
        -- <select1-6.1.2>
        0, {"f1", 11, "f1", 33}
        -- </select1-6.1.2>
    })

test:do_catchsql2_test(
    "select1-6.1.3",
    [[
        SELECT * FROM test1 WHERE f1==11
    ]], {
        -- <select1-6.1.3>
        0, {"F1", 11, "F2", 22}
        -- </select1-6.1.3>
    })

test:do_test(
    "select1-6.1.4",
    function()
        local msg
        local v = pcall(function()
            msg = test:execsql2 "SELECT DISTINCT * FROM test1 WHERE f1==11"
            end)
        v = v == true and {0} or {1} 
        test:execsql "PRAGMA full_column_names=off"
        return table.insert(v,msg) or v
    end, {
        -- <select1-6.1.4>
        0, {"F1", 11, "F2", 22}
        -- </select1-6.1.4>
    })

test:do_catchsql2_test(
    "select1-6.1.5",
    [[
        SELECT * FROM test1 WHERE f1==11
    ]], {
        -- <select1-6.1.5>
        0, {"F1", 11, "F2", 22}
        -- </select1-6.1.5>
    })

test:do_catchsql2_test(
    "select1-6.1.6",
    [[
        SELECT DISTINCT * FROM test1 WHERE f1==11
    ]], {
        -- <select1-6.1.6>
        0, {"F1", 11, "F2", 22}
        -- </select1-6.1.6>
    })

test:do_catchsql2_test(
    "select1-6.2",
    [[
        SELECT f1 as xyzzy FROM test1 ORDER BY f2
    ]], {
        -- <select1-6.2>
        0, {"XYZZY", 11, "XYZZY", 33}
        -- </select1-6.2>
    })

test:do_catchsql2_test(
    "select1-6.3",
    [[
        SELECT f1 as "xyzzy" FROM test1 ORDER BY f2
    ]], {
        -- <select1-6.3>
        0, {"xyzzy", 11, "xyzzy", 33}
        -- </select1-6.3>
    })

test:do_catchsql2_test(
    "select1-6.3.1",
    [[
        SELECT f1 as "xyzzy " FROM test1 ORDER BY f2
    ]], {
        -- <select1-6.3.1>
        0, {"xyzzy ", 11, "xyzzy ", 33}
        -- </select1-6.3.1>
    })

test:do_catchsql2_test(
    "select1-6.4",
    [[
        SELECT f1+F2 as xyzzy FROM test1 ORDER BY f2
    ]], {
        -- <select1-6.4>
        0, {"XYZZY", 33, "XYZZY", 77}
        -- </select1-6.4>
    })

test:do_catchsql2_test(
    "select1-6.4a",
    [[
        SELECT f1+F2 FROM test1 ORDER BY f2
    ]], {
        -- <select1-6.4a>
        0, {"f1+F2", 33, "f1+F2", 77}
        -- </select1-6.4a>
    })

test:do_catchsql2_test(
    "select1-6.5",
    [[
        SELECT test1.f1+F2 FROM test1 ORDER BY f2
    ]], {
        -- <select1-6.5>
        0, {"test1.f1+F2", 33, "test1.f1+F2", 77}
        -- </select1-6.5>
    })

test:do_test(
    "select1-6.5.1",
    function()
        test:execsql2 "PRAGMA full_column_names=on"
        local msg
        v = pcall( function ()
                msg = test:execsql2 "SELECT test1.f1+F2 FROM test1 ORDER BY f2"
            end)
        v = v == true and {0} or {1}
        test:execsql2 "PRAGMA full_column_names=off"
        return table.insert(v,msg) or v
    end, {
        -- <select1-6.5.1>
        0, {'test1.f1+F2', 33, 'test1.f1+F2', 77}
        -- </select1-6.5.1>
    })

test:do_catchsql2_test(
    "select1-6.6",
    [[SELECT test1.f1+F2, t1 FROM test1, test2 
         ORDER BY f2]], {
        -- <select1-6.6>
        0, {"test1.f1+F2", 33, "T1", "abc", "test1.f1+F2", 77, "T1", "abc"}
        -- </select1-6.6>
    })

test:do_catchsql2_test(
    "select1-6.7",
    [[SELECT A.f1, t1 FROM test1 as A, test2 
         ORDER BY f2]], {
        -- <select1-6.7>
        0, {"F1", 11, "T1", "abc", "F1", 33, "T1", "abc"}
        -- </select1-6.7>
    })

test:do_catchsql2_test(
    "select1-6.8",
    [[SELECT A.f1, f1 FROM test1 as A, test1 as B 
         ORDER BY f2]], {
        -- <select1-6.8>
        1, "ambiguous column name: F1"
        -- </select1-6.8>
    })

test:do_catchsql2_test(
    "select1-6.8b",
    [[SELECT A.f1, B.f1 FROM test1 as A, test1 as B 
         ORDER BY f2]], {
        -- <select1-6.8b>
        1, "ambiguous column name: F2"
        -- </select1-6.8b>
    })

test:do_catchsql2_test(
    "select1-6.8c",
    [[SELECT A.f1, f1 FROM test1 as A, test1 as A 
         ORDER BY f2]], {
        -- <select1-6.8c>
        1, "ambiguous column name: A.F1"
        -- </select1-6.8c>
    })

test:do_catchsql_test(
    "select1-6.9.1",
    [[SELECT A.f1, B.f1 FROM test1 as A, test1 as B 
         ORDER BY A.f1, B.f1]], {
        -- <select1-6.9.1>
        0, {11, 11, 11, 33, 33, 11, 33, 33}
        -- </select1-6.9.1>
    })

test:do_catchsql2_test(
    "select1-6.9.2",
    [[SELECT A.f1, B.f1 FROM test1 as A, test1 as B
         ORDER BY A.f1, B.f1]], {
    -- <select1-6.9.2>
    0, {"F1", 11, "F1", 11, "F1", 11, "F1", 33, "F1", 33, "F1", 11, "F1", 33, "F1", 33}
    -- </select1-6.9.2>
})

test:do_test(
    "select1-6.9.3",
    function()
        test:execsql [[
            PRAGMA short_column_names='OFF';
            PRAGMA full_column_names='OFF';
        ]]
        return test:execsql2 [[
            SELECT test1 . f1, test1 . f2 FROM test1 LIMIT 1
        ]]
    end, {
        -- <select1-6.9.3>
        "test1 . f1", 11, "test1 . f2", 22
        -- </select1-6.9.3>
    })

test:do_test(
    "select1-6.9.4",
    function()
        test:execsql [[
            PRAGMA short_column_names='OFF';
            PRAGMA full_column_names='ON';
        ]]
        return test:execsql2 [[
            SELECT test1 . f1, test1 . f2 FROM test1 LIMIT 1
        ]]
    end, {
        -- <select1-6.9.4>
        "TEST1.F1", 11, "TEST1.F2", 22
        -- </select1-6.9.4>
    })

test:do_test(
    "select1-6.9.5",
    function()
        test:execsql [[
            PRAGMA short_column_names='OFF';
            PRAGMA full_column_names='ON';
        ]]
        return test:execsql2 [[
            SELECT 123.45;
        ]]
    end, {
        -- <select1-6.9.5>
        "123.45", 123.45
        -- </select1-6.9.5>
    })

test:do_execsql2_test(
    "select1-6.9.6",
    [[
        SELECT * FROM test1 a, test1 b LIMIT 1
    ]], {
        -- <select1-6.9.6>
        "A.F1", 11, "A.F2", 22, "B.F1", 11, "B.F2", 22
        -- </select1-6.9.6>
    })

test:do_test(
    "select1-6.9.7",
    function()
        x = test:execsql2 [[
            SELECT * FROM test1 a, (select 5, 6) LIMIT 1
        ]]
        for i, tmp in ipairs(x) do
            if type(tmp) == "string" then
                x[i] = tmp:gsub("sq_[0-9a-fA-F_]+", "subquery")
            end
        end
        return x
    end, {
        -- <select1-6.9.7>
        "A.F1", 11, "A.F2", 22, "sqlite_subquery.5", 5, "sqlite_subquery.6", 6
        -- </select1-6.9.7>
    })

test:do_test(
    "select1-6.9.8",
    function()
        x = test:execsql2 [[
            SELECT * FROM test1 a, (select 5 AS x, 6 AS y) AS b LIMIT 1
        ]]
        for i, tmp in ipairs(x) do
            if type(tmp) == "string" then
                x[i] = tmp:gsub("subquery_[0-9a-fA-F]+_", "subquery")
            end
        end
        return x
    end, {
        -- <select1-6.9.8>
        "A.F1", 11, "A.F2", 22, "B.X", 5, "B.Y", 6
        -- </select1-6.9.8>
    })

test:do_execsql2_test(
    "select1-6.9.9",
    [[
        SELECT a.f1, b.f2 FROM test1 a, test1 b LIMIT 1
    ]], {
        -- <select1-6.9.9>
        "TEST1.F1", 11, "TEST1.F2", 22
        -- </select1-6.9.9>
    })

test:do_execsql2_test(
    "select1-6.9.10",
    [[
        SELECT f1, t1 FROM test1, test2 LIMIT 1
    ]], {
        -- <select1-6.9.10>
        "TEST1.F1", 11, "TEST2.T1", "abc"
        -- </select1-6.9.10>
    })

test:do_test(
    "select1-6.9.11",
    function()
        test:execsql [[
            PRAGMA short_column_names='ON';
            PRAGMA full_column_names='ON';
        ]]
        return test:execsql2 [[
            SELECT a.f1, b.f2 FROM test1 a, test1 b LIMIT 1
        ]]
    end, {
        -- <select1-6.9.11>
        "TEST1.F1", 11, "TEST1.F2", 22
        -- </select1-6.9.11>
    })

test:do_execsql2_test(
    "select1-6.9.12",
    [[
        SELECT f1, t1 FROM test1, test2 LIMIT 1
    ]], {
        -- <select1-6.9.12>
        "TEST1.F1", 11, "TEST2.T1", "abc"
        -- </select1-6.9.12>
    })

test:do_test(
    "select1-6.9.13",
    function()
        test:execsql [[
            PRAGMA short_column_names='ON';
            PRAGMA full_column_names='OFF';
        ]]
        return test:execsql2 [[
            SELECT a.f1, b.f1 FROM test1 a, test1 b LIMIT 1
        ]]
    end, {
        -- <select1-6.9.13>
        "F1", 11, "F1", 11
        -- </select1-6.9.13>
    })

test:do_execsql2_test(
    "select1-6.9.14",
    [[
        SELECT f1, t1 FROM test1, test2 LIMIT 1
    ]], {
        -- <select1-6.9.14>
        "F1", 11, "T1", "abc"
        -- </select1-6.9.14>
    })

test:do_test(
    "select1-6.9.15",
    function()
        test:execsql [[
            PRAGMA short_column_names='OFF';
            PRAGMA full_column_names='ON';
        ]]
        return test:execsql2 [[
            SELECT a.f1, b.f1 FROM test1 a, test1 b LIMIT 1
        ]]
    end, {
        -- <select1-6.9.15>
        "TEST1.F1", 11, "TEST1.F1", 11
        -- </select1-6.9.15>
    })

test:do_execsql2_test(
    "select1-6.9.16",
    [[
        SELECT f1, t1 FROM test1, test2 LIMIT 1
    ]], {
        -- <select1-6.9.16>
        "TEST1.F1", 11, "TEST2.T1", "abc"
        -- </select1-6.9.16>
    })

test:execsql [[
    PRAGMA short_column_names='ON';
    PRAGMA full_column_names='OFF';
]]
test:do_catchsql2_test(
        "select1-6.10",
        [[
            SELECT f1 FROM test1 UNION SELECT f2 FROM test1
            ORDER BY f2;
        ]], {
            -- <select1-6.10>
            0, {"F1", 11, "F1", 22, "F1", 33, "F1", 44}
            -- </select1-6.10>
        })

    test:do_catchsql2_test(
        "select1-6.11",
        [[
            SELECT f1 FROM test1 UNION SELECT f2+100 FROM test1
            ORDER BY f2+101;
        ]], {
            -- <select1-6.11>
            1, "1st ORDER BY term does not match any column in the result set"
            -- </select1-6.11>
        })

    -- Ticket #2296
    test:do_execsql_test(
            "select1-6.20",
            [[
                DROP TABLE IF EXISTS t6;
                CREATE TABLE t6(a TEXT primary key, b TEXT);
                INSERT INTO t6 VALUES('a','0');
                INSERT INTO t6 VALUES('b','1');
                INSERT INTO t6 VALUES('c','2');
                INSERT INTO t6 VALUES('d','3');
                SELECT a FROM t6 WHERE b IN 
                   (SELECT b FROM t6 WHERE a<='b' UNION SELECT '3' AS x
                            ORDER BY 1 LIMIT 1)
            ]], {
                -- <select1-6.20>
                "a"
                -- </select1-6.20>
            })

        test:do_execsql_test(
            "select1-6.21",
            [[
                SELECT a FROM t6 WHERE b IN 
                   (SELECT b FROM t6 WHERE a<='b' UNION SELECT '3' AS x
                            ORDER BY 1 DESC LIMIT 1)
            ]], {
                -- <select1-6.21>
                "d"
                -- </select1-6.21>
            })

        test:do_execsql_test(
            "select1-6.22",
            [[
                SELECT a FROM t6 WHERE b IN 
                   (SELECT b FROM t6 WHERE a<='b' UNION SELECT '3' AS x
                            ORDER BY b LIMIT 2)
                ORDER BY a;
            ]], {
                -- <select1-6.22>
                "a", "b"
                -- </select1-6.22>
            })

        test:do_execsql_test(
            "select1-6.23",
            [[
                SELECT a FROM t6 WHERE b IN 
                   (SELECT b FROM t6 WHERE a<='b' UNION SELECT '3' AS x
                            ORDER BY x DESC LIMIT 2)
                ORDER BY a;
            ]], {
                -- <select1-6.23>
                "b", "d"
                -- </select1-6.23>
            })





--ifcapable compound
test:do_catchsql_test(
    "select1-7.1",
    [[
        SELECT f1 FROM test1 WHERE f2=;
    ]], {
        -- <select1-7.1>
        1, [[near ";": syntax error]]
        -- </select1-7.1>
    })

test:do_catchsql_test(
        "select1-7.2",
        [[
            SELECT f1 FROM test1 UNION SELECT WHERE;
        ]], {
            -- <select1-7.2>
            1, [[keyword "WHERE" is reserved]]
            -- </select1-7.2>
        })



-- ifcapable compound
test:do_catchsql_test(
    "select1-7.3",
    [[
        SELECT f1 FROM test1 as "hi", test2 as]], {
        -- <select1-7.3>
        1, [[keyword "as" is reserved]]
        -- </select1-7.3>
    })

test:do_catchsql_test(
    "select1-7.4",
    [[
        SELECT f1 FROM test1 ORDER BY;
    ]], {
        -- <select1-7.4>
        1, [[near ";": syntax error]]
        -- </select1-7.4>
    })

test:do_catchsql_test(
    "select1-7.5",
    [[
        SELECT f1 FROM test1 ORDER BY f1 desc, f2 where;
    ]], {
        -- <select1-7.5>
        1, [[keyword "where" is reserved]]
        -- </select1-7.5>
    })

test:do_catchsql_test(
    "select1-7.6",
    [[
        SELECT count(f1,f2 FROM test1;
    ]], {
        -- <select1-7.6>
        1, [[keyword "FROM" is reserved]]
        -- </select1-7.6>
    })

test:do_catchsql_test(
    "select1-7.7",
    [[
        SELECT count(f1,f2+) FROM test1;
    ]], {
        -- <select1-7.7>
        1, [[near ")": syntax error]]
        -- </select1-7.7>
    })

test:do_catchsql_test(
    "select1-7.8",
    [[
        SELECT f1 FROM test1 ORDER BY f2, f1+;
    ]], {
        -- <select1-7.8>
        1, [[near ";": syntax error]]
        -- </select1-7.8>
    })

test:do_catchsql_test(
    "select1-7.9",
    [[
        SELECT f1 FROM test1 LIMIT 5+3 OFFSET 11 ORDER BY f2;
    ]], {
        -- <select1-7.9>
        1, [[keyword "ORDER" is reserved]]
        -- </select1-7.9>
    })

test:do_execsql_test(
    "select1-8.1",
    [[
        SELECT f1 FROM test1 WHERE 4.3+2.4 OR 1 ORDER BY f1
    ]], {
        -- <select1-8.1>
        11, 33
        -- </select1-8.1>
    })

test:do_execsql_test(
    "select1-8.2",
    [[
        SELECT f1 FROM test1 WHERE ('x' || f1) BETWEEN 'x10' AND 'x20'
        ORDER BY f1
    ]], {
        -- <select1-8.2>
        11
        -- </select1-8.2>
    })

test:do_execsql_test(
    "select1-8.3",
    [[
        SELECT f1 FROM test1 WHERE 5-3==2
        ORDER BY f1
    ]], {
        -- <select1-8.3>
        11, 33
        -- </select1-8.3>
    })

-- TODO: This test is failing because f1 is now being loaded off the
-- disk as a vdbe integer, not a string. Hence the value of f1/(f1-11)
-- changes because of rounding. Disable the test for now.
if false
 then
    test:do_execsql_test(
        "select1-8.4",
        [[
            SELECT coalesce(f1/(f1-11),'x'),
                   coalesce(min(f1/(f1-11),5),'y'),
                   coalesce(max(f1/(f1-33),6),'z')
            FROM test1 ORDER BY f1
        ]], {
            -- <select1-8.4>
            "x", "y", 6, 1.5, 1.5, "z"
            -- </select1-8.4>
        })

end
test:do_execsql_test(
    "select1-8.5",
    [[
        SELECT min(1,2,3), -max(1,2,3)
        FROM test1 ORDER BY f1
    ]], {
        -- <select1-8.5>
        1, -3, 1, -3
        -- </select1-8.5>
    })

-- Check the behavior when the result set is empty
--
-- SQLite v3 always sets r(*).
--
-- do_test select1-9.1 {
--   catch {unset r}
--   set r(*) {}
--   db eval {SELECT * FROM test1 WHERE f1<0} r {}
--   set r(*)
-- } {}
test:do_test(
    "select1-9.2",
    function()
        local r = box.sql.execute "SELECT * FROM test1 WHERE f1<0"
        return r[0]
    end, {
        -- <select1-9.2>
        "F1", "F2"
        -- </select1-9.2>
    })

test:do_test(
        "select1-9.3",
        function()
            local r = box.sql.execute "SELECT * FROM test1 WHERE f1<(select count(*) from test2)"
            return r[0]
        end, {
            -- <select1-9.3>
            "F1", "F2"
            -- </select1-9.3>
        })



test:do_test(
    "select1-9.4",
    function()
        local r = box.sql.execute "SELECT * FROM test1 ORDER BY f1"
        return r[0]
    end, {
        -- <select1-9.4>
        "F1", "F2"
        -- </select1-9.4>
    })

test:do_test(
    "select1-9.5",
    function()
        local r = box.sql.execute "SELECT * FROM test1 WHERE f1<0 ORDER BY f1"
        return r[0]
    end, {
        -- <select1-9.5>
        "F1", "F2"
        -- </select1-9.5>
    })

-- ["unset","r"]
-- Check for ORDER BY clauses that refer to an AS name in the column list
--
test:do_execsql_test(
    "select1-10.1",
    [[
        SELECT f1 AS x FROM test1 ORDER BY x
    ]], {
        -- <select1-10.1>
        11, 33
        -- </select1-10.1>
    })

test:do_execsql_test(
    "select1-10.2",
    [[
        SELECT f1 AS x FROM test1 ORDER BY -x
    ]], {
        -- <select1-10.2>
        33, 11
        -- </select1-10.2>
    })

test:do_execsql_test(
    "select1-10.3",
    [[
        SELECT f1-23 AS x FROM test1 ORDER BY abs(x)
    ]], {
        -- <select1-10.3>
        10, -12
        -- </select1-10.3>
    })

test:do_execsql_test(
    "select1-10.4",
    [[
        SELECT f1-23 AS x FROM test1 ORDER BY -abs(x)
    ]], {
        -- <select1-10.4>
        -12, 10
        -- </select1-10.4>
    })

test:do_execsql_test(
    "select1-10.5",
    [[
        SELECT f1-22 AS x, f2-22 as y FROM test1
    ]], {
        -- <select1-10.5>
        -11, 0, 11, 22
        -- </select1-10.5>
    })

test:do_execsql_test(
    "select1-10.6",
    [[
        SELECT f1-22 AS x, f2-22 as y FROM test1 WHERE x>0 AND y<50
    ]], {
        -- <select1-10.6>
        11, 22
        -- </select1-10.6>
    })

test:do_execsql_test(
    "select1-10.7",
    [[
        SELECT f1 COLLATE "unicode_ci" AS x FROM test1 ORDER BY x
    ]], {
        -- <select1-10.7>
        11, 33
        -- </select1-10.7>
    })

-- Check the ability to specify "TABLE.*" in the result set of a SELECT
--
test:do_execsql_test(
    "select1-11.1",
    [[
        DELETE FROM t3;
        DELETE FROM t4;
        INSERT INTO t3 VALUES(0,1,2);
        INSERT INTO t4 VALUES(0,3,4);
        SELECT * FROM t3, t4;
    ]], {
        -- <select1-11.1>
        0, 1, 2, 0, 3, 4
        -- </select1-11.1>
    })

test:do_execsql_test(
    "select1-11.2.1",
    [[
        SELECT * FROM t3, t4;
    ]], {
        -- <select1-11.2.1>
        0, 1, 2, 0, 3, 4
        -- </select1-11.2.1>
    })

test:do_execsql2_test(
    "select1-11.2.2",
    [[
        SELECT * FROM t3, t4;
    ]], {
        -- <select1-11.2.2>
        "ID",0,"A",1,"B",2,"ID",0,"A",3,"B",4
        -- </select1-11.2.2>
    })

test:do_execsql_test(
    "select1-11.4.1",
    [[
        SELECT t3.*, t4.b FROM t3, t4;
    ]], {
        -- <select1-11.4.1>
        0, 1, 2, 4
        -- </select1-11.4.1>
    })

test:do_execsql_test(
    "select1-11.4.2",
    [[
        SELECT "T3".*, t4.b FROM t3, t4;
    ]], {
        -- <select1-11.4.2>
        0, 1, 2, 4
        -- </select1-11.4.2>
    })

test:do_execsql2_test(
    "select1-11.5.1",
    [[
        SELECT t3.*, t4.b FROM t3, t4;
    ]], {
        -- <select1-11.5.1>
        "ID", 0, "A", 1, "B", 2, "B", 4
        -- </select1-11.5.1>
    })

test:do_execsql2_test(
    "select1-11.6",
    [[
        SELECT x.*, y.b FROM t3 AS x, t4 AS y;
    ]], {
        -- <select1-11.6>
        "ID", 0, "A", 1, "B", 2, "B", 4
        -- </select1-11.6>
    })

test:do_execsql_test(
    "select1-11.7",
    [[
        SELECT t3.b, t4.* FROM t3, t4;
    ]], {
        -- <select1-11.7>
        2, 0, 3, 4
        -- </select1-11.7>
    })

test:do_execsql2_test(
    "select1-11.8",
    [[
        SELECT t3.b, t4.* FROM t3, t4;
    ]], {
        -- <select1-11.8>
        "B", 2, "ID", 0, "A", 3, "B", 4
        -- </select1-11.8>
    })

test:do_execsql2_test(
    "select1-11.9",
    [[
        SELECT x.b, y.* FROM t3 AS x, t4 AS y;
    ]], {
        -- <select1-11.9>
        "B", 2, "ID", 0, "A", 3, "B", 4
        -- </select1-11.9>
    })

test:do_catchsql_test(
    "select1-11.10",
    [[
        SELECT t5.* FROM t3, t4;
    ]], {
        -- <select1-11.10>
        1, "no such table: T5"
        -- </select1-11.10>
    })

test:do_catchsql_test(
    "select1-11.11",
    [[
        SELECT t3.* FROM t3 AS x, t4;
    ]], {
        -- <select1-11.11>
        1, "no such table: T3"
        -- </select1-11.11>
    })

test:do_execsql2_test(
        "select1-11.12",
        [[
            SELECT t3.* FROM t3, (SELECT max(a), max(b) FROM t4)
        ]], {
            -- <select1-11.12>
            "ID", 0, "A", 1, "B", 2
            -- </select1-11.12>
        })

    test:do_execsql2_test(
        "select1-11.13",
        [[
            SELECT t3.* FROM (SELECT max(a), max(b) FROM t4), t3
        ]], {
            -- <select1-11.13>
            "ID", 0, "A", 1, "B", 2
            -- </select1-11.13>
        })

    test:do_execsql2_test(
        "select1-11.14",
        [[
            SELECT * FROM t3, (SELECT max(a), max(b) FROM t4) as "tx"
        ]], {
            -- <select1-11.14>
            "ID", 0, "A", 1, "B", 2, "max(a)", 3, "max(b)", 4
            -- </select1-11.14>
        })

    test:do_execsql2_test(
        "select1-11.15",
        [[
            SELECT y.*, t3.* FROM t3, (SELECT max(a), max(b) FROM t4) AS y
        ]], {
            -- <select1-11.15>
            "max(a)", 3, "max(b)", 4, "ID", 0, "A", 1, "B", 2
            -- </select1-11.15>
        })



test:do_execsql2_test(
    "select1-11.16",
    [[
        SELECT y.* FROM t3 as y, t4 as z
    ]], {
        -- <select1-11.16>
        "ID", 0, "A", 1, "B", 2
        -- </select1-11.16>
    })

-- Tests of SELECT statements without a FROM clause.
--
test:do_execsql2_test(
    "select1-12.1",
    [[
        SELECT 1+2+3
    ]], {
        -- <select1-12.1>
        "1+2+3", 6
        -- </select1-12.1>
    })

test:do_execsql2_test(
    "select1-12.2",
    [[
        SELECT 1,'hello',2
    ]], {
        -- <select1-12.2>
        '1', 1, "'hello'", "hello", '2', 2
        -- </select1-12.2>
    })

test:do_execsql2_test(
    "select1-12.3",
    [[
        SELECT 1 as "a",'hello' as "b",2 as "c"
    ]], {
        -- <select1-12.3>
        "a", 1, "b", "hello", "c", 2
        -- </select1-12.3>
    })

test:do_execsql_test(
    "select1-12.4",
    [[
        DELETE FROM t3;
        INSERT INTO t3 VALUES(0,1,2);
    ]], {
        -- <select1-12.4>
        
        -- </select1-12.4>
    })

test:do_execsql_test(
        "select1-12.5",
        [[
            SELECT a,b FROM t3 UNION SELECT 3 as "a", 4 ORDER BY a;
        ]], {
            -- <select1-12.5>
            1, 2, 3, 4
            -- </select1-12.5>
        })

    test:do_execsql_test(
        "select1-12.6",
        [[
            SELECT 5, 3, 4 UNION SELECT * FROM t3;
        ]], {
            -- <select1-12.6>
            0, 1, 2, 5, 3, 4
            -- </select1-12.6>
        })

    -- 


-- ifcapable compound
test:do_execsql_test(
        "select1-12.7",
        [[
            SELECT * FROM t3 WHERE a=(SELECT 1);
        ]], {
            -- <select1-12.7>
            0, 1, 2
            -- </select1-12.7>
        })

test:do_execsql_test(
    "select1-12.8",
    [[
        SELECT * FROM t3 WHERE a=(SELECT 2);
    ]], {
        -- <select1-12.8>

        -- </select1-12.8>
    })



test:do_execsql2_test(
    "select1-12.9",
    [[
        SELECT x FROM (
          SELECT a AS x, b AS y FROM t3 UNION SELECT a,b FROM t4 ORDER BY a,b
        ) ORDER BY x;
    ]], {
        -- <select1-12.9>
        "X", 1, "X", 3
        -- </select1-12.9>
    })

test:do_execsql2_test(
    "select1-12.10",
    [[
        SELECT z.x FROM (
          SELECT a AS x,b AS y FROM t3 UNION SELECT a, b FROM t4 ORDER BY a,b
        ) as z ORDER BY x;
    ]], {
        -- <select1-12.10>
        "X", 1, "X", 3
        -- </select1-12.10>
    })


-- ifcapable compound
-- MUST_WORK_TEST
-- Check for a VDBE stack growth problem that existed at one point.
--
test:do_test(
    "select1-13.1",
    function()
        test:execsql [[
            drop table if exists abc;
            create TABLE abc(a, b, c, PRIMARY KEY(a, b));
            BEGIN;
            INSERT INTO abc VALUES(1, 1, 1);
        ]]
        for i = 0,9,1 do
            test:execsql [[
                INSERT INTO abc SELECT a+(select max(a) FROM abc), b+(select max(a) FROM abc), c+(select max(a) FROM abc) FROM abc;
            ]]
        end
        test:execsql "COMMIT"
        -- This used to seg-fault when the problem existed.
        return test:execsql [[
            SELECT count(
              (SELECT a FROM abc WHERE a = NULL AND b >= upper.c)
            ) FROM abc AS upper;
        ]]
    end, {
        -- <select1-13.1>
        0
        -- </select1-13.1>
    })



-- foreach tab [db eval {SELECT name FROM sqlite_master WHERE type = 'table'}] {
--   db eval "DROP TABLE $tab"
-- }
-- db close
-- sqlite3 db test.db
-- do_test select1-14.1 {
--   execsql { 
--     SELECT * FROM sqlite_master WHERE rowid>10; 
--     SELECT * FROM sqlite_master WHERE rowid=10;
--     SELECT * FROM sqlite_master WHERE rowid<10;
--     SELECT * FROM sqlite_master WHERE rowid<=10;
--     SELECT * FROM sqlite_master WHERE rowid>=10;
--     SELECT * FROM sqlite_master;
--   }
-- } {}
-- do_test select1-14.2 {
--   execsql { 
--     SELECT 10 IN (SELECT rowid FROM sqlite_master);
--   }
-- } {0}


-------------------------------------------
--if X(1053, "X!cmd", "[\"expr\",\"[db one {PRAGMA locking_mode}]==\\\"normal\\\"\"]")
-- then
    -- Check that ticket #3771 has been fixed.  This test does not
    -- work with locking_mode=EXCLUSIVE so disable in that case.
    --
    test:do_execsql_test(
        "select1-15.1",
        [[
            DROP TABLE IF EXISTS t1;
            CREATE TABLE t1(id int primary key,a);
            CREATE INDEX i1 ON t1(a);
            INSERT INTO t1 VALUES(1, 1);
            INSERT INTO t1 VALUES(2, 2);
            INSERT INTO t1 VALUES(3, 3);
        ]], {
            -- <select1-15.1>
            
            -- </select1-15.1>
        })

    -- do_test select1-15.2 {
    --   sqlite3 db2 test.db
    --   execsql { DROP INDEX i1 } db2
    --   db2 close
    -- } {}
    test:do_execsql_test(
        "select1-15.3",
        [[
            SELECT 2 IN (SELECT a FROM t1) 
        ]], {
            -- <select1-15.3>
            1
            -- </select1-15.3>
        })

--end
-- Crash bug reported on the mailing list on 2012-02-23
--
test:do_catchsql_test(
    "select1-16.1",
    [[
        SELECT 1 FROM (SELECT *)
    ]], {
        -- <select1-16.1>
        1, "no tables specified"
        -- </select1-16.1>
    })

-- # 2015-04-17:  assertion fix.
-- do_catchsql_test select1-16.2 {
--   SELECT 1 FROM sqlite_master LIMIT 1,#1;
-- } {1 {near "#1": syntax error}}
test:finish_test()

