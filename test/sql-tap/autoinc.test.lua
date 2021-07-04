#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(57)

--!./tcltestrunner.lua
-- 2004 November 12
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
---------------------------------------------------------------------------
-- This file implements regression tests for sql library.  The
-- focus of this script is testing the AUTOINCREMENT features.
--
-- $Id: autoinc.test,v 1.14 2009/06/23 20:28:54 drh Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- If the library is not compiled with autoincrement support then
-- skip all tests in this file.
--

-- Add a table with the AUTOINCREMENT feature.  Verify that the
-- sql_SEQUENCE table gets created.
--
test:do_execsql_test(
    "autoinc-1.2",
    [[
        CREATE TABLE t1(x INTEGER PRIMARY KEY AUTOINCREMENT, y INT );
    ]], {
        -- <autoinc-1.2>

        -- </autoinc-1.2>
    })

test:do_execsql_test(
    "autoinc-1.3",
    [[
        SELECT * FROM "_sequence";
    ]], {
        -- <autoinc-1.3>
        1,1,"T1",1,0,9223372036854775807LL,1,0,false
        -- </autoinc-1.3>
    })

-- Close and reopen the database.  Verify that everything is still there.
--
-- test:do_test(
--     "autoinc-1.4",
--     function()
--         return test:execsql([[
--             SELECT * FROM _sequence;
--         ]])
--     end, {
--         -- <autoinc-1.4>

--         -- </autoinc-1.4>
--     })

-- We are not allowed to drop the sql_sequence table.
--
test:do_catchsql_test(
    "autoinc-1.5",
    [[
        DROP TABLE "_sequence"
    ]], {
        -- <autoinc-1.5>
        1, "/system space/"
        -- </autoinc-1.5>
    })

