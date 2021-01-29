#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(60)

--!./tcltestrunner.lua
-- 2001 September 15.
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file implements regression tests for sql library.  The
-- focus of this file is testing the sorter (code in vdbesort.c).
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Create a bunch of data to sort against
--
test:do_test(
    "sort-1.0",
    function()
        test:execsql [[
            CREATE TABLE t1(
               n int PRIMARY KEY,
               v varchar(10),
               log int,
               roman varchar(10),
               flt NUMBER
            );
            INSERT INTO t1 VALUES(1,'one',0,'I',3.141592653);
            INSERT INTO t1 VALUES(2,'two',1,'II',2.15);
            INSERT INTO t1 VALUES(3,'three',1,'III',4221.0);
            INSERT INTO t1 VALUES(4,'four',2,'IV',-0.0013442);
            INSERT INTO t1 VALUES(5,'five',2,'V',-11);
            INSERT INTO t1 VALUES(6,'six',2,'VI',0.123);
            INSERT INTO t1 VALUES(7,'seven',2,'VII',123.0);
            INSERT INTO t1 VALUES(8,'eight',3,'VIII',-1.6);
        ]]
        return test:execsql "SELECT count(*) FROM t1"
    end, {
        -- <sort-1.0>
        8
        -- </sort-1.0>
    })

test:do_execsql_test(
    "sort-1.1",
    [[
        SELECT n FROM t1 ORDER BY n
    ]], {
        -- <sort-1.1>
        1, 2, 3, 4, 5, 6, 7, 8
        -- </sort-1.1>
    })

test:do_execsql_test(
    "sort-1.1.1",
    [[
        SELECT n FROM t1 ORDER BY n ASC
    ]], {
        -- <sort-1.1.1>
        1, 2, 3, 4, 5, 6, 7, 8
        -- </sort-1.1.1>
    })

test:do_execsql_test(
    "sort-1.1.1",
    [[
        SELECT ALL n FROM t1 ORDER BY n ASC
    ]], {
        -- <sort-1.1.1>
        1, 2, 3, 4, 5, 6, 7, 8
        -- </sort-1.1.1>
    })

test:do_execsql_test(
    "sort-1.2",
    [[
        SELECT n FROM t1 ORDER BY n DESC
    ]], {
        -- <sort-1.2>
        8, 7, 6, 5, 4, 3, 2, 1
        -- </sort-1.2>
    })

test:do_execsql_test(
    "sort-1.3a",
    [[
        SELECT v FROM t1 ORDER BY v
    ]], {
        -- <sort-1.3a>
        "eight", "five", "four", "one", "seven", "six", "three", "two"
        -- </sort-1.3a>
    })

test:do_execsql_test(
    "sort-1.3b",
    [[
        SELECT n FROM t1 ORDER BY v
    ]], {
        -- <sort-1.3b>
        8, 5, 4, 1, 7, 6, 3, 2
        -- </sort-1.3b>
    })

test:do_execsql_test(
    "sort-1.4",
    [[
        SELECT n FROM t1 ORDER BY v DESC
    ]], {
        -- <sort-1.4>
        2, 3, 6, 7, 1, 4, 5, 8
        -- </sort-1.4>
    })

test:do_execsql_test(
    "sort-1.5",
    [[
        SELECT flt FROM t1 ORDER BY flt
    ]], {
        -- <sort-1.5>
        -11.0, -1.6, -0.0013442, 0.123, 2.15, 3.141592653, 123.0, 4221.0
        -- </sort-1.5>
    })

test:do_execsql_test(
    "sort-1.6",
    [[
        SELECT flt FROM t1 ORDER BY flt DESC
    ]], {
        -- <sort-1.6>
        4221.0, 123.0, 3.141592653, 2.15, 0.123, -0.0013442, -1.6, -11.0
        -- </sort-1.6>
    })

test:do_execsql_test(
    "sort-1.7",
    [[
        SELECT roman FROM t1 ORDER BY roman
    ]], {
        -- <sort-1.7>
        "I", "II", "III", "IV", "V", "VI", "VII", "VIII"
        -- </sort-1.7>
    })

test:do_execsql_test(
    "sort-1.8",
    [[
        SELECT n FROM t1 ORDER BY log, flt
    ]], {
        -- <sort-1.8>
        1, 2, 3, 5, 4, 6, 7, 8
        -- </sort-1.8>
    })

