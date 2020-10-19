#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(27)

--!./tcltestrunner.lua
-- 2013-04-13
--
-- The author disclaims copyright to this source code. In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file tests features of the name resolver (the component that
-- figures out what identifiers in the SQL statement refer to) that
-- were fixed by ticket [2500cdb9be].
--
-- See also tickets [1c69be2daf] and [f617ea3125] from 2013-08-14.
--
-- Also a fuzzer-discovered problem on 2015-04-23.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- "ORDER BY y" binds to the output result-set column named "y"
-- if available.  If no output column is named "y", then try to
-- bind against an input column named "y".
--
-- This is classical SQL92 behavior.
--
test:do_catchsql_test(
    "resolver01-1.1",
    [[
        CREATE TABLE t1(x  INT primary key, y INT ); INSERT INTO t1 VALUES(11,22);
        CREATE TABLE t2(y  INT primary key, z INT ); INSERT INTO t2 VALUES(33,44);
        SELECT 1 AS y FROM t1, t2 ORDER BY y;
    ]], {
        -- <resolver01-1.1>
        0, {1}
        -- </resolver01-1.1>
    })

test:do_catchsql_test(
    "resolver01-1.2",
    [[
        SELECT 1 AS yy FROM t1, t2 ORDER BY y;
    ]], {
        -- <resolver01-1.2>
        1, "ambiguous column name: Y"
        -- </resolver01-1.2>
    })

test:do_catchsql_test(
    "resolver01-1.3",
    [[
        CREATE TABLE t3(x  INT primary key,y INT ); INSERT INTO t3 VALUES(11,44),(33,22);
        SELECT x AS y FROM t3 ORDER BY y;
    ]], {
        -- <resolver01-1.3>
        0, {11, 33}
        -- </resolver01-1.3>
    })

test:do_catchsql_test(
    "resolver01-1.4",
    [[
        SELECT x AS yy FROM t3 ORDER BY y;
    ]], {
        -- <resolver01-1.4>
        0, {33, 11}
        -- </resolver01-1.4>
    })

-- sql allows the WHERE clause to reference output columns if there is
-- no other way to resolve the name.
--
test:do_catchsql_test(
    "resolver01-1.5",
    [[
        SELECT x AS yy FROM t3 ORDER BY yy;
    ]], {
        -- <resolver01-1.5>
        0, {11, 33}
        -- </resolver01-1.5>
    })

test:do_catchsql_test(
    "resolver01-1.6",
    [[
        SELECT x AS yy FROM t3 ORDER BY 1;
    ]], {
        -- <resolver01-1.6>
        0, {11, 33}
        -- </resolver01-1.6>
    })

-- The "ORDER BY y COLLATE nocase" form works the same as "ORDER BY y".
-- The "y" binds more tightly to output columns than to input columns.
--
-- This is for compatibility with SQL92 and with historical sql behavior.
-- Note that PostgreSQL considers "y COLLATE nocase" to be an expression
-- and thus PostgreSQL treats this case as if it where the 3.x case below.
--
test:do_catchsql_test(
    "resolver01-2.1",
    [[
        SELECT 2 AS y FROM t1, t2 ORDER BY y;
    ]], {
        -- <resolver01-2.1>
        0, {2}
        -- </resolver01-2.1>
    })

test:do_catchsql_test(
    "resolver01-2.2",
    [[
        SELECT 2 AS yy FROM t1, t2 ORDER BY y;
    ]], {
        -- <resolver01-2.2>
        1, "ambiguous column name: Y"
        -- </resolver01-2.2>
    })

test:do_catchsql_test(
    "resolver01-2.3",
    [[
        SELECT x AS y FROM t3 ORDER BY y;
    ]], {
        -- <resolver01-2.3>
        0, {11, 33}
        -- </resolver01-2.3>
    })

test:do_catchsql_test(
    "resolver01-2.4",
    [[
        SELECT x AS yy FROM t3 ORDER BY y;
    ]], {
        -- <resolver01-2.4>
        0, {33, 11}
        -- </resolver01-2.4>
    })

test:do_catchsql_test(
    "resolver01-2.5",
    [[
        SELECT x AS yy FROM t3 ORDER BY yy;
    ]], {
        -- <resolver01-2.5>
        0, {11, 33}
        -- </resolver01-2.5>
    })

test:do_catchsql_test(
    "resolver01-2.6",
    [[
        SELECT x AS yy FROM t3 ORDER BY 1;
    ]], {
        -- <resolver01-2.6>
        0, {11, 33}
        -- </resolver01-2.6>
    })

-- But if the form is "ORDER BY expr" then bind more tightly to the
-- the input column names and only use the output column names if no
-- input column name matches.
--
-- This is SQL99 behavior, as implemented by PostgreSQL and MS-SQL.
-- Note that Oracle works differently.
--
test:do_catchsql_test(
    "resolver01-3.1",
    [[
        SELECT 3 AS y FROM t1, t2 ORDER BY +y;
    ]], {
        -- <resolver01-3.1>
        1, "ambiguous column name: Y"
        -- </resolver01-3.1>
    })

test:do_catchsql_test(
    "resolver01-3.2",
    [[
        SELECT 2 AS yy FROM t1, t2 ORDER BY +y;
    ]], {
        -- <resolver01-3.2>
        1, "ambiguous column name: Y"
        -- </resolver01-3.2>
    })

test:do_catchsql_test(
    "resolver01-3.3",
    [[
        SELECT x AS y FROM t3 ORDER BY +y;
    ]], {
        -- <resolver01-3.3>
        0, {33, 11}
        -- </resolver01-3.3>
    })