test:do_execsql_test(
    "autoinc-1.6",
    [[
        SELECT "name" FROM "_space" WHERE "name" NOT IN (SELECT "name" FROM "_space" WHERE "name" LIKE '\_%' ESCAPE '\')
    ]], {
        -- <autoinc-1.6>
        "T1"
        -- </autoinc-1.6>
    })

-- Insert an entries into the t1 table and make sure the largest key
-- is always recorded in the sql_sequence table.
--
test:do_execsql_test(
    "autoinc-2.1",
    [[
        --SELECT * FROM _sequence
    ]], {
        -- <autoinc-2.1>

        -- </autoinc-2.1>
    })

test:do_execsql_test(
    "autoinc-2.2",
    [[
        INSERT INTO t1 VALUES(12,34);
    ]], {
        -- <autoinc-2.2>
        -- </autoinc-2.2>
    })

test:do_execsql_test(
    "autoinc-2.3",
    [[
        INSERT INTO t1 VALUES(1,23);
    ]], {
        -- <autoinc-2.3>
        -- </autoinc-2.3>
    })

test:do_execsql_test(
    "autoinc-2.4",
    [[
        INSERT INTO t1 VALUES(123,456);
    ]], {
        -- <autoinc-2.4>
        -- </autoinc-2.4>
    })

test:do_execsql_test(
    "autoinc-2.5",
    [[
        INSERT INTO t1 VALUES(NULL,567);
        SELECT MAX(x) FROM t1;
    ]], {
        -- <autoinc-2.5>
        124
        -- </autoinc-2.5>
    })

test:do_execsql_test(
    "autoinc-2.6",
    [[
        DELETE FROM t1 WHERE y=567;
        SELECT max(x) FROM t1;
    ]], {
        -- <autoinc-2.6>
        123
        -- </autoinc-2.6>
    })

test:do_execsql_test(
    "autoinc-2.7",
    [[
        INSERT INTO t1 VALUES(NULL,567);
        SELECT max(x) FROM t1;
    ]], {
        -- <autoinc-2.7>
        125
        -- </autoinc-2.7>
    })

test:do_execsql_test(
    "autoinc-2.8",
    [[
        DELETE FROM t1;
    ]], {
        -- <autoinc-2.8>
        -- </autoinc-2.8>
    })

test:do_execsql_test(
    "autoinc-2.9",
    [[
        INSERT INTO t1 VALUES(12,34);
        SELECT max(x) FROM t1;
    ]], {
        -- <autoinc-2.9>
        12
        -- </autoinc-2.9>
    })

test:do_execsql_test(
    "autoinc-2.10",
    [[
        INSERT INTO t1 VALUES(125,456);
        SELECT max(x) FROM t1;
    ]], {
        -- <autoinc-2.10>
        125
        -- </autoinc-2.10>
    })

test:do_execsql_test(
    "autoinc-2.11",
    [[
        INSERT INTO t1 VALUES(-1234567,-1);
        SELECT max(x) FROM t1;
    ]], {
        -- <autoinc-2.11>
        125
        -- </autoinc-2.11>
    })

test:do_execsql_test(
    "autoinc-2.12",
    [[
        INSERT INTO t1 VALUES(234,5678);
        SELECT max(x) FROM t1;
    ]], {
        -- <autoinc-2.12>
        234
        -- </autoinc-2.12>
    })

-- Autoincrement is now handled inside Tarantool
test:do_execsql_test(
    "autoinc-2.13",
    [[
        DELETE FROM t1;
        INSERT INTO t1 VALUES(NULL,1);
    ]], {
        -- <autoinc-2.13>
        -- </autoinc-2.13>
    })

test:do_execsql_test(
    "autoinc-2.14",
    [[
        SELECT * FROM t1;
    ]], {
        -- <autoinc-2.14>
        235, 1
        -- </autoinc-2.14>
    })

-- # Manually change the autoincrement values in sql_sequence.
-- #
test:do_execsql_test(
    "autoinc-2.20",
    [[
        INSERT INTO t1 VALUES(NULL,2);
        SELECT * FROM t1;
    ]], {
        -- <autoinc-2.20>
        235, 1, 236, 2
        -- </autoinc-2.20>
    })

-- # Test multi-row inserts
-- #
test:do_execsql_test(
    "autoinc-2.50",
    [[
        DELETE FROM t1 WHERE y>=3;
        INSERT INTO t1 SELECT NULL, y+2 FROM t1;
        SELECT * FROM t1;
    ]], {
        -- <autoinc-2.50>
        235, 1, 236, 2, 237, 3, 238, 4
        -- </autoinc-2.50>
    })

-- # Create multiple AUTOINCREMENT tables.  Make sure all sequences are
-- # tracked separately and do not interfere with one another.
-- #
test:do_test(
    "autoinc-2.70",
    function()
        test:catchsql([[
            DROP TABLE t2;
        ]])
        return test:execsql([[
            CREATE TABLE t2(d INT , e INTEGER PRIMARY KEY AUTOINCREMENT, f INT );
            INSERT INTO t2(d) VALUES(1);
        ]])
    end, {
        -- <autoinc-2.70>
        -- </autoinc-2.70>
    })

test:do_execsql_test(
    "autoinc-2.71",
    [[
        INSERT INTO t2(d) VALUES(2);
        SELECT max(x) FROM t1 UNION SELECT max(e) FROM t2;
    ]], {
        -- <autoinc-2.71>
        2, 238
        -- </autoinc-2.71>
    })

test:do_execsql_test(
    "autoinc-2.72",
    [[
        INSERT INTO t1(x) VALUES(10000);
        SELECT max(x) FROM t1 UNION SELECT max(e) FROM t2;
    ]], {
        -- <autoinc-2.72>
        2, 10000
        -- </autoinc-2.72>
    })

test:do_execsql_test(
    "autoinc-2.73",
    [[
        CREATE TABLE t3(g INTEGER PRIMARY KEY AUTOINCREMENT, h INT );
        INSERT INTO t3(h) VALUES(1);
        SELECT max(x) FROM t1 UNION SELECT max(e) FROM t2
          UNION SELECT max(g) FROM t3;
    ]], {
        -- <autoinc-2.73>
        1, 2, 10000
        -- </autoinc-2.73>
    })

test:do_execsql_test(
    "autoinc-2.74",
    [[
        INSERT INTO t2(d,e) VALUES(3,100);
        SELECT max(x) FROM t1 UNION SELECT max(e) FROM t2
          UNION SELECT max(g) FROM t3;
    ]], {
        -- <autoinc-2.74>
        1, 100, 10000
        -- </autoinc-2.74>
    })

-- When a table with an AUTOINCREMENT is deleted, the corresponding entry
-- in the sql_SEQUENCE table should also be deleted.  But the sql_SEQUENCE
-- table itself should remain behind.
--
test:do_execsql_test(
    "autoinc-3.1",
    [[
        SELECT "name" FROM "_sequence"
    ]], {
        -- <autoinc-3.1>
        "T1", "T2", "T3"
        -- </autoinc-3.1>
    })

test:do_execsql_test(
    "autoinc-3.2",
    [[
        DROP TABLE t1;
        SELECT "name" FROM "_sequence";
    ]], {
        -- <autoinc-3.2>
       "T2", "T3"
        -- </autoinc-3.2>
    })

test:do_execsql_test(
    "autoinc-3.3",
    [[
        DROP TABLE t3;
        SELECT "name" FROM "_sequence";
    ]], {
        -- <autoinc-3.3>
        "T2"
        -- </autoinc-3.3>
    })

test:do_execsql_test(
    "autoinc-3.4",
    [[
        DROP TABLE t2;
        SELECT "name" FROM "_sequence";
    ]], {
        -- <autoinc-3.4>

        -- </autoinc-3.4>
    })

-- AUTOINCREMENT on TEMP tables.
--
-- Tarantool: TEMP tables are not supported yet. To be uncommented. #2119
-- test:do_execsql_test(
--     "autoinc-4.1",
--     [[
--         SELECT 1, name FROM sql_master WHERE type='table';
--         SELECT 2, name FROM sql_temp_master WHERE type='table';
--     ]], {
--         -- <autoinc-4.1>
--         1, "sql_sequence"
--         -- </autoinc-4.1>
--     })

-- test:do_execsql_test(
--     "autoinc-4.2",
--     [[
--         CREATE TABLE t1(x INTEGER PRIMARY KEY AUTOINCREMENT, y INT );
--         CREATE TEMP TABLE t3(a INTEGER PRIMARY KEY AUTOINCREMENT, b);
--         SELECT 1, name FROM sql_master WHERE type='table';
--         SELECT 2, name FROM sql_temp_master WHERE type='table';
--     ]], {
--         -- <autoinc-4.2>
--         1, "sql_sequence", 1, "t1", 2, "t3", 2, "sql_sequence"
--         -- </autoinc-4.2>
--     })

-- test:do_execsql_test(
--     "autoinc-4.3",
--     [[
--         SELECT 1, * FROM main.sql_sequence;
--         SELECT 2, * FROM temp.sql_sequence;
--     ]], {
--         -- <autoinc-4.3>

--         -- </autoinc-4.3>
--     })

-- test:do_execsql_test(
--     "autoinc-4.4",
--     [[
--         INSERT INTO t1 VALUES(10,1);
--         INSERT INTO t3 VALUES(20,2);
--         INSERT INTO t1 VALUES(NULL,3);
--         INSERT INTO t3 VALUES(NULL,4);
--     ]], {
--         -- <autoinc-4.4>

--         -- </autoinc-4.4>
--     })

-- test:do_execsql_test(
--     "autoinc-4.4.1",
--     [[
--         SELECT * FROM t1 UNION ALL SELECT * FROM t3;
--     ]], {
--         -- <autoinc-4.4.1>
--         10, 1, 11, 3, 20, 2, 21, 4
--         -- </autoinc-4.4.1>
--     })



-- -- ifcapable compound
-- test:do_execsql_test(
--     "autoinc-4.5",
--     [[
--         SELECT 1, * FROM main.sql_sequence;
--         SELECT 2, * FROM temp.sql_sequence;
--     ]], {
--         -- <autoinc-4.5>
--         1, "t1", 11, 2, "t3", 21
--         -- </autoinc-4.5>
--     })

-- test:do_execsql_test(
--     "autoinc-4.6",
--     [[
--         INSERT INTO t1 SELECT * FROM t3;
--         SELECT 1, * FROM main.sql_sequence;
--         SELECT 2, * FROM temp.sql_sequence;
--     ]], {
--         -- <autoinc-4.6>
--         1, "t1", 21, 2, "t3", 21
--         -- </autoinc-4.6>
--     })

-- test:do_execsql_test(
--     "autoinc-4.7",
--     [[
--         INSERT INTO t3 SELECT x+100, y  FROM t1;
--         SELECT 1, * FROM main.sql_sequence;
--         SELECT 2, * FROM temp.sql_sequence;
--     ]], {
--         -- <autoinc-4.7>
--         1, "t1", 21, 2, "t3", 121
--         -- </autoinc-4.7>
--     })

-- test:do_execsql_test(
--     "autoinc-4.8",
--     [[
--         DROP TABLE t3;
--         SELECT 1, * FROM main.sql_sequence;
--         SELECT 2, * FROM temp.sql_sequence;
--     ]], {
--         -- <autoinc-4.8>
--         1, "t1", 21
--         -- </autoinc-4.8>
--     })

-- test:do_execsql_test(
--     "autoinc-4.9",
--     [[
--         CREATE TEMP TABLE t2(p INTEGER PRIMARY KEY AUTOINCREMENT, q);
--         INSERT INTO t2 SELECT * FROM t1;
--         DROP TABLE t1;
--         SELECT 1, * FROM main.sql_sequence;
--         SELECT 2, * FROM temp.sql_sequence;
--     ]], {
--         -- <autoinc-4.9>
--         2, "t2", 21
--         -- </autoinc-4.9>
--     })

-- test:do_execsql_test(
--     "autoinc-4.10",
--     [[
--         DROP TABLE t2;
--         SELECT 1, * FROM main.sql_sequence;
--         SELECT 2, * FROM temp.sql_sequence;
--     ]], {
--         -- <autoinc-4.10>

--         -- </autoinc-4.10>
--     })



-- Requirement REQ00310:  Make sure an insert fails if the sequence is
-- already at its maximum value.
--
test:do_execsql_test(
    "autoinc-6.1",
    [[
        CREATE TABLE t6(v INTEGER PRIMARY KEY AUTOINCREMENT, w INT );
        INSERT INTO t6 VALUES(9223372036854775807,1);
        SELECT max(v) FROM t6;
    ]], {
        -- <autoinc-6.1>
        9223372036854775807LL
        -- </autoinc-6.1>
    })

test:do_catchsql_test(
    "autoinc-6.2",
    [[
        INSERT INTO t6 VALUES(NULL,1);
    ]], {
        -- <autoinc-6.2>
        1, "Sequence 'T6' has overflowed"
        -- </autoinc-6.2>
    })

-- Allow the AUTOINCREMENT keyword inside the parentheses
-- on a separate PRIMARY KEY designation.
--
test:do_execsql_test(
    "autoinc-7.1",
    [[
        CREATE TABLE t7(x INTEGER, y NUMBER, PRIMARY KEY(x AUTOINCREMENT));
        INSERT INTO t7(y) VALUES(123);
        INSERT INTO t7(y) VALUES(234);
        DELETE FROM t7;
        INSERT INTO t7(y) VALUES(345);
        SELECT * FROM t7;
    ]], {
        -- <autoinc-7.1>
        3, 345.0
        -- </autoinc-7.1>
    })

-- Test that if the AUTOINCREMENT is applied to a non integer primary key
-- the error message is sensible.
test:do_catchsql_test(
    "autoinc-7.2",
    [[
        CREATE TABLE t8(x TEXT PRIMARY KEY AUTOINCREMENT);
    ]], {
        -- <autoinc-7.2>
        1, "Can't create or modify index 'pk_unnamed_T8_1' in space 'T8': sequence cannot be used with a non-integer key"
        -- </autoinc-7.2>
    })

-- Ticket #3148
-- Make sure the sql_sequence table is not damaged when doing
-- an empty insert - an INSERT INTO ... SELECT ... where the SELECT
-- clause returns an empty set.
--
test:do_test(
    "autoinc-9.1",
    function()
        return test:execsql([[
            CREATE TABLE t2(x INTEGER PRIMARY KEY AUTOINCREMENT, y INT );
            INSERT INTO t2 VALUES(NULL, 1);
            CREATE TABLE t3(a INTEGER PRIMARY KEY AUTOINCREMENT, b INT );
            INSERT INTO t3 SELECT * FROM t2 WHERE y>1;

            SELECT max(a) FROM t3;
        ]])
    end, {
        -- <autoinc-9.1>
        ""
        -- </autoinc-9.1>
    })

-- Issue #2999.
-- Make sure that INT is synonim for INTEGER. In this respect this:
-- "INT PRIMARY KEY AUTOINCREMET" should not faild and be equal to
-- "INTEGER PRIMARY KEY AUTOINCREMENT".
--
test:do_test(
    "autoinc-10.1",
    function()
        return test:execsql([[
            DROP TABLE IF EXISTS t2;
            CREATE TABLE t2(x INT PRIMARY KEY AUTOINCREMENT, y INT);
            INSERT INTO t2 VALUES(NULL, 1);
            INSERT INTO t2 VALUES(NULL, 2);
            INSERT INTO t2 VALUES(NULL, 3);
            INSERT INTO t2 VALUES(NULL, 4);
            SELECT * FROM t2;
        ]])
    end, {
        -- <autoinc-10.1>
        1, 1, 2, 2, 3, 3, 4, 4
        -- </autoinc-10.1>
    })

test:do_catchsql_test(
    "autoinc-10.2",
    [[
            DROP TABLE IF EXISTS t2;
            CREATE TABLE t2(x INT PRIMARY KEY AUTOINCREMENT);
            INSERT INTO t2 VALUES('asd');
    ]], {
        -- <autoinc-10.2>
        1, "Type mismatch: can not convert string('asd') to integer"
        -- </autoinc-10.2>
    })

test:do_test(
    "autoinc-10.3",
    function()
        return test:execsql([[
            DROP TABLE IF EXISTS t7;
            CREATE TABLE t7(x INT, y NUMBER, PRIMARY KEY(x AUTOINCREMENT));
            INSERT INTO t7(y) VALUES(123);
            INSERT INTO t7(y) VALUES(234);
            DELETE FROM t7;
            INSERT INTO t7(y) VALUES(345);
            SELECT * FROM t7;
        ]])
    end, {
        -- <autoinc-10.3>
        3, 345.0
        -- </autoinc-10.3>
    })

box.space._session_settings:update('sql_recursive_triggers', {{'=', 2, false}})

-- Ticket #3928.  Make sure that triggers to not make extra slots in
-- the sql_SEQUENCE table.
--
test:do_test(
    "autoinc-3928.1",
    function()
        return test:execsql([[
            CREATE TABLE t3928(a INTEGER PRIMARY KEY AUTOINCREMENT, b TEXT);
            CREATE TRIGGER t3928r1 BEFORE INSERT ON t3928 FOR EACH ROW BEGIN
              INSERT INTO t3928(b) VALUES('before1');
              INSERT INTO t3928(b) VALUES('before2');
            END;
            CREATE TRIGGER t3928r2 AFTER INSERT ON t3928 FOR EACH ROW BEGIN
              INSERT INTO t3928(b) VALUES('after1');
              INSERT INTO t3928(b) VALUES('after2');
            END;
            INSERT INTO t3928(b) VALUES('test');
            SELECT * FROM t3928 ORDER BY a;
        ]])
    end, {
        -- <autoinc-3928.1>
        1, "before1", 2, "after1", 3, "after2", 4, "before2", 5, "after1", 6, "after2", 7, "test", 8, "before1", 9, "before2", 10, "after1", 11, "before1", 12, "before2", 13, "after2"
        -- </autoinc-3928.1>
    })

test:do_test(
    "autoinc-3928.2",
    function()
        return test:execsql([[
            SELECT max(a) FROM t3928;
        ]])
    end, {
        -- <autoinc-3928.2>
        13
        -- </autoinc-3928.2>
    })

test:do_test(
    "autoinc-3928.3",
    function()
        return test:execsql([[
            DROP TRIGGER t3928r1;
            DROP TRIGGER t3928r2;
            CREATE TRIGGER t3928r3 BEFORE UPDATE ON t3928 FOR EACH ROW
              WHEN new.b=='456' BEGIN
                 INSERT INTO t3928(b) VALUES('before-int-' || CAST(new.b AS TEXT));
            END;
            CREATE TRIGGER t3928r4 AFTER UPDATE ON t3928 FOR EACH ROW
              WHEN new.b=='456' BEGIN
                 INSERT INTO t3928(b) VALUES('after-int-' || CAST(new.b AS TEXT));
            END;
            DELETE FROM t3928 WHERE a!=1;
            UPDATE t3928 SET b='456' WHERE a=1;
            SELECT * FROM t3928 ORDER BY a;
        ]])
    end, {
        -- <autoinc-3928.3>
        1, '456', 14, "before-int-456", 15, "after-int-456"
        -- </autoinc-3928.3>
    })

test:do_test(
    "autoinc-3928.4",
    function()
        return test:execsql([[
            SELECT max(a) FROM t3928;
        ]])
    end, {
        -- <autoinc-3928.4>
        15
        -- </autoinc-3928.4>
    })

test:do_test(
    "autoinc-3928.5",
    function()
        return test:execsql([[
            CREATE TABLE t3928b(x INTEGER PRIMARY KEY);
            INSERT INTO t3928b VALUES(100);
            INSERT INTO t3928b VALUES(200);
            INSERT INTO t3928b VALUES(300);
            DELETE FROM t3928;
            CREATE TABLE t3928c(y INTEGER PRIMARY KEY AUTOINCREMENT, z TEXT);
            CREATE TRIGGER t3928br1 BEFORE DELETE ON t3928b FOR EACH ROW BEGIN
              INSERT INTO t3928(b) VALUES('before-del-'|| CAST(old.x AS TEXT));
              INSERT INTO t3928c(z) VALUES('before-del-'|| CAST(old.x AS TEXT));
            END;
            CREATE TRIGGER t3928br2 AFTER DELETE ON t3928b FOR EACH ROW BEGIN
              INSERT INTO t3928(b) VALUES('after-del-'|| CAST(old.x AS TEXT));
              INSERT INTO t3928c(z) VALUES('after-del-'|| CAST(old.x AS TEXT));
            END;
            DELETE FROM t3928b;
            SELECT * FROM t3928 ORDER BY a;
        ]])
    end, {
        -- <autoinc-3928.5>
        16, "before-del-100", 17, "after-del-100", 18, "before-del-200", 19, "after-del-200", 20, "before-del-300", 21, "after-del-300"
        -- </autoinc-3928.5>
    })

test:do_test(
    "autoinc-3928.6",
    function()
        return test:execsql([[
            SELECT * FROM t3928c ORDER BY y;
        ]])
    end, {
        -- <autoinc-3928.6>
        1, "before-del-100", 2, "after-del-100", 3, "before-del-200", 4, "after-del-200", 5, "before-del-300", 6, "after-del-300"
        -- </autoinc-3928.6>
    })

test:do_test(
    "autoinc-3928.7",
    function()
        return test:execsql([[
            SELECT max(a) FROM t3928 UNION SELECT max(y) FROM t3928c
        ]])
    end, {
        -- <autoinc-3928.7>
        6, 21
        -- </autoinc-3928.7>
    })

-- Ticket [a696379c1f0886615541a48b35bd8181a80e88f8]
test:do_test(
    "autoinc-a69637.1",
    function()
        return test:execsql([[
            CREATE TABLE ta69637_1(x INTEGER PRIMARY KEY AUTOINCREMENT, y INT );
            CREATE TABLE ta69637_2(z INTEGER PRIMARY KEY);
            CREATE TRIGGER ra69637_1 AFTER INSERT ON ta69637_2 FOR EACH ROW BEGIN
              INSERT INTO ta69637_1(y) VALUES(new.z+1);
            END;
            INSERT INTO ta69637_2 VALUES(123);
            SELECT * FROM ta69637_1;
        ]])
    end, {
        -- <autoinc-a69637.1>
        1, 124
        -- </autoinc-a69637.1>
    })

test:do_test(
    "autoinc-a69637.2",
    function()
        return test:execsql([[
            CREATE VIEW va69637_2 AS SELECT * FROM ta69637_2;
            CREATE TRIGGER ra69637_2 INSTEAD OF INSERT ON va69637_2 FOR EACH ROW BEGIN
              INSERT INTO ta69637_1(y) VALUES(new.z+10000);
            END;
            INSERT INTO va69637_2 VALUES(123);
            SELECT * FROM ta69637_1;
        ]])
    end, {
        -- <autoinc-a69637.2>
        1, 124, 2, 10123
        -- </autoinc-a69637.2>
    })

-- gh-3670: Assertion with large number in autoincrement column
test:do_catchsql_test(
    "autoinc-gh-3670",
    [[
        CREATE TABLE t1 (s1 INT PRIMARY KEY AUTOINCREMENT, s2 VARCHAR(10));
        INSERT INTO t1 VALUES (1, 'a');
        INSERT INTO t1 SELECT s2, s2 FROM t1;
    ]], {
        -- <autoinc-gh-3670>
        1, "Type mismatch: can not convert string('a') to integer"
        -- </autoinc-gh-3670>
    })

--
-- gh-4217: make sure that AUTOINCREMENT can be used for any
-- INTEGER field of PRIMARY KEY.
--

test:do_execsql_test(
    "autoinc-11.1",
    [[
        CREATE TABLE t11_1 (i INT AUTOINCREMENT, a INT, PRIMARY KEY(i, a));
        INSERT INTO t11_1 VALUES (NULL, 1), (NULL, 1), (NULL, 1);
        SELECT * FROM t11_1;
    ]], {
        1, 1, 2, 1, 3, 1
    })

test:do_execsql_test(
    "autoinc-11.2",
    [[
        CREATE TABLE t11_2 (i INT AUTOINCREMENT, a INT, PRIMARY KEY(a, i));
        INSERT INTO t11_2 VALUES (NULL, 1), (NULL, 1), (NULL, 1);
        SELECT * FROM t11_2;
    ]], {
        1, 1, 2, 1, 3, 1
    })

test:do_execsql_test(
    "autoinc-11.3",
    [[
        CREATE TABLE t11_3 (i INT, a INT, PRIMARY KEY(i AUTOINCREMENT, a));
        INSERT INTO t11_3 VALUES (NULL, 1), (NULL, 1), (NULL, 1);
        SELECT * FROM t11_3;
    ]], {
        1, 1, 2, 1, 3, 1
    })

test:do_execsql_test(
    "autoinc-11.4",
    [[
        CREATE TABLE t11_4 (i INT, a INT, PRIMARY KEY(a, i AUTOINCREMENT));
        INSERT INTO t11_4 VALUES (NULL, 1), (NULL, 1), (NULL, 1);
        SELECT * FROM t11_4;
    ]], {
        1, 1, 2, 1, 3, 1
    })

test:do_catchsql_test(
    "autoinc-11.5",
    [[
        CREATE TABLE t11_5 (i INT, a INT, PRIMARY KEY(a, i COLLATE "unicode_ci" AUTOINCREMENT));
    ]], {
        1, "Wrong index options (field 2): collation is reasonable only for string and scalar parts"
    })

test:do_catchsql_test(
    "autoinc-11.6",
    [[
        CREATE TABLE t11_6 (i INT, a INT, b INT AUTOINCREMENT, PRIMARY KEY(a, i));
    ]], {
        1, "Can't create or modify index 'pk_unnamed_T11_6_1' in space 'T11_6': sequence field must be a part of the index"
    })

test:do_catchsql_test(
    "autoinc-11.7",
    [[
        CREATE TABLE t11_7 (i INT AUTOINCREMENT, a INT AUTOINCREMENT, PRIMARY KEY(a, i));
    ]], {
        1, "Syntax error at line 1 at or near position 69: table must feature at most one AUTOINCREMENT field"
    })

test:do_catchsql_test(
    "autoinc-11.8",
    [[
        CREATE TABLE t11_8 (i INT, a INT, PRIMARY KEY(a AUTOINCREMENT, i AUTOINCREMENT));
    ]], {
        1, "Syntax error at line 1 at or near position 87: table must feature at most one AUTOINCREMENT field"
    })

test:do_catchsql_test(
    "autoinc-11.9",
    [[
        CREATE TABLE t11_9 (i INT, PRIMARY KEY(a AUTOINCREMENT), a INT);
    ]], {
        1, "Can’t resolve field 'A'"
    })

test:do_catchsql_test(
    "autoinc-11.10",
    [[
        CREATE TABLE t11_8 (i INT, a INT, PRIMARY KEY(a, 1 AUTOINCREMENT));
    ]], {
        1, "Expressions are prohibited in an index definition"
})

test:finish_test()