test:do_execsql_test(
    "sort-1.8.1",
    [[
        SELECT n FROM t1 ORDER BY log asc, flt
    ]], {
        -- <sort-1.8.1>
        1, 2, 3, 5, 4, 6, 7, 8
        -- </sort-1.8.1>
    })

test:do_execsql_test(
    "sort-1.8.2",
    [[
        SELECT n FROM t1 ORDER BY log, flt ASC
    ]], {
        -- <sort-1.8.2>
        1, 2, 3, 5, 4, 6, 7, 8
        -- </sort-1.8.2>
    })

test:do_execsql_test(
    "sort-1.8.3",
    [[
        SELECT n FROM t1 ORDER BY log ASC, flt asc
    ]], {
        -- <sort-1.8.3>
        1, 2, 3, 5, 4, 6, 7, 8
        -- </sort-1.8.3>
    })

test:do_execsql_test(
    "sort-1.9",
    [[
        SELECT n FROM t1 ORDER BY log, flt DESC
    ]], {
        -- <sort-1.9>
        1, 3, 2, 7, 6, 4, 5, 8
        -- </sort-1.9>
    })

test:do_execsql_test(
    "sort-1.9.1",
    [[
        SELECT n FROM t1 ORDER BY log ASC, flt DESC
    ]], {
        -- <sort-1.9.1>
        1, 3, 2, 7, 6, 4, 5, 8
        -- </sort-1.9.1>
    })

test:do_execsql_test(
    "sort-1.10",
    [[
        SELECT n FROM t1 ORDER BY log DESC, flt
    ]], {
        -- <sort-1.10>
        8, 5, 4, 6, 7, 2, 3, 1
        -- </sort-1.10>
    })

test:do_execsql_test(
    "sort-1.11",
    [[
        SELECT n FROM t1 ORDER BY log DESC, flt DESC
    ]], {
        -- <sort-1.11>
        8, 7, 6, 4, 5, 3, 2, 1
        -- </sort-1.11>
    })

-- These tests are designed to reach some hard-to-reach places
-- inside the string comparison routines.
--
-- (Later) The sorting behavior changed in 2.7.0.  But we will
-- keep these tests.  You can never have too many test cases!
--
test:do_execsql_test(
    "sort-2.1.1",
    [[
        UPDATE t1 SET v='x' || CAST(-flt AS TEXT);
        UPDATE t1 SET v='x-2b' where v=='x-0.123';
        SELECT v FROM t1 ORDER BY v;
    ]], {
        -- <sort-2.1.1>
        "x-123.0", "x-2.15", "x-2b", "x-3.141592653", "x-4221.0", "x0.0013442", "x1.6", "x11"
        -- </sort-2.1.1>
    })

test:do_execsql_test(
    "sort-2.1.2",
    [[
        SELECT v FROM t1 ORDER BY substr(v,2,999);
    ]], {
        -- <sort-2.1.2>
        "x-123.0", "x-2.15", "x-2b", "x-3.141592653", "x-4221.0", "x0.0013442", "x1.6", "x11"
        -- </sort-2.1.2>
    })

test:do_execsql_test(
    "sort-2.1.4",
    [[
        SELECT v FROM t1 ORDER BY substr(v,2,999) DESC;
    ]], {
        -- <sort-2.1.4>
        "x11", "x1.6", "x0.0013442", "x-4221.0", "x-3.141592653", "x-2b", "x-2.15", "x-123.0"
        -- </sort-2.1.4>
    })

-- This is a bug fix for 2.2.4.
-- Strings are normally mapped to upper-case for a caseless comparison.
-- But this can cause problems for characters in between 'Z' and 'a'.
--
test:do_execsql_test(
    "sort-3.1",
    [[
        CREATE TABLE t2(a TEXT ,b  INT PRIMARY KEY);
        INSERT INTO t2 VALUES('AGLIENTU',1);
        INSERT INTO t2 VALUES('AGLIE`',2);
        INSERT INTO t2 VALUES('AGNA',3);
        SELECT a, b FROM t2 ORDER BY a;
    ]], {
        -- <sort-3.1>
        "AGLIENTU", 1, "AGLIE`", 2, "AGNA", 3
        -- </sort-3.1>
    })

test:do_execsql_test(
    "sort-3.2",
    [[
        SELECT a, b FROM t2 ORDER BY a DESC;
    ]], {
        -- <sort-3.2>
        "AGNA", 3, "AGLIE`", 2, "AGLIENTU", 1
        -- </sort-3.2>
    })

