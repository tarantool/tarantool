#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(53)

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
-- This file implements regression tests for SQLite library.  The
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
-- SQLITE_SEQUENCE table gets created.
--
test:do_execsql_test(
    "autoinc-1.2",
    [[
        CREATE TABLE t1(x INTEGER PRIMARY KEY AUTOINCREMENT, y);
    ]], {
        -- <autoinc-1.2>

        -- </autoinc-1.2>
    })

-- The SQLITE_SEQUENCE table is initially empty
--
test:do_execsql_test(
    "autoinc-1.3",
    [[
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-1.3>

        -- </autoinc-1.3>
    })

-- Close and reopen the database.  Verify that everything is still there.
--
-- test:do_test(
--     "autoinc-1.4",
--     function()
--         db("close")
--         sqlite3("db", "test.db")
--         return test:execsql([[
--             SELECT * FROM sql_sequence;
--         ]])
--     end, {
--         -- <autoinc-1.4>

--         -- </autoinc-1.4>
--     })

-- We are not allowed to drop the sqlite_sequence table.
--
test:do_catchsql_test(
    "autoinc-1.5",
    [[
        DROP TABLE sql_sequence
    ]], {
        -- <autoinc-1.5>
        1, "table sql_sequence may not be dropped"
        -- </autoinc-1.5>
    })

test:do_execsql_test(
    "autoinc-1.6",
    [[
        SELECT name FROM _space WHERE name NOT IN (SELECT name FROM _space WHERE name LIKE '\_%' ESCAPE '\')
    ]], {
        -- <autoinc-1.6>
        "t1", "sql_sequence"
        -- </autoinc-1.6>
    })

-- Insert an entries into the t1 table and make sure the largest key
-- is always recorded in the sqlite_sequence table.
--
test:do_execsql_test(
    "autoinc-2.1",
    [[
        --SELECT * FROM sql_sequence
    ]], {
        -- <autoinc-2.1>

        -- </autoinc-2.1>
    })

test:do_execsql_test(
    "autoinc-2.2",
    [[
        INSERT INTO t1 VALUES(12,34);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.2>
        "t1", 12
        -- </autoinc-2.2>
    })

test:do_execsql_test(
    "autoinc-2.3",
    [[
        INSERT INTO t1 VALUES(1,23);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.3>
        "t1", 12
        -- </autoinc-2.3>
    })

test:do_execsql_test(
    "autoinc-2.4",
    [[
        INSERT INTO t1 VALUES(123,456);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.4>
        "t1", 123
        -- </autoinc-2.4>
    })

test:do_execsql_test(
    "autoinc-2.5",
    [[
        INSERT INTO t1 VALUES(NULL,567);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.5>
        "t1", 124
        -- </autoinc-2.5>
    })

test:do_execsql_test(
    "autoinc-2.6",
    [[
        DELETE FROM t1 WHERE y=567;
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.6>
        "t1", 124
        -- </autoinc-2.6>
    })

test:do_execsql_test(
    "autoinc-2.7",
    [[
        INSERT INTO t1 VALUES(NULL,567);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.7>
        "t1", 125
        -- </autoinc-2.7>
    })

test:do_execsql_test(
    "autoinc-2.8",
    [[
        DELETE FROM t1;
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.8>
        "t1", 125
        -- </autoinc-2.8>
    })

test:do_execsql_test(
    "autoinc-2.9",
    [[
        INSERT INTO t1 VALUES(12,34);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.9>
        "t1", 125
        -- </autoinc-2.9>
    })

test:do_execsql_test(
    "autoinc-2.10",
    [[
        INSERT INTO t1 VALUES(125,456);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.10>
        "t1", 125
        -- </autoinc-2.10>
    })

test:do_execsql_test(
    "autoinc-2.11",
    [[
        INSERT INTO t1 VALUES(-1234567,-1);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.11>
        "t1", 125
        -- </autoinc-2.11>
    })

test:do_execsql_test(
    "autoinc-2.12",
    [[
        INSERT INTO t1 VALUES(234,5678);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.12>
        "t1", 234
        -- </autoinc-2.12>
    })

test:do_execsql_test(
    "autoinc-2.13",
    [[
        DELETE FROM t1;
        INSERT INTO t1 VALUES(NULL,1);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.13>
        "t1", 235
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

-- # Manually change the autoincrement values in sqlite_sequence.
-- #
test:do_execsql_test(
    "autoinc-2.20",
    [[
        UPDATE sql_sequence SET seq=1234 WHERE name='t1';
        INSERT INTO t1 VALUES(NULL,2);
        SELECT * FROM t1;
    ]], {
        -- <autoinc-2.20>
        235, 1, 1235, 2
        -- </autoinc-2.20>
    })

test:do_execsql_test(
    "autoinc-2.21",
    [[
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.21>
        "t1", 1235
        -- </autoinc-2.21>
    })

test:do_execsql_test(
    "autoinc-2.22",
    [[
        UPDATE sql_sequence SET seq=NULL WHERE name='t1';
        INSERT INTO t1 VALUES(NULL,3);
        SELECT * FROM t1;
    ]], {
        -- <autoinc-2.22>
        235, 1, 1235, 2, 1236, 3
        -- </autoinc-2.22>
    })

test:do_execsql_test(
    "autoinc-2.23",
    [[
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.23>
        "t1", 1236
        -- </autoinc-2.23>
    })

test:do_execsql_test(
    "autoinc-2.24",
    [[
        UPDATE sql_sequence SET seq='a-string' WHERE name='t1';
        INSERT INTO t1 VALUES(NULL,4);
        SELECT * FROM t1;
    ]], {
        -- <autoinc-2.24>
        235, 1, 1235, 2, 1236, 3, 1237, 4
        -- </autoinc-2.24>
    })

test:do_execsql_test(
    "autoinc-2.25",
    [[
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.25>
        "t1", 1237
        -- </autoinc-2.25>
    })

test:do_execsql_test(
    "autoinc-2.26",
    [[
        DELETE FROM sql_sequence WHERE name='t1';
        INSERT INTO t1 VALUES(NULL,5);
        SELECT * FROM t1;
    ]], {
        -- <autoinc-2.26>
        235, 1, 1235, 2, 1236, 3, 1237, 4, 1238, 5
        -- </autoinc-2.26>
    })

test:do_execsql_test(
    "autoinc-2.27",
    [[
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.27>
        "t1", 1238
        -- </autoinc-2.27>
    })

test:do_execsql_test(
    "autoinc-2.28",
    [[
        UPDATE sql_sequence SET seq='-12345678901234567890'
          WHERE name='t1';
        INSERT INTO t1 VALUES(NULL,6);
        SELECT * FROM t1;
    ]], {
        -- <autoinc-2.28>
        235, 1, 1235, 2, 1236, 3, 1237, 4, 1238, 5, 1239, 6
        -- </autoinc-2.28>
    })

test:do_execsql_test(
    "autoinc-2.29",
    [[
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.29>
        "t1", 1239
        -- </autoinc-2.29>
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
        235, 1, 1235, 2, 1240, 3, 1241, 4
        -- </autoinc-2.50>
    })

test:do_execsql_test(
    "autoinc-2.51",
    [[
        SELECT * FROM sql_sequence
    ]], {
        -- <autoinc-2.51>
        "t1", 1241
        -- </autoinc-2.51>
    })

-- ifcapable tempdb {
--   do_test autoinc-2.52 {
--     execsql {
--       CREATE TEMP TABLE t2 AS SELECT y FROM t1;
--     }
--     execsql {
--       INSERT INTO t1 SELECT NULL, y+4 FROM t2;
--       SELECT * FROM t1;
--     }
--   } {235 1 1235 2 1240 3 1241 4 1242 5 1243 6 1244 7 1245 8}
--   do_test autoinc-2.53 {
--     execsql {
--       SELECT * FROM sqlite_sequence
--     }
--   } {t1 1245}
--   do_test autoinc-2.54 {
--     execsql {
--       DELETE FROM t1;
--       INSERT INTO t1 SELECT NULL, y FROM t2;
--       SELECT * FROM t1;
--     }
--   } {1246 1 1247 2 1248 3 1249 4}
--   do_test autoinc-2.55 {
--     execsql {
--       SELECT * FROM sqlite_sequence
--     }
--   } {t1 1249}
-- }
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
            CREATE TABLE t2(d, e INTEGER PRIMARY KEY AUTOINCREMENT, f);
            INSERT INTO t2(d) VALUES(1);
            SELECT * FROM sql_sequence;
        ]])
    end, {
        -- <autoinc-2.70>
        "t1", 1241, "t2", 1
        -- </autoinc-2.70>
    })

test:do_execsql_test(
    "autoinc-2.71",
    [[
        INSERT INTO t2(d) VALUES(2);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.71>
        "t1", 1241, "t2", 2
        -- </autoinc-2.71>
    })

test:do_execsql_test(
    "autoinc-2.72",
    [[
        INSERT INTO t1(x) VALUES(10000);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.72>
        "t1", 10000, "t2", 2
        -- </autoinc-2.72>
    })

test:do_execsql_test(
    "autoinc-2.73",
    [[
        CREATE TABLE t3(g INTEGER PRIMARY KEY AUTOINCREMENT, h);
        INSERT INTO t3(h) VALUES(1);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.73>
        "t1", 10000, "t2", 2, "t3", 1
        -- </autoinc-2.73>
    })

test:do_execsql_test(
    "autoinc-2.74",
    [[
        INSERT INTO t2(d,e) VALUES(3,100);
        SELECT * FROM sql_sequence;
    ]], {
        -- <autoinc-2.74>
        "t1", 10000, "t2", 100, "t3", 1
        -- </autoinc-2.74>
    })

-- When a table with an AUTOINCREMENT is deleted, the corresponding entry
-- in the SQLITE_SEQUENCE table should also be deleted.  But the SQLITE_SEQUENCE
-- table itself should remain behind.
--
test:do_execsql_test(
    "autoinc-3.1",
    [[
        SELECT name FROM sql_sequence
    ]], {
        -- <autoinc-3.1>
        "t1", "t2", "t3"
        -- </autoinc-3.1>
    })

test:do_execsql_test(
    "autoinc-3.2",
    [[
        DROP TABLE t1;
        SELECT name FROM sql_sequence;
    ]], {
        -- <autoinc-3.2>
        "t2", "t3"
        -- </autoinc-3.2>
    })

test:do_execsql_test(
    "autoinc-3.3",
    [[
        DROP TABLE t3;
        SELECT name FROM sql_sequence;
    ]], {
        -- <autoinc-3.3>
        "t2"
        -- </autoinc-3.3>
    })

test:do_execsql_test(
    "autoinc-3.4",
    [[
        DROP TABLE t2;
        SELECT name FROM sql_sequence;
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
--         SELECT 1, name FROM sqlite_master WHERE type='table';
--         SELECT 2, name FROM sqlite_temp_master WHERE type='table';
--     ]], {
--         -- <autoinc-4.1>
--         1, "sqlite_sequence"
--         -- </autoinc-4.1>
--     })

-- test:do_execsql_test(
--     "autoinc-4.2",
--     [[
--         CREATE TABLE t1(x INTEGER PRIMARY KEY AUTOINCREMENT, y);
--         CREATE TEMP TABLE t3(a INTEGER PRIMARY KEY AUTOINCREMENT, b);
--         SELECT 1, name FROM sqlite_master WHERE type='table';
--         SELECT 2, name FROM sqlite_temp_master WHERE type='table';
--     ]], {
--         -- <autoinc-4.2>
--         1, "sqlite_sequence", 1, "t1", 2, "t3", 2, "sqlite_sequence"
--         -- </autoinc-4.2>
--     })

-- test:do_execsql_test(
--     "autoinc-4.3",
--     [[
--         SELECT 1, * FROM main.sqlite_sequence;
--         SELECT 2, * FROM temp.sqlite_sequence;
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
--         SELECT 1, * FROM main.sqlite_sequence;
--         SELECT 2, * FROM temp.sqlite_sequence;
--     ]], {
--         -- <autoinc-4.5>
--         1, "t1", 11, 2, "t3", 21
--         -- </autoinc-4.5>
--     })

-- test:do_execsql_test(
--     "autoinc-4.6",
--     [[
--         INSERT INTO t1 SELECT * FROM t3;
--         SELECT 1, * FROM main.sqlite_sequence;
--         SELECT 2, * FROM temp.sqlite_sequence;
--     ]], {
--         -- <autoinc-4.6>
--         1, "t1", 21, 2, "t3", 21
--         -- </autoinc-4.6>
--     })

-- test:do_execsql_test(
--     "autoinc-4.7",
--     [[
--         INSERT INTO t3 SELECT x+100, y  FROM t1;
--         SELECT 1, * FROM main.sqlite_sequence;
--         SELECT 2, * FROM temp.sqlite_sequence;
--     ]], {
--         -- <autoinc-4.7>
--         1, "t1", 21, 2, "t3", 121
--         -- </autoinc-4.7>
--     })

-- test:do_execsql_test(
--     "autoinc-4.8",
--     [[
--         DROP TABLE t3;
--         SELECT 1, * FROM main.sqlite_sequence;
--         SELECT 2, * FROM temp.sqlite_sequence;
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
--         SELECT 1, * FROM main.sqlite_sequence;
--         SELECT 2, * FROM temp.sqlite_sequence;
--     ]], {
--         -- <autoinc-4.9>
--         2, "t2", 21
--         -- </autoinc-4.9>
--     })

-- test:do_execsql_test(
--     "autoinc-4.10",
--     [[
--         DROP TABLE t2;
--         SELECT 1, * FROM main.sqlite_sequence;
--         SELECT 2, * FROM temp.sqlite_sequence;
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
        CREATE TABLE t6(v INTEGER PRIMARY KEY AUTOINCREMENT, w);
        INSERT INTO t6 VALUES(9223372036854775808,1);
        INSERT INTO t6 VALUES(NULL,1);
        -- SELECT seq FROM sql_sequence WHERE name='t6';
    ]], {
        -- <autoinc-6.1>
        -- 9223372036854775807
        -- </autoinc-6.1>
    })

test:do_catchsql_test(
    "autoinc-6.2",
    [[
        INSERT INTO t6 VALUES(NULL,1);
    ]], {
        -- <autoinc-6.2>
        1, "UNIQUE constraint failed: t6.v"
        -- </autoinc-6.2>
    })

-- Allow the AUTOINCREMENT keyword inside the parentheses
-- on a separate PRIMARY KEY designation.
--
test:do_execsql_test(
    "autoinc-7.1",
    [[
        CREATE TABLE t7(x INTEGER, y REAL, PRIMARY KEY(x AUTOINCREMENT));
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
        1, "AUTOINCREMENT is only allowed on an INTEGER PRIMARY KEY"
        -- </autoinc-7.2>
    })

-- Ticket #3148
-- Make sure the sqlite_sequence table is not damaged when doing
-- an empty insert - an INSERT INTO ... SELECT ... where the SELECT
-- clause returns an empty set.
--
test:do_test(
    "autoinc-9.1",
    function()
        return test:execsql([[
            CREATE TABLE t2(x INTEGER PRIMARY KEY AUTOINCREMENT, y);
            INSERT INTO t2 VALUES(NULL, 1);
            CREATE TABLE t3(a INTEGER PRIMARY KEY AUTOINCREMENT, b);
            INSERT INTO t3 SELECT * FROM t2 WHERE y>1;

            SELECT * FROM sql_sequence WHERE name='t3';
        ]])
    end, {
        -- <autoinc-9.1>
        "t3", 0
        -- </autoinc-9.1>
    })

test:catchsql(" pragma recursive_triggers = off ")
-- Ticket #3928.  Make sure that triggers to not make extra slots in
-- the SQLITE_SEQUENCE table.
--
test:do_test(
    "autoinc-3928.1",
    function()
        return test:execsql([[
            CREATE TABLE t3928(a INTEGER PRIMARY KEY AUTOINCREMENT, b);
            CREATE TRIGGER t3928r1 BEFORE INSERT ON t3928 BEGIN
              INSERT INTO t3928(b) VALUES('before1');
              INSERT INTO t3928(b) VALUES('before2');
            END;
            CREATE TRIGGER t3928r2 AFTER INSERT ON t3928 BEGIN
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
            SELECT * FROM sql_sequence WHERE name='t3928'
        ]])
    end, {
        -- <autoinc-3928.2>
        "t3928", 13
        -- </autoinc-3928.2>
    })

test:do_test(
    "autoinc-3928.3",
    function()
        return test:execsql([[
            DROP TRIGGER t3928r1;
            DROP TRIGGER t3928r2;
            CREATE TRIGGER t3928r3 BEFORE UPDATE ON t3928
              WHEN typeof(new.b)=='integer' BEGIN
                 INSERT INTO t3928(b) VALUES('before-int-' || new.b);
            END;
            CREATE TRIGGER t3928r4 AFTER UPDATE ON t3928
              WHEN typeof(new.b)=='integer' BEGIN
                 INSERT INTO t3928(b) VALUES('after-int-' || new.b);
            END;
            DELETE FROM t3928 WHERE a!=1;
            UPDATE t3928 SET b=456 WHERE a=1;
            SELECT * FROM t3928 ORDER BY a;
        ]])
    end, {
        -- <autoinc-3928.3>
        1, 456, 14, "before-int-456", 15, "after-int-456"
        -- </autoinc-3928.3>
    })

test:do_test(
    "autoinc-3928.4",
    function()
        return test:execsql([[
            SELECT * FROM sql_sequence WHERE name='t3928'
        ]])
    end, {
        -- <autoinc-3928.4>
        "t3928", 15
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
            CREATE TABLE t3928c(y INTEGER PRIMARY KEY AUTOINCREMENT, z);
            CREATE TRIGGER t3928br1 BEFORE DELETE ON t3928b BEGIN
              INSERT INTO t3928(b) VALUES('before-del-'||old.x);
              INSERT INTO t3928c(z) VALUES('before-del-'||old.x);
            END;
            CREATE TRIGGER t3928br2 AFTER DELETE ON t3928b BEGIN
              INSERT INTO t3928(b) VALUES('after-del-'||old.x);
              INSERT INTO t3928c(z) VALUES('after-del-'||old.x);
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
            SELECT * FROM sql_sequence WHERE name LIKE 't3928%' ORDER BY name;
        ]])
    end, {
        -- <autoinc-3928.7>
        "t3928", 21, "t3928c", 6
        -- </autoinc-3928.7>
    })

-- Ticket [a696379c1f0886615541a48b35bd8181a80e88f8]
test:do_test(
    "autoinc-a69637.1",
    function()
        return test:execsql([[
            CREATE TABLE ta69637_1(x INTEGER PRIMARY KEY AUTOINCREMENT, y);
            CREATE TABLE ta69637_2(z INTEGER PRIMARY KEY);
            CREATE TRIGGER ra69637_1 AFTER INSERT ON ta69637_2 BEGIN
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
            CREATE TRIGGER ra69637_2 INSTEAD OF INSERT ON va69637_2 BEGIN
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

test:finish_test()
