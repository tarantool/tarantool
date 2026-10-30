#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(22)

--!./tcltestrunner.lua
-- 2005 September 19
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.
--
-- This file implements tests for left outer joins containing ON
-- clauses that restrict the scope of the left term of the join.
--
-- $Id: join5.test,v 1.2 2007/06/08 00:20:48 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "join5-1.1",
    [[
        CREATE TABLE t1(a integer primary key, b integer, c integer);
        CREATE TABLE t2(x integer primary key, y TEXT);
        CREATE TABLE t3(p integer primary key, q TEXT);
        START TRANSACTION;
        INSERT INTO t3 VALUES(11,'t3-11');
        INSERT INTO t3 VALUES(12,'t3-12');
        INSERT INTO t2 VALUES(11,'t2-11');
        INSERT INTO t2 VALUES(12,'t2-12');
        INSERT INTO t1 VALUES(1, 5, 0);
        INSERT INTO t1 VALUES(2, 11, 2);
        INSERT INTO t1 VALUES(3, 12, 1);
        COMMIT;
    ]], {
        -- <join5-1.1>

        -- </join5-1.1>
    })

test:do_execsql_test(
    "join5-1.2",
    [[
        select * from t1 left join t2 on t1.b=t2.x and t1.c=1
    ]], {
        -- <join5-1.2>
        1, 5, 0, "", "", 2, 11, 2, "", "", 3, 12, 1, 12, "t2-12"
        -- </join5-1.2>
    })

test:do_execsql_test(
    "join5-1.3",
    [[
        select * from t1 left join t2 on t1.b=t2.x where t1.c=1
    ]], {
        -- <join5-1.3>
        3, 12, 1, 12, "t2-12"
        -- </join5-1.3>
    })

test:do_execsql_test(
    "join5-1.4",
    [[
        select * from t1 left join t2 on t1.b=t2.x and t1.c=1
                         left join t3 on t1.b=t3.p and t1.c=2
    ]], {
        -- <join5-1.4>
        1, 5, 0, "", "", "", "", 2, 11, 2, "", "", 11, "t3-11", 3, 12, 1, 12, "t2-12", "", ""
        -- </join5-1.4>
    })

test:do_execsql_test(
    "join5-1.5",
    [[
        select * from t1 left join t2 on t1.b=t2.x and t1.c=1
                         left join t3 on t1.b=t3.p where t1.c=2
    ]], {
        -- <join5-1.5>
        2, 11, 2, "", "", 11, "t3-11"
        -- </join5-1.5>
    })

-- Ticket #2403
--
test:do_test(
    "join5-2.1",
    function()
        test:execsql [[
            CREATE TABLE ab(a  INT primary key,b INT );
            INSERT INTO ab VALUES(1,2);
            INSERT INTO ab VALUES(3,NULL);

            CREATE TABLE xy(x INT ,y  INT primary key);
            INSERT INTO xy VALUES(2,3);
            INSERT INTO xy VALUES(NULL,1);
        ]]
        return test:execsql "SELECT * FROM xy LEFT JOIN ab ON false"
    end, {
        -- <join5-2.1>
        "", 1, "", "", 2, 3, "", ""
        -- </join5-2.1>
    })

test:do_execsql_test(
    "join5-2.2",
    [[
        SELECT * FROM xy LEFT JOIN ab ON true
    ]], {
        -- <join5-2.2>
        "", 1, 1, 2, "", 1, 3, "", 2, 3, 1, 2, 2, 3, 3, ""
        -- </join5-2.2>
    })

test:do_execsql_test(
    "join5-2.3",
    [[
        SELECT * FROM xy LEFT JOIN ab ON NULL
    ]], {
        -- <join5-2.3>
        "", 1, "", "", 2, 3, "", ""
        -- </join5-2.3>
    })

test:do_execsql_test(
    "join5-2.4",
    [[
        SELECT * FROM xy LEFT JOIN ab ON false WHERE false
    ]], {
        -- <join5-2.4>

        -- </join5-2.4>
    })

test:do_execsql_test(
    "join5-2.5",
    [[
        SELECT * FROM xy LEFT JOIN ab ON true WHERE false
    ]], {
        -- <join5-2.5>

        -- </join5-2.5>
    })

test:do_execsql_test(
    "join5-2.6",
    [[
        SELECT * FROM xy LEFT JOIN ab ON NULL WHERE false
    ]], {
        -- <join5-2.6>

        -- </join5-2.6>
    })

test:do_execsql_test(
    "join5-2.7",
    [[
        SELECT * FROM xy LEFT JOIN ab ON false WHERE true
    ]], {
        -- <join5-2.7>
        "", 1, "", "", 2, 3, "", ""
        -- </join5-2.7>
    })

test:do_execsql_test(
    "join5-2.8",
    [[
        SELECT * FROM xy LEFT JOIN ab ON true WHERE true
    ]], {
        -- <join5-2.8>
        "",1 ,1, 2, "", 1, 3, "", 2, 3, 1, 2, 2, 3, 3, ""
        -- </join5-2.8>
    })

test:do_execsql_test(
    "join5-2.9",
    [[
        SELECT * FROM xy LEFT JOIN ab ON NULL WHERE true
    ]], {
        -- <join5-2.9>
        "", 1, "", "", 2, 3, "", ""
        -- </join5-2.9>
    })