test:do_execsql_test(
    "sort-3.3",
    [[
        DELETE FROM t2;
        INSERT INTO t2 VALUES('aglientu',1);
        INSERT INTO t2 VALUES('aglie`',2);
        INSERT INTO t2 VALUES('agna',3);
        SELECT a, b FROM t2 ORDER BY a;
    ]], {
        -- <sort-3.3>
        "aglie`", 2, "aglientu", 1, "agna", 3
        -- </sort-3.3>
    })

test:do_execsql_test(
    "sort-3.4",
    [[
        SELECT a, b FROM t2 ORDER BY a DESC;
    ]], {
        -- <sort-3.4>
        "agna", 3, "aglientu", 1, "aglie`", 2
        -- </sort-3.4>
    })

-- Version 2.7.0 testing.
--
test:do_execsql_test(
    "sort-4.1",
    [[
        INSERT INTO t1 VALUES(9,'x2.7',3,'IX',4.0e5);
        INSERT INTO t1 VALUES(10,'x5.0e10',3,'X',-4.0e5);
        INSERT INTO t1 VALUES(11,'x-4.0e9',3,'XI',4.1e4);
        INSERT INTO t1 VALUES(12,'x01234567890123456789',3,'XII',-4.2e3);
        SELECT n FROM t1 ORDER BY n;
    ]], {
        -- <sort-4.1>
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
        -- </sort-4.1>
    })

test:do_execsql_test(
    "sort-4.2",
    [[
        SELECT CAST(n AS TEXT) || '' FROM t1 ORDER BY 1;
    ]], {
        -- <sort-4.2>
        "1", "10", "11", "12", "2", "3", "4", "5", "6", "7", "8", "9"
        -- </sort-4.2>
    })

test:do_execsql_test(
    "sort-4.3",
    [[
        SELECT n+0 FROM t1 ORDER BY 1;
    ]], {
        -- <sort-4.3>
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
        -- </sort-4.3>
    })

test:do_execsql_test(
    "sort-4.4",
    [[
        SELECT CAST(n AS TEXT) || '' FROM t1 ORDER BY 1 DESC;
    ]], {
        -- <sort-4.4>
        "9", "8", "7", "6", "5", "4", "3", "2", "12", "11", "10", "1"
        -- </sort-4.4>
    })

test:do_execsql_test(
    "sort-4.5",
    [[
        SELECT n+0 FROM t1 ORDER BY 1 DESC;
    ]], {
        -- <sort-4.5>
        12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1
        -- </sort-4.5>
    })

test:do_execsql_test(
    "sort-4.6",
    [[
        SELECT v FROM t1 ORDER BY 1;
    ]], {
        -- <sort-4.6>
        "x-123.0", "x-2.15", "x-2b", "x-3.141592653", "x-4.0e9", "x-4221.0", "x0.0013442", "x01234567890123456789", "x1.6", "x11", "x2.7", "x5.0e10"
        -- </sort-4.6>
    })

test:do_execsql_test(
    "sort-4.7",
    [[
        SELECT v FROM t1 ORDER BY 1 DESC;
    ]], {
        -- <sort-4.7>
        "x5.0e10", "x2.7", "x11", "x1.6", "x01234567890123456789", "x0.0013442", "x-4221.0", "x-4.0e9", "x-3.141592653", "x-2b", "x-2.15", "x-123.0"
        -- </sort-4.7>
    })

test:do_execsql_test(
    "sort-4.8",
    [[
        SELECT substr(v,2,99) FROM t1 ORDER BY 1;
    ]], {
        -- <sort-4.8>
    "-123.0","-2.15","-2b","-3.141592653","-4.0e9","-4221.0","0.0013442","01234567890123456789","1.6","11","2.7","5.0e10"
        -- </sort-4.8>
    })

--do_test sort-4.9 {
--  execsql {
--    SELECT substr(v,2,99)+0.0 FROM t1 ORDER BY 1;
--  }
--} {-4000000000 -4221 -123 -3.141592653 -2.15 -2 0.0013442 1.6 2.7 11 50000000000 1.23456789012346e+18}
test:do_execsql_test(
    "sort-5.1",
    [[
        create table t3(id  INT primary key, a INT ,b TEXT);
        insert into t3 values(1, 5,NULL);
        insert into t3 values(2, 6,NULL);
        insert into t3 values(3, 3,NULL);
        insert into t3 values(4, 4,'cd');
        insert into t3 values(5, 1,'ab');
        insert into t3 values(6, 2,NULL);
        select a from t3 order by b, a;
    ]], {
        -- <sort-5.1>
        2, 3, 5, 6, 1, 4
        -- </sort-5.1>
    })