test:do_catchsql_test(
    "resolver01-3.4",
    [[
        SELECT x AS yy FROM t3 ORDER BY +y;
    ]], {
        -- <resolver01-3.4>
        0, {33, 11}
        -- </resolver01-3.4>
    })

test:do_catchsql_test(
    "resolver01-3.5",
    [[
        SELECT x AS yy FROM t3 ORDER BY +yy
    ]], {
        -- <resolver01-3.5>
        0, {11, 33}
        -- </resolver01-3.5>
    })

-- This is the test case given in ticket [f617ea3125e9] (with table name
-- changed from "t1" to "t4".  The behavior of (1) and (3) match with
-- PostgreSQL, but we intentionally break with PostgreSQL to provide
-- SQL92 behavior for case (2).
--
test:do_test(
    "resolver01-4.1",
    function ()
        test:execsql([[
            CREATE TABLE t4(m VARCHAR(2) primary key);
            INSERT INTO t4 VALUES('az');
            INSERT INTO t4 VALUES('by');
            INSERT INTO t4 VALUES('cx');
        ]])
        local r = {}
        table.insert(r, test:execsql("SELECT '1', substr(m,2) AS m FROM t4 ORDER BY m;"))
        table.insert(r, test:execsql("SELECT '2', substr(m,2) AS m FROM t4 ORDER BY m COLLATE \"binary\";"))
        table.insert(r, test:execsql("SELECT '3', substr(m,2) AS m FROM t4 ORDER BY lower(m);"))
        return r
    end, {
        -- <resolver01-4.1>
        {"1", "x", "1", "y", "1", "z"},
        {"2", "x", "2", "y", "2", "z"},
        {"3", "z", "3", "y", "3", "x"}
        -- </resolver01-4.1>
    })

---------------------------------------------------------------------------
-- Test cases for ticket [1c69be2dafc28]:  Make sure the GROUP BY binds
-- more tightly to the input tables in all cases.
--
-- This first case case has been wrong in sql for time out of mind.
-- For sql version 3.7.17 the answer was two rows, which is wrong.
--
test:do_execsql_test(
    "resolver01-5.1",
    [[
        CREATE TABLE t5(m VARCHAR(2) primary key);
        INSERT INTO t5 VALUES('ax');
        INSERT INTO t5 VALUES('bx');
        INSERT INTO t5 VALUES('cy');
        SELECT count(*), substr(m,2,1) AS m FROM t5 GROUP BY m ORDER BY 1, 2;
    ]], {
        -- <resolver01-5.1>
        1, "x", 1, "x", 1, "y"
        -- </resolver01-5.1>
    })

-- This case is unambiguous and has always been correct.
--
test:do_execsql_test(
    "resolver01-5.2",
    [[
        SELECT count(*), substr(m,2,1) AS mx FROM t5 GROUP BY m ORDER BY 1, 2;
    ]], {
        -- <resolver01-5.2>
        1, "x", 1, "x", 1, "y"
        -- </resolver01-5.2>
    })

-- This case is not allowed in standard SQL, but sql allows and does
-- the sensible thing.
--
test:do_execsql_test(
    "resolver01-5.3",
    [[
        SELECT count(*), substr(m,2,1) AS mx FROM t5 GROUP BY mx ORDER BY 1, 2;
    ]], {
        -- <resolver01-5.3>
        1, "y", 2, "x"
        -- </resolver01-5.3>
    })

test:do_execsql_test(
    "resolver01-5.4",
    [[
        SELECT count(*), substr(m,2,1) AS mx FROM t5
         GROUP BY substr(m,2,1) ORDER BY 1, 2;
    ]], {
        -- <resolver01-5.4>
        1, "y", 2, "x"
        -- </resolver01-5.4>
    })

-- These test case weere provided in the 2013-08-14 email from Rob Golsteijn
-- that originally reported the problem of ticket [1c69be2dafc28].
--
test:do_execsql_test(
    "resolver01-6.1",
    [[
        CREATE TABLE t61(name TEXT primary key);
        SELECT min(name) FROM t61 GROUP BY lower(name);
    ]], {
        -- <resolver01-6.1>
        
        -- </resolver01-6.1>
    })

test:do_execsql_test(
    "resolver01-6.2",
    [[
        SELECT min(name) AS name FROM t61 GROUP BY lower(name); 
    ]], {
        -- <resolver01-6.2>
        
        -- </resolver01-6.2>
    })

test:do_execsql_test(
    "resolver01-6.3",
    [[
        CREATE TABLE t63(id  INT primary key, name TEXT);
        INSERT INTO t63 VALUES (1, NULL);
        INSERT INTO t63 VALUES (2, 'abc');
        SELECT count(),
             NULLIF(name,'abc') AS name
          FROM t63
         GROUP BY lower(name);
    ]], {
        -- <resolver01-6.3>
        1, "", 1, ""
        -- </resolver01-6.3>
    })

test:do_execsql_test(
    "resolver01-7.1",
    [[
        SELECT 2 AS x WHERE (SELECT x AS y WHERE 3>y) <> 0;
    ]], {
        -- <resolver01-7.1>
        2
        -- </resolver01-7.1>
    })

test:do_execsql_test(
    "resolver01-7.2",
    [[
        SELECT 2 AS x WHERE (SELECT x AS y WHERE 1>y);
    ]], {
        -- <resolver01-7.2>
        
        -- </resolver01-7.2>
    })



test:finish_test()