test:do_execsql_test(
    "join5-2.10",
    [[
        SELECT * FROM xy LEFT JOIN ab ON false WHERE NULL
    ]], {
        -- <join5-2.10>

        -- </join5-2.10>
    })

test:do_execsql_test(
    "join5-2.11",
    [[
        SELECT * FROM xy LEFT JOIN ab ON true WHERE NULL
    ]], {
        -- <join5-2.11>

        -- </join5-2.11>
    })

test:do_execsql_test(
    "join5-2.12",
    [[
        SELECT * FROM xy LEFT JOIN ab ON NULL WHERE NULL
    ]], {
        -- <join5-2.12>

        -- </join5-2.12>
    })

-- Ticket https://www.sql.org/src/tktview/6f2222d550f5b0ee7ed37601
-- Incorrect output on a LEFT JOIN.
--
test:do_execsql_test(
    "join5-3.1",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t3;
        CREATE TABLE x1(a  INT primary key);
        INSERT INTO x1 VALUES(1);
        CREATE TABLE x2(b TEXT NOT NULL primary key);
        CREATE TABLE x3(c TEXT primary key, d TEXT);
        INSERT INTO x3 VALUES('a', NULL);
        INSERT INTO x3 VALUES('b', NULL);
        INSERT INTO x3 VALUES('c', NULL);
        SELECT * FROM x1 LEFT JOIN x2 LEFT JOIN x3 ON x3.d = x2.b;
    ]], {
        -- <join5-3.1>
        1, "", "", ""
        -- </join5-3.1>
    })

test:do_execsql_test(
    "join5-3.2",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t3;
        DROP TABLE IF EXISTS t4;
        DROP TABLE IF EXISTS t5;
        CREATE TABLE t1(x text NOT NULL primary key, y text);
        CREATE TABLE t2(u text NOT NULL primary key, x text NOT NULL);
        CREATE TABLE t3(w text NOT NULL primary key, v text);
        CREATE TABLE t4(w text NOT NULL primary key, z text NOT NULL);
        CREATE TABLE t5(z text NOT NULL primary key, m text);
        INSERT INTO t1 VALUES('f6d7661f-4efe-4c90-87b5-858e61cd178b',NULL);
        INSERT INTO t1 VALUES('f6ea82c3-2cad-45ce-ae8f-3ddca4fb2f48',NULL);
        INSERT INTO t1 VALUES('f6f47499-ecb4-474b-9a02-35be73c235e5',NULL);
        INSERT INTO t1 VALUES('56f47499-ecb4-474b-9a02-35be73c235e5',NULL);
        INSERT INTO t3 VALUES('007f2033-cb20-494c-b135-a1e4eb66130c',
                              'f6d7661f-4efe-4c90-87b5-858e61cd178b');
        SELECT *
          FROM t3
               INNER JOIN t1 ON t1.x= t3.v AND t1.y IS NULL
               LEFT JOIN t4  ON t4.w = t3.w
               LEFT JOIN t5  ON t5.z = t4.z
               LEFT JOIN t2  ON t2.u = t5.m
               LEFT JOIN t1 xyz ON xyz.y = t2.x;
    ]], {
        -- <join5-3.2>
        "007f2033-cb20-494c-b135-a1e4eb66130c", "f6d7661f-4efe-4c90-87b5-858e61cd178b", "f6d7661f-4efe-4c90-87b5-858e61cd178b", "", "", "", "", "", "", "", "", ""
        -- </join5-3.2>
    })

test:do_execsql_test(
    "join5-3.3",
    [[
        DROP TABLE IF EXISTS x1;
        DROP TABLE IF EXISTS x2;
        DROP TABLE IF EXISTS x3;
        CREATE TABLE x1(a  INT primary key);
        INSERT INTO x1 VALUES(1);
        CREATE TABLE x2(b TEXT NOT NULL primary key);
        CREATE TABLE x3(c TEXT primary key, d INT );
        INSERT INTO x3 VALUES('a', NULL);
        INSERT INTO x3 VALUES('b', NULL);
        INSERT INTO x3 VALUES('c', NULL);
        SELECT * FROM x1 LEFT JOIN x2 JOIN x3 WHERE x3.d = x2.b;
    ]], {
        -- <join5-3.3>

        -- </join5-3.3>
    })

-- Ticket https://www.sql.org/src/tktview/c2a19d81652f40568c770c43 on
-- 2015-08-20.  LEFT JOIN and the push-down optimization.
--
test:do_execsql_test(
    "join6-4.1",
    [[
        SELECT *
        FROM (
            SELECT 'apple' fruit
            UNION ALL SELECT 'banana'
        ) a
        JOIN (
            SELECT 'apple' fruit
            UNION ALL SELECT 'banana'
        ) b ON a.fruit=b.fruit
        LEFT JOIN (
            SELECT 1 isyellow
        ) c ON b.fruit='banana';
    ]], {
        -- <join6-4.1>
        "apple", "apple", "", "banana", "banana", 1
        -- </join6-4.1>
    })

test:do_execsql_test(
    "join6-4.2",
    [[
        SELECT *
          FROM (SELECT 'apple' fruit UNION ALL SELECT 'banana')
               LEFT JOIN (SELECT 1) ON fruit='banana';
    ]], {
        -- <join6-4.2>
        "apple", "", "banana", 1
        -- </join6-4.2>
    })

test:finish_test()