test:do_execsql_test(
    "sort-5.2",
    [[
        select a from t3 order by b, a desc;
    ]], {
        -- <sort-5.2>
        6, 5, 3, 2, 1, 4
        -- </sort-5.2>
    })

test:do_execsql_test(
    "sort-5.3",
    [[
        select a from t3 order by b desc, a;
    ]], {
        -- <sort-5.3>
        4, 1, 2, 3, 5, 6
        -- </sort-5.3>
    })

test:do_execsql_test(
    "sort-5.4",
    [[
        select a from t3 order by b desc, a desc;
    ]], {
        -- <sort-5.4>
        4, 1, 6, 5, 3, 2
        -- </sort-5.4>
    })

test:do_execsql_test(
    "sort-6.1",
    [[
        create index i3 on t3(b,a);
        select a from t3 order by b, a;
    ]], {
        -- <sort-6.1>
        2, 3, 5, 6, 1, 4
        -- </sort-6.1>
    })

test:do_execsql_test(
    "sort-6.2",
    [[
        select a from t3 order by b, a desc;
    ]], {
        -- <sort-6.2>
        6, 5, 3, 2, 1, 4
        -- </sort-6.2>
    })

test:do_execsql_test(
    "sort-6.3",
    [[
        select a from t3 order by b desc, a;
    ]], {
        -- <sort-6.3>
        4, 1, 2, 3, 5, 6
        -- </sort-6.3>
    })

test:do_execsql_test(
    "sort-6.4",
    [[
        select a from t3 order by b desc, a desc;
    ]], {
        -- <sort-6.4>
        4, 1, 6, 5, 3, 2
        -- </sort-6.4>
    })

test:do_execsql_test(
    "sort-7.1",
    [[
        CREATE TABLE t4(
          a INTEGER PRIMARY KEY,
          b VARCHAR(30)
        );
        INSERT INTO t4 VALUES(1,'1');
        INSERT INTO t4 VALUES(2,'2');
        INSERT INTO t4 VALUES(11,'11');
        INSERT INTO t4 VALUES(12,'12');
        SELECT a FROM t4 ORDER BY 1;
    ]], {
        -- <sort-7.1>
        1, 2, 11, 12
        -- </sort-7.1>
    })

test:do_execsql_test(
    "sort-7.2",
    [[
        SELECT b FROM t4 ORDER BY 1
    ]], {
        -- <sort-7.2>
    "1","11","12","2"
        -- </sort-7.2>
    })

-- Omit tests sort-7.3 to sort-7.8 if view support was disabled at
-- compilatation time.
test:do_execsql_test(
    "sort-7.3",
    [[
        CREATE VIEW v4 AS SELECT * FROM t4;
        SELECT a FROM v4 ORDER BY 1;
    ]], {
        -- <sort-7.3>
    1,2,11,12
        -- </sort-7.3>
    })

test:do_execsql_test(
    "sort-7.4",
    [[
        SELECT b FROM v4 ORDER BY 1;
    ]], {
        -- <sort-7.4>
    "1","11","12","2"
        -- </sort-7.4>
    })

test:do_execsql_test(
    "sort-7.5",
    [[
        SELECT a FROM t4 UNION SELECT a FROM v4 ORDER BY 1;
    ]], {
        -- <sort-7.5>
        1, 2, 11, 12
        -- </sort-7.5>
    })

test:do_execsql_test(
    "sort-7.6",
    [[
        SELECT b FROM t4 UNION SELECT a FROM v4 ORDER BY 1;
    ]], {
        -- <sort-7.6>
    1,2,11,12,"1","11","12","2"
        -- </sort-7.6>
    })

-- text from t4.b and numeric from v4.a
test:do_execsql_test(
    "sort-7.7",
    [[
        SELECT a FROM t4 UNION SELECT b FROM v4 ORDER BY 1;
    ]], {
        -- <sort-7.7>
    1,2,11,12,"1","11","12","2"
        -- </sort-7.7>
    })

-- numeric from t4.a and text from v4.b
test:do_execsql_test(
    "sort-7.8",
    [[
        SELECT b FROM t4 UNION SELECT b FROM v4 ORDER BY 1;
    ]], {
        -- <sort-7.8>
    "1","11","12","2"
        -- </sort-7.8>
    })



-- ifcapable compound


-- ifcapable view
--### Version 3 works differently here:
--do_test sort-7.9 {
--  execsql {
--    SELECT b FROM t4 UNION SELECT b FROM v4 ORDER BY 1 COLLATE numeric;
--  }
--} {1 2 11 12}
--do_test sort-7.10 {
--  execsql {
--    SELECT b FROM t4 UNION SELECT b FROM v4 ORDER BY 1 COLLATE integer;
--  }
--} {1 2 11 12}
--do_test sort-7.11 {
--  execsql {
--    SELECT b FROM t4 UNION SELECT b FROM v4 ORDER BY 1 COLLATE text;
--  }
--} {1 11 12 2}
--do_test sort-7.12 {
--  execsql {
--    SELECT b FROM t4 UNION SELECT b FROM v4 ORDER BY 1 COLLATE blob;
--  }
--} {1 11 12 2}
--do_test sort-7.13 {
--  execsql {
--    SELECT b FROM t4 UNION SELECT b FROM v4 ORDER BY 1 COLLATE clob;
--  }
--} {1 11 12 2}
--do_test sort-7.14 {
--  execsql {
--    SELECT b FROM t4 UNION SELECT b FROM v4 ORDER BY 1 COLLATE varchar;
--  }
--} {1 11 12 2}
-- Ticket #297
--
test:do_execsql_test(
    "sort-8.1",
    [[
        CREATE TABLE t5(a NUMBER, b text PRIMARY KEY);
        INSERT INTO t5 VALUES(100,'A1');
        INSERT INTO t5 VALUES(100.0,'A2');
        SELECT * FROM t5 ORDER BY a, b;
    ]], {
        -- <sort-8.1>
        100.0, "A1", 100.0, "A2"
        -- </sort-8.1>
    })


-- endif bloblit
-- Ticket #1092 - ORDER BY on rowid fields.
test:do_execsql_test(
    "sort-10.1",
    [[
        CREATE TABLE t7(c INTEGER PRIMARY KEY);
        INSERT INTO t7 VALUES(1);
        INSERT INTO t7 VALUES(2);
        INSERT INTO t7 VALUES(3);
        INSERT INTO t7 VALUES(4);
    ]], {
        -- <sort-10.1>

        -- </sort-10.1>
    })

test:do_execsql_test(
    "sort-10.2",
    [[
        SELECT c FROM t7 WHERE c<=3 ORDER BY c DESC;
    ]], {
        -- <sort-10.2>
        3, 2, 1
        -- </sort-10.2>
    })

test:do_execsql_test(
    "sort-10.3",
    [[
        SELECT c FROM t7 WHERE c<3 ORDER BY c DESC;
    ]], {
        -- <sort-10.3>
        2, 1
        -- </sort-10.3>
    })

-- ticket #1358.  Just because one table in a join gives a unique
-- result does not mean they all do.  We cannot disable sorting unless
-- all tables in the join give unique results.
--
test:do_execsql_test(
    "sort-11.1",
    [[
        create table t8(a  INT PRIMARY KEY, b INT , c INT );
        insert into t8 values(1,2,3);
        insert into t8 values(2,3,4);
        create table t9(id  INT primary key, x INT ,y INT );
        insert into t9 values(1, 2,4);
        insert into t9 values(2, 2,3);
        select y from t8, t9 where a=1 order by a, y;
    ]], {
        -- <sort-11.1>
        3, 4
        -- </sort-11.1>
    })

-- Trouble reported on the mailing list.  Check for overly aggressive
-- (which is to say, incorrect) optimization of order-by with a rowid
-- in a join.
--
test:do_execsql_test(
    "sort-12.1",
    [[
        create table a (id integer primary key);
        create table b (id integer primary key, aId integer, "text" text);
        insert into a values (1);
        insert into b values (2, 1, 'xxx');
        insert into b values (1, 1, 'zzz');
        insert into b values (3, 1, 'yyy');
        select a.id, b.id, b."text" from a join b on (a.id = b.aId)
          order by a.id, b."text";
    ]], {
        -- <sort-12.1>
        1, 2, "xxx", 1, 3, "yyy", 1, 1, "zzz"
        -- </sort-12.1>
    })

---------------------------------------------------------------------------
-- Check that the sorter in vdbesort.c sorts in a stable fashion.
--
test:do_execsql_test(
    "sort-13.0",
    [[
        CREATE TABLE t10(id  INT primary key, a INT , b INT );
    ]])

test:do_test(
    "sort-13.1",
    function()
        test:execsql("START TRANSACTION;")
        for i = 0, 100000 -1 , 1 do
            test:execsql( string.format("INSERT INTO t10 VALUES(%s +1, %s/10, %s%%10)",i, i, i))
        end
        test:execsql("COMMIT;")
    end, {
        -- <sort-13.1>

        -- </sort-13.1>
    })

test:do_execsql_test(
    "sort-13.2",
    [[
        SELECT a, b FROM t10 ORDER BY a;
    ]], test:execsql "SELECT a, b FROM t10 ORDER BY a, b")

test:do_execsql_test(
    "sort-13.3",
    [[
        SELECT a, b FROM t10 ORDER BY a;
    ]], test:execsql "SELECT a, b FROM t10 ORDER BY a, b")

---------------------------------------------------------------------------
-- Sort some large ( > 4KiB) records.
--
-- MUST_WORK_TEST? special sql functions (sql_soft_heap_limit, sql_test_control...)
if (0 > 0) then
-- Legacy from the original code. Must be replaced with analogue
-- functions from box.
local X = nil
local function cksum(x) -- luacheck: no unused
    local i1 = 1
    local i2 = 2
    X(503, "X!cmd", [=[["binary","scan",["x"],"c*","L"]]=])
    for _ in X(0, "X!foreach", [=[["a b",["L"]]]=]) do
        i1 = X(0, "X!expr", [=[["&",["+",["<<",["i2"],3],["a"]],2147483647]]=])
        i2 = X(0, "X!expr", [=[["&",["+",["<<",["i1"],3],["b"]],2147483647]]=])
    end
    return i1, i2
end
box.internal.sql_create_function("cksum", cksum)

    test:do_execsql_test(
        "sort-14.0",
        [[
            CREATE TABLE t11(a INT , b INT );
            INSERT INTO t11 VALUES(randomblob(5000), NULL);
            INSERT INTO t11 SELECT randomblob(5000), NULL FROM t11; --2
            INSERT INTO t11 SELECT randomblob(5000), NULL FROM t11; --3
            INSERT INTO t11 SELECT randomblob(5000), NULL FROM t11; --4
            INSERT INTO t11 SELECT randomblob(5000), NULL FROM t11; --5
            INSERT INTO t11 SELECT randomblob(5000), NULL FROM t11; --6
            INSERT INTO t11 SELECT randomblob(5000), NULL FROM t11; --7
            INSERT INTO t11 SELECT randomblob(5000), NULL FROM t11; --8
            INSERT INTO t11 SELECT randomblob(5000), NULL FROM t11; --9
            UPDATE t11 SET b = cksum(a);
        ]])

    -- Legacy from the original code. Must be replaced with analogue
    -- functions from box.
    local tn = nil
    local sql_test_control = nil
    local mmap_limit = nil
    for _ in X(0, "X!foreach", [=[["tn mmap_limit","\n     1 0\n     2 1000000\n   "]]=]) do
        test:do_test(
            "sort-14."..tn,
            function()
                sql_test_control("sql_TESTCTRL_SORTER_MMAP", "db", mmap_limit)
                local prev = "" -- luacheck: no unused
                X(536, "X!cmd", [=[["db","eval"," SELECT * FROM t11 ORDER BY b ","\n         if {$b != [cksum $a]} {error \"checksum failed\"}\n         if {[string compare $b $prev] < 0} {error \"sort failed\"}\n         set prev $b\n       "]]=])
                return X(541, "X!cmd", [=[["set","",""]]=])
            end, {

            })

    end
    ---------------------------------------------------------------------------
    --
    -- Legacy from the original code. Must be replaced with analogue
    -- functions from box.
    local coremutex = nil
    local sql_config = nil
    local sql_initialize = nil
    local sql_soft_heap_limit = nil
    local tmpstore = nil
    local softheaplimit = nil
    local nWorker = nil
    for _ in X(0, "X!foreach", [=[["tn mmap_limit nWorker tmpstore coremutex fakeheap softheaplimit","\n             1          0       3     file      true    false             0\n             2          0       3     file      true     true             0\n             3          0       0     file      true    false             0\n             4    1000000       3     file      true    false             0\n             5          0       0   memory     false     true             0\n             6          0       0     file     false     true       1000000     \n             7          0       0     file     false     true         10000\n   "]]=]) do
        if coremutex then
            sql_config("multithread")
        else
            sql_config("singlethread")
        end
        sql_initialize()
        X(558, "X!cmd", [=[["sorter_test_fakeheap",["fakeheap"]]]=])
        sql_soft_heap_limit(softheaplimit)
        sql_test_control("sql_TESTCTRL_SORTER_MMAP", "db", mmap_limit)
        test:execsql(string.format("PRAGMA temp_store = %s; PRAGMA threads = '%s'", tmpstore, nWorker))
        local ten, one
        ten = string.rep("X", 10300)
        one = string.rep("y", 200)
        if softheaplimit then
            test:execsql " PRAGMA cache_size = 20 "
        else
            test:execsql " PRAGMA cache_size = 5 "
        end
        test:do_execsql_test(
            "15."..tn..".1",
            [[
                WITH rr AS (
                  SELECT 4, $ten UNION ALL
                  SELECT 2, $one UNION ALL
                  SELECT 1, $ten UNION ALL
                  SELECT 3, $one
                )
                SELECT * FROM rr ORDER BY 1;
            ]], {
                1, ten, 2, one, 3, one, 4, ten
            })

        test:do_execsql_test(
            "15."..tn..".2",
            [[
                CREATE TABLE t1(a  INT primary key);
                INSERT INTO t1 VALUES(4);
                INSERT INTO t1 VALUES(5);
                INSERT INTO t1 VALUES(3);
                INSERT INTO t1 VALUES(2);
                INSERT INTO t1 VALUES(6);
                INSERT INTO t1 VALUES(1);
                CREATE INDEX i1 ON t1(a);
                SELECT * FROM t1 ORDER BY a;
            ]], {
                1, 2, 3, 4, 5, 6
            })

        test:do_execsql_test(
            "15."..tn..".3",
            [[
                WITH rr AS (
                  SELECT 4, $ten UNION ALL
                  SELECT 2, $one
                )
                SELECT * FROM rr ORDER BY 1;
            ]], {
                2, one, 4, ten
            })

        X(605, "X!cmd", [=[["sorter_test_fakeheap","0"]]=])
    end
    X(617, "X!cmd", [=[["set","t(0)","singlethread"]]=])
    X(618, "X!cmd", [=[["set","t(1)","multithread"]]=])
    X(619, "X!cmd", [=[["set","t(2)","serialized"]]=])
    sql_config(X(620, "X!expr", [=[["t($sql_options(threadsafe))"]]=]))
    sql_initialize()
    sql_soft_heap_limit(0)
    test:do_catchsql_test(
        16.1,
        [[
            CREATE TABLE t1(a INT , b INT , c INT );
            INSERT INTO t1 VALUES(1, 2, 3);
            INSERT INTO t1 VALUES(1, NULL, 3);
            INSERT INTO t1 VALUES(NULL, 2, 3);
            INSERT INTO t1 VALUES(1, 2, NULL);
            INSERT INTO t1 VALUES(4, 5, 6);
            CREATE UNIQUE INDEX i1 ON t1(b, a, c);
        ]], {
            -- <16.1>
            0, ""
            -- </16.1>
        })

    test:do_catchsql_test(
        16.2,
        [[
            CREATE TABLE t1(a INT , b INT , c INT );
            INSERT INTO t1 VALUES(1, 2, 3);
            INSERT INTO t1 VALUES(1, NULL, 3);
            INSERT INTO t1 VALUES(1, 2, 3);
            INSERT INTO t1 VALUES(1, 2, NULL);
            INSERT INTO t1 VALUES(4, 5, 6);
            CREATE UNIQUE INDEX i1 ON t1(b, a, c);
        ]], {
            -- <16.2>
            1, "UNIQUE constraint failed: t1.b, t1.a, t1.c"
            -- </16.2>
        })

    test:do_execsql_test(
        17.1,
        [[
            SELECT * FROM sql_master ORDER BY sql;
        ]], {
            -- <17.1>

            -- </17.1>
        })

end
test:finish_test()
