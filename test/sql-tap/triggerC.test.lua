#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(43)

--!./tcltestrunner.lua
-- 2009 August 24
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice', here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "triggerC"


---------------------------------------------------------------------------
-- Test organization:
--
-- triggerC-1.*: Haphazardly designed trigger related tests that were useful
--               during an upgrade of the triggers sub-system.
--
-- triggerC-2.*:
--
-- triggerC-3.*:
--
-- triggerC-4.*:
--
-- triggerC-5.*: Test that when recursive triggers are enabled DELETE
--               triggers are fired when rows are deleted as part of OR
--               REPLACE conflict resolution. And that they are not fired
--               if recursive triggers are not enabled.
--
-- Enable recursive triggers for this file.
--
box.space._session_settings:update('sql_recursive_triggers', {{'=', 2, true}})
---------------------------------------------------------------------------
-- This block of tests, triggerC-1.*, are not aimed at any specific
-- property of the triggers sub-system. They were created to debug
-- specific problems while modifying sql to support recursive
-- triggers. They are left here in case they can help debug the
-- same problems again.
--
test:do_execsql_test(
    "triggerC-1.1",
    [[
        CREATE TABLE t1(id INT PRIMARY KEY AUTOINCREMENT, a TEXT UNIQUE, b TEXT, c TEXT);
        CREATE TABLE log(t TEXT PRIMARY KEY, a1 TEXT, b1 TEXT, c1 TEXT, a2 TEXT, b2 TEXT, c2 TEXT);
        CREATE TRIGGER trig1 BEFORE INSERT ON t1 FOR EACH ROW BEGIN
          INSERT INTO log VALUES('before', NULL, NULL, NULL, new.a, new.b, new.c);
        END;
        CREATE TRIGGER trig2 AFTER INSERT ON t1 FOR EACH ROW BEGIN
          INSERT INTO log VALUES('after', NULL, NULL, NULL, new.a, new.b, new.c);
        END;
        CREATE TRIGGER trig3 BEFORE UPDATE ON t1 FOR EACH ROW BEGIN
          INSERT INTO log VALUES('before', old.a,old.b,old.c, new.a,new.b,new.c);
        END;
        CREATE TRIGGER trig4 AFTER UPDATE ON t1 FOR EACH ROW BEGIN
          INSERT INTO log VALUES('after', old.a,old.b,old.c, new.a,new.b,new.c);
        END;

        CREATE TRIGGER trig5 BEFORE DELETE ON t1 FOR EACH ROW BEGIN
          INSERT INTO log VALUES('before', old.a,old.b,old.c, NULL,NULL,NULL);
        END;
        CREATE TRIGGER trig6 AFTER DELETE ON t1 FOR EACH ROW BEGIN
          INSERT INTO log VALUES('after', old.a,old.b,old.c, NULL,NULL,NULL);
        END;
    ]], {
        -- <triggerC-1.1>

        -- </triggerC-1.1>
    })

test:do_execsql_test(
    "triggerC-1.2",
    [[
        INSERT INTO t1 VALUES(1, 'A', 'B', 'C');
        SELECT * FROM log ORDER BY t DESC;
    ]], {
        -- <triggerC-1.2>
        "before", "", "", "", "A", "B", "C", "after", "", "", "", "A", "B", "C"
        -- </triggerC-1.2>
    })

test:do_execsql_test(
    "triggerC-1.3",
    [[
        SELECT * FROM t1
    ]], {
        -- <triggerC-1.3>
        1, "A", "B", "C"
        -- </triggerC-1.3>
    })

test:do_execsql_test(
    "triggerC-1.4",
    [[
        DELETE FROM log;
        UPDATE t1 SET a = 'a';
        SELECT * FROM log ORDER BY t DESC;
    ]], {
        -- <triggerC-1.4>
        "before", "A", "B", "C", "a", "B", "C", "after", "A", "B", "C", "a", "B", "C"
        -- </triggerC-1.4>
    })

test:do_execsql_test(
    "triggerC-1.5",
    [[
        SELECT * FROM t1
    ]], {
        -- <triggerC-1.5>
        1, "a", "B", "C"
        -- </triggerC-1.5>
    })

test:do_execsql_test(
    "triggerC-1.6",
    [[
        DELETE FROM log;
        DELETE FROM t1;
        SELECT * FROM log ORDER BY t DESC;
    ]], {
        -- <triggerC-1.6>
        "before", "a", "B", "C", "", "", "", "after", "a", "B", "C", "", "", ""
        -- </triggerC-1.6>
    })

test:do_execsql_test(
    "triggerC-1.7",
    [[
        SELECT * FROM t1
    ]], {
        -- <triggerC-1.7>

        -- </triggerC-1.7>
    })

-- MUST_WORK_TEST
test:do_execsql_test(
    "triggerC-1.8",
    [[
        CREATE TABLE t4(a INT PRIMARY KEY, b INT);
        CREATE TRIGGER t4t AFTER DELETE ON t4 FOR EACH ROW BEGIN
          SELECT RAISE(ABORT, 'delete is not supported');
        END;
    ]], {
        -- <triggerC-1.8>

        -- </triggerC-1.8>
    })

test:do_test(
    "triggerC-1.9",
    function()
        test:execsql " INSERT INTO t4 VALUES(1, 2) "
        return test:catchsql " DELETE FROM t4 "
    end, {
        -- <triggerC-1.9>
        1, "Failed to execute SQL statement: delete is not supported"
        -- </triggerC-1.9>
    })

test:do_execsql_test(
    "triggerC-1.10",
    [[
        SELECT * FROM t4
    ]], {
        -- <triggerC-1.10>
        1, 2
        -- </triggerC-1.10>
    })

test:do_execsql_test(
    "triggerC-1.11",
    [[
        CREATE TABLE t5 (a INT UNIQUE, b INT PRIMARY KEY, c INT);
        INSERT INTO t5 values (1, 2, 3);
        CREATE TRIGGER au_tbl AFTER UPDATE ON t5 FOR EACH ROW BEGIN
          UPDATE OR IGNORE t5 SET a = new.a, c = 10;
        END;
    ]], {
        -- <triggerC-1.11>

        -- </triggerC-1.11>
    })

test:do_catchsql_test(
    "triggerC-1.12",
    [[
        UPDATE OR REPLACE t5 SET a = 4 WHERE a = 1
    ]], {
        -- <triggerC-1.12>
        1, "Failed to execute SQL statement: too many levels of trigger recursion"
        -- </triggerC-1.12>
    })

test:do_execsql_test(
    "triggerC-1.13",
    [[
        CREATE TABLE t6(a INT UNIQUE, b INT PRIMARY KEY);
        INSERT INTO t6 VALUES(1, 2);
        create trigger r1 after update on t6 for each row begin
          SELECT 1;
        end;
        UPDATE t6 SET a=a;
    ]], {
        -- <triggerC-1.13>

        -- </triggerC-1.13>
    })

test:do_execsql_test(
    "triggerC-1.14",
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE cnt(id INT PRIMARY KEY, n INT UNIQUE);
        INSERT INTO cnt VALUES(0, 0);
        CREATE TABLE t1(a INT UNIQUE, b INT UNIQUE, c INT, d INT, e INT PRIMARY KEY);
        CREATE INDEX t1cd ON t1(c,d);
        CREATE UNIQUE INDEX t1a ON t1(a);
        CREATE TRIGGER t1r1 AFTER UPDATE ON t1 FOR EACH ROW BEGIN UPDATE cnt SET n=n+1; END;
        INSERT INTO t1 VALUES(1,2,3,4,5);
        INSERT INTO t1 VALUES(6,7,8,9,10);
        INSERT INTO t1 VALUES(11,12,13,14,15);
    ]], {
        -- <triggerC-1.14>
        -- </triggerC-1.14>
    })

test:do_catchsql_test(
    "triggerC-1.15",
    [[
        UPDATE OR ROLLBACK t1 SET a=100;
    ]], {
        -- <triggerC-1.15>
        1, "Duplicate key exists in unique index 'unique_unnamed_T1_1' in space 'T1'"
        -- </triggerC-1.15>
    })

---------------------------------------------------------------------------
-- This block of tests, triggerC-2.*, tests that recursive trigger
-- programs (triggers that fire themselves) work. More specifically,
-- this block focuses on recursive INSERT triggers.
--
test:do_execsql_test(
    "triggerC-2.1.0",
    [[
        CREATE TABLE t2(a INT PRIMARY KEY);
    ]], {
        -- <triggerC-2.1.0>

        -- </triggerC-2.1.0>
    })

-- MUST_WORK_TEST
-- for _ in X(0, "X!foreach", [=[["n tdefn rc","\n  1 {\n    CREATE TRIGGER t2_trig AFTER INSERT ON t2 WHEN (new.a>0) BEGIN\n      INSERT INTO t2 VALUES(new.a - 1);\n    END;\n  } {0 {10 9 8 7 6 5 4 3 2 1 0}}\n\n  2 {\n    CREATE TRIGGER t2_trig AFTER INSERT ON t2 BEGIN\n      SELECT CASE WHEN new.a==2 THEN RAISE(IGNORE) ELSE NULL END;\n      INSERT INTO t2 VALUES(new.a - 1);\n    END;\n  } {0 {10 9 8 7 6 5 4 3 2}}\n\n  3 {\n    CREATE TRIGGER t2_trig BEFORE INSERT ON t2 WHEN (new.a>0) BEGIN\n      INSERT INTO t2 VALUES(new.a - 1);\n    END;\n  } {0 {0 1 2 3 4 5 6 7 8 9 10}}\n\n  4 {\n    CREATE TRIGGER t2_trig BEFORE INSERT ON t2 BEGIN\n      SELECT CASE WHEN new.a==2 THEN RAISE(IGNORE) ELSE NULL END;\n      INSERT INTO t2 VALUES(new.a - 1);\n    END;\n  } {0 {3 4 5 6 7 8 9 10}}\n\n  5 {\n    CREATE TRIGGER t2_trig BEFORE INSERT ON t2 BEGIN\n      INSERT INTO t2 VALUES(new.a - 1);\n    END;\n  } {1 {too many levels of trigger recursion}}\n\n  6 {\n    CREATE TRIGGER t2_trig AFTER INSERT ON t2 WHEN (new.a>0) BEGIN\n      INSERT OR IGNORE INTO t2 VALUES(new.a);\n    END;\n  } {0 10}\n\n  7 {\n    CREATE TRIGGER t2_trig BEFORE INSERT ON t2 WHEN (new.a>0) BEGIN\n      INSERT OR IGNORE INTO t2 VALUES(new.a);\n    END;\n  } {1 {too many levels of trigger recursion}}\n"]]=]) do

local
tests =   { {[[ CREATE TRIGGER t2_trig AFTER INSERT ON t2 FOR EACH ROW WHEN (new.a>0) BEGIN
                  INSERT INTO t2 VALUES(new.a - 1);
                END;]], {0, {10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}}},

            {[[ CREATE TRIGGER t2_trig AFTER INSERT ON t2 FOR EACH ROW BEGIN
                  SELECT CASE WHEN new.a==2 THEN RAISE(IGNORE) ELSE NULL END;
                  INSERT INTO t2 VALUES(new.a - 1);
                END;]], {0, {10, 9, 8, 7, 6, 5, 4, 3, 2}}},

            {[[ CREATE TRIGGER t2_trig BEFORE INSERT ON t2 FOR EACH ROW WHEN (new.a>0) BEGIN
                  INSERT INTO t2 VALUES(new.a - 1);
                END;]], {0, {10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}}},

            {[[ CREATE TRIGGER t2_trig BEFORE INSERT ON t2 FOR EACH ROW BEGIN
                  SELECT CASE WHEN new.a==2 THEN RAISE(IGNORE) ELSE NULL END;
                  INSERT INTO t2 VALUES(new.a - 1);
                END;]], {0, {10, 9, 8, 7, 6, 5, 4, 3}}},

            {[[ CREATE TRIGGER t2_trig BEFORE INSERT ON t2 FOR EACH ROW BEGIN
                  INSERT INTO t2 VALUES(new.a - 1);
                END;]], {1, "Failed to execute SQL statement: too many levels of trigger recursion"}},

            {[[ CREATE TRIGGER t2_trig AFTER INSERT ON t2 FOR EACH ROW WHEN (new.a>0) BEGIN
                  INSERT OR IGNORE INTO t2 VALUES(new.a);
                END;]], {0, {10}}},

            {[[  CREATE TRIGGER t2_trig BEFORE INSERT ON t2 FOR EACH ROW WHEN (new.a>0) BEGIN
                   INSERT OR IGNORE INTO t2 VALUES(new.a);
                 END;]], {1, "Failed to execute SQL statement: too many levels of trigger recursion"}}}

for n, v in ipairs(tests) do
    test:do_test(
        "triggerC-2.1."..n,
        function()
            test:catchsql " DROP TRIGGER t2_trig "
            test:execsql " DELETE FROM t2 "
            test:execsql(v[1])
            return test:catchsql [[
                INSERT INTO t2 VALUES(10);
                SELECT * FROM t2 ORDER BY a DESC;
            ]]
        end,
        v[2])
end

-- test:do_execsql_test(
--     "triggerC-2.2",
-- string.format([[
--         CREATE TABLE t22(x PRIMARY KEY);

--         CREATE TRIGGER t22a AFTER INSERT ON t22 FOR EACH ROW BEGIN
--           INSERT INTO t22 SELECT x + (SELECT max(x) FROM t22) FROM t22;
--         END;
--         CREATE TRIGGER t22b BEFORE INSERT ON t22 FOR EACH ROW BEGIN
--           SELECT CASE WHEN (SELECT count(*) FROM t22) >= %s
--                       THEN RAISE(IGNORE)
--                       ELSE NULL END;
--         END;

--         INSERT INTO t22 VALUES(1);
--         SELECT count(*) FROM t22;
--     ]], (sql_MAX_TRIGGER_DEPTH / 2)), {
--         -- <triggerC-2.2>
--         (sql_MAX_TRIGGER_DEPTH / 2)
--         -- </triggerC-2.2>
--     })

-- test:do_execsql_test(
--     "triggerC-2.3",
-- string.format([[
--         CREATE TABLE t23(x PRIMARY KEY);

--         CREATE TRIGGER t23a AFTER INSERT ON t23 FOR EACH ROW BEGIN
--           INSERT INTO t23 VALUES(new.x + 1);
--         END;

--         CREATE TRIGGER t23b BEFORE INSERT ON t23 FOR EACH ROW BEGIN
--           SELECT CASE WHEN new.x>%s
--                       THEN RAISE(IGNORE)
--                       ELSE NULL END;
--         END;

--         INSERT INTO t23 VALUES(1);
--         SELECT count(*) FROM t23;
--     ]], (sql_MAX_TRIGGER_DEPTH / 2)), {
--         -- <triggerC-2.3>
--         (sql_MAX_TRIGGER_DEPTH / 2)
--         -- </triggerC-2.3>
--     })

-------------------------------------------------------------------------
-- This block of tests, triggerC-3.*, test that sql throws an exception
-- when it detects excessive recursion.
--
test:do_execsql_test(
    "triggerC-3.1.1",
    [[
        CREATE TABLE t3(a INT PRIMARY KEY, b INT);
        CREATE TRIGGER t3i AFTER INSERT ON t3 FOR EACH ROW BEGIN
          DELETE FROM t3 WHERE a = new.a;
        END;
        CREATE TRIGGER t3d AFTER DELETE ON t3 FOR EACH ROW BEGIN
          INSERT INTO t3 VALUES(old.a, old.b);
        END;
    ]], {
        -- <triggerC-3.1.1>

        -- </triggerC-3.1.1>
    })

-- MUST_WORK_TEST
test:do_catchsql_test(
    "triggerC-3.1.2",
    [[
        INSERT INTO t3 VALUES(0,0)
    ]], {
        -- <triggerC-3.1.2>
        1, "Failed to execute SQL statement: too many levels of trigger recursion"
        -- </triggerC-3.1.2>
    })

test:do_execsql_test(
    "triggerC-3.1.3",
    [[
        SELECT * FROM t3
    ]], {
        -- <triggerC-3.1.3>
        -- </triggerC-3.1.3>
    })

test:do_execsql_test(
    "triggerC-3.2.1",
    [[
        CREATE TABLE t3b(x INT PRIMARY KEY);
        CREATE TRIGGER t3bi AFTER INSERT ON t3b FOR EACH ROW BEGIN INSERT INTO t3b VALUES(new.x+1); END;
    ]], {
        -- <triggerC-3.2.1>
        -- </triggerC-3.2.1>
    })

test:do_catchsql_test(
    "triggerC-3.2.2",
    [[
        INSERT INTO t3b VALUES(1);
    ]], {
        -- <triggerC-3.1.3>
        1, "Failed to execute SQL statement: too many levels of trigger recursion"
        -- </triggerC-3.1.3>
    })

test:do_execsql_test(
    "triggerC-3.2.3",
    [[
        SELECT * FROM t3b;
    ]], {
        -- <triggerC-3.2.3>
        -- </triggerC-3.2.3>
    })

--------------------------------------------------------------------------
-- This block of tests, triggerC-5.*, test that DELETE triggers are fired
-- if a row is deleted as a result of OR REPLACE conflict resolution.
--
test:do_execsql_test(
    "triggerC-5.1.0",
    [[
        DROP TABLE IF EXISTS t5;
        CREATE TABLE t5(a INTEGER PRIMARY KEY, b TEXT);
        CREATE UNIQUE INDEX t5i ON t5(b);
        INSERT INTO t5 VALUES(1, 'a');
        INSERT INTO t5 VALUES(2, 'b');
        INSERT INTO t5 VALUES(3, 'c');

        CREATE TABLE t5g(a INT PRIMARY KEY, b TEXT, c INT);
        CREATE TRIGGER t5t BEFORE DELETE ON t5 FOR EACH ROW BEGIN
          INSERT INTO t5g VALUES(old.a, old.b, (SELECT count(*) FROM t5));
        END;
    ]], {
        -- <triggerC-5.1.0>

        -- </triggerC-5.1.0>
    })

-- MUST_WORK_TEST
-- foreach {n dml t5g t5} {
--   1 "DELETE FROM t5 WHERE a=2"                        {2 b 3} {1 a 3 c}
--   2 "INSERT OR REPLACE INTO t5 VALUES(2, 'd')"        {2 b 3} {1 a 2 d 3 c}
--   3 "UPDATE OR REPLACE t5 SET a = 2 WHERE a = 3"      {2 b 3} {1 a 2 c}
--   4 "INSERT OR REPLACE INTO t5 VALUES(4, 'b')"        {2 b 3} {1 a 3 c 4 b}
--   5 "UPDATE OR REPLACE t5 SET b = 'b' WHERE b = 'c'"  {2 b 3} {1 a 3 b}
--   6 "INSERT OR REPLACE INTO t5 VALUES(2, 'c')"        {2 b 3 3 c 2} {1 a 2 c}
--   7 "UPDATE OR REPLACE t5 SET a=1, b='b' WHERE a = 3" {1 a 3 2 b 2} {1 b}
-- } {
--   do_test triggerC-5.1.$n {
--     execsql "
--       BEGIN;
--         $dml ;
--         SELECT * FROM t5g ORDER BY rowid;
--         SELECT * FROM t5 ORDER BY rowid;
--       ROLLBACK;
--     "
--   } [concat $t5g $t5]
-- }
test:do_execsql_test(
    "triggerC-5.2.0",
    [[
        DROP TRIGGER t5t;
        CREATE TRIGGER t5t AFTER DELETE ON t5 FOR EACH ROW BEGIN
          INSERT INTO t5g VALUES(old.a, old.b, (SELECT count(*) FROM t5));
        END;
    ]], {
        -- <triggerC-5.2.0>

        -- </triggerC-5.2.0>
    })

-- MUST_WORK_TEST
-- foreach {n dml t5g t5} {
--   1 "DELETE FROM t5 WHERE a=2"                        {2 b 2} {1 a 3 c}
--   2 "INSERT OR REPLACE INTO t5 VALUES(2, 'd')"        {2 b 2} {1 a 2 d 3 c}
--   3 "UPDATE OR REPLACE t5 SET a = 2 WHERE a = 3"      {2 b 2} {1 a 2 c}
--   4 "INSERT OR REPLACE INTO t5 VALUES(4, 'b')"        {2 b 2} {1 a 3 c 4 b}
--   5 "UPDATE OR REPLACE t5 SET b = 'b' WHERE b = 'c'"  {2 b 2} {1 a 3 b}
--   6 "INSERT OR REPLACE INTO t5 VALUES(2, 'c')"        {2 b 2 3 c 1} {1 a 2 c}
--   7 "UPDATE OR REPLACE t5 SET a=1, b='b' WHERE a = 3" {1 a 2 2 b 1} {1 b}
-- } {
--   do_test triggerC-5.2.$n {
--     execsql "
--       BEGIN;
--         $dml ;
--         SELECT * FROM t5g ORDER BY rowid;
--         SELECT * FROM t5 ORDER BY rowid;
--       ROLLBACK;
--     "
--   } [concat $t5g $t5]
-- }

-- MUST_WORK_TEST
-- foreach {n dml t5g t5} {
--   1 "DELETE FROM t5 WHERE a=2"                        {2 b 2} {1 a 3 c}
--   2 "INSERT OR REPLACE INTO t5 VALUES(2, 'd')"        {} {1 a 2 d 3 c}
--   3 "UPDATE OR REPLACE t5 SET a = 2 WHERE a = 3"      {} {1 a 2 c}
--   4 "INSERT OR REPLACE INTO t5 VALUES(4, 'b')"        {} {1 a 3 c 4 b}
--   5 "UPDATE OR REPLACE t5 SET b = 'b' WHERE b = 'c'"  {} {1 a 3 b}
--   6 "INSERT OR REPLACE INTO t5 VALUES(2, 'c')"        {} {1 a 2 c}
--   7 "UPDATE OR REPLACE t5 SET a=1, b='b' WHERE a = 3" {} {1 b}
-- } {
--   do_test triggerC-5.3.$n {
--     execsql "
--       BEGIN;
--         $dml ;
--         SELECT * FROM t5g ORDER BY rowid;
--         SELECT * FROM t5 ORDER BY rowid;
--       ROLLBACK;
--     "
--   } [concat $t5g $t5]
-- }

-- MUST_WORK_TEST
-- #-------------------------------------------------------------------------
-- # Test some of the "undefined behaviour" associated with triggers. The
-- # undefined behaviour occurs when a row being updated or deleted is
-- # manipulated by a BEFORE trigger.
-- #
-- do_test triggerC-7.1 {
--   execsql {
--     CREATE TABLE t8(x);
--     CREATE TABLE t7(a, b);
--     INSERT INTO t7 VALUES(1, 2);
--     INSERT INTO t7 VALUES(3, 4);
--     INSERT INTO t7 VALUES(5, 6);
--     CREATE TRIGGER t7t BEFORE UPDATE ON t7 FOR EACH ROW BEGIN
--       DELETE FROM t7 WHERE a = 1;
--     END;
--     CREATE TRIGGER t7ta AFTER UPDATE ON t7 FOR EACH ROW BEGIN
--       INSERT INTO t8 VALUES('after fired ' || old.rowid || '->' || new.rowid);
--     END;
--   }
-- } {}
-- do_test triggerC-7.2 {
--   execsql {
--     BEGIN;
--       UPDATE t7 SET b=7 WHERE a = 5;
--       SELECT * FROM t7;
--       SELECT * FROM t8;
--     ROLLBACK;
--   }
-- } {3 4 5 7 {after fired 3->3}}
-- do_test triggerC-7.3 {
--   execsql {
--     BEGIN;
--       UPDATE t7 SET b=7 WHERE a = 1;
--       SELECT * FROM t7;
--       SELECT * FROM t8;
--     ROLLBACK;
--   }
-- } {3 4 5 6}
-- do_test triggerC-7.4 {
--   execsql {
--     DROP TRIGGER t7t;
--     CREATE TRIGGER t7t BEFORE UPDATE ON t7 FOR EACH ROW WHEN (old.rowid!=1 OR new.rowid!=8)
--     BEGIN
--       UPDATE t7 set rowid = 8 WHERE rowid=1;
--     END;
--   }
-- } {}
-- do_test triggerC-7.5 {
--   execsql {
--     BEGIN;
--       UPDATE t7 SET b=7 WHERE a = 5;
--       SELECT rowid, * FROM t7;
--       SELECT * FROM t8;
--     ROLLBACK;
--   }
-- } {2 3 4 3 5 7 8 1 2 {after fired 1->8} {after fired 3->3}}
-- do_test triggerC-7.6 {
--   execsql {
--     BEGIN;
--       UPDATE t7 SET b=7 WHERE a = 1;
--       SELECT rowid, * FROM t7;
--       SELECT * FROM t8;
--     ROLLBACK;
--   }
-- } {2 3 4 3 5 6 8 1 2 {after fired 1->8}}
-- do_test triggerC-7.7 {
--   execsql {
--     DROP TRIGGER t7t;
--     DROP TRIGGER t7ta;
--     CREATE TRIGGER t7t BEFORE DELETE ON t7 FOR EACH ROW BEGIN
--       UPDATE t7 set rowid = 8 WHERE rowid=1;
--     END;
--     CREATE TRIGGER t7ta AFTER DELETE ON t7 FOR EACH ROW BEGIN
--       INSERT INTO t8 VALUES('after fired ' || old.rowid);
--     END;
--   }
-- } {}
-- do_test triggerC-7.8 {
--   execsql {
--     BEGIN;
--       DELETE FROM t7 WHERE a = 3;
--       SELECT rowid, * FROM t7;
--       SELECT * FROM t8;
--     ROLLBACK;
--   }
-- } {3 5 6 8 1 2 {after fired 2}}
-- do_test triggerC-7.9 {
--   execsql {
--     BEGIN;
--       DELETE FROM t7 WHERE a = 1;
--       SELECT rowid, * FROM t7;
--       SELECT * FROM t8;
--     ROLLBACK;
--   }
-- } {2 3 4 3 5 6 8 1 2}
-- # Ticket [e25d9ea771febc9c311928c1c01c3163dcb26643]
-- #
-- do_test triggerC-9.1 {
--   execsql {
--     CREATE TABLE t9(a,b);
--     CREATE INDEX t9b ON t9(b);
--     INSERT INTO t9 VALUES(1,0);
--     INSERT INTO t9 VALUES(2,1);
--     INSERT INTO t9 VALUES(3,2);
--     INSERT INTO t9 SELECT a+3, a+2 FROM t9;
--     INSERT INTO t9 SELECT a+6, a+5 FROM t9;
--     SELECT a FROM t9 ORDER BY a;
--   }
-- } {1 2 3 4 5 6 7 8 9 10 11 12}
-- do_test triggerC-9.2 {
--   execsql {
--     CREATE TRIGGER t9r1 AFTER DELETE ON t9 FOR EACH ROW BEGIN
--       DELETE FROM t9 WHERE b=old.a;
--     END;
--     DELETE FROM t9 WHERE b=4;
--     SELECT a FROM t9 ORDER BY a;
--   }
-- } {1 2 3 4}
-- At one point (between versions 3.6.18 and 3.6.20 inclusive), an UPDATE
-- that fired a BEFORE trigger that itself updated the same row as the
-- statement causing it to fire was causing a strange side-effect: The
-- values updated by the statement within the trigger were being overwritten
-- by the values in the new.* array, even if those values were not
-- themselves written by the parent UPDATE statement.
--
-- Technically speaking this was not a bug. The sql documentation says
-- that if a BEFORE UPDATE or BEFORE DELETE trigger modifies or deletes the
-- row that the parent statement is operating on the results are undefined.
-- But as of 3.6.21 behaviour is restored to the way it was in versions
-- 3.6.17 and earlier to avoid causing unnecessary difficulties.
--
test:do_test(
    "triggerC-10.1",
    function()
        test:execsql [[
            CREATE TABLE t10(id INT PRIMARY KEY, a TEXT, updatecnt INT DEFAULT 0);
            CREATE UNIQUE INDEX t10i1 ON t10(a);
            CREATE TRIGGER t10_bu BEFORE UPDATE OF a ON t10 FOR EACH ROW BEGIN
              UPDATE t10 SET updatecnt = updatecnt+1 WHERE a = old.a;
            END;
            INSERT INTO t10 VALUES(0, 'hello', 0);
        ]]
        -- Before the problem was fixed, table t10 would contain the tuple
        -- (world, 0) after running the following script (because the value
        -- 1 written to column "updatecnt" was clobbered by the old value 0).
        --
        return test:execsql [[
            UPDATE t10 SET a = 'world';
            SELECT * FROM t10;
        ]]
    end, {
        -- <triggerC-10.1>
        0, "world", 1
        -- </triggerC-10.1>
    })

test:do_execsql_test(
    "triggerC-10.2",
    [[
        UPDATE t10 SET a = 'tcl', updatecnt = 5;
        SELECT * FROM t10;
    ]], {
        -- <triggerC-10.2>
        0, "tcl", 5
        -- </triggerC-10.2>
    })

test:do_test(
    "triggerC-10.3",
    function()
        test:execsql [[
            CREATE TABLE t11(
              c0 INT PRIMARY KEY, c1 INT UNIQUE,   c2 INT,  c3 INT,  c4 INT,  c5 INT,  c6 INT,  c7 INT,
              c8 INT,  c9 INT, c10 INT, c11 INT, c12 INT, c13 INT, c14 INT, c15 INT, c16 INT,
              c17 INT, c18 INT, c19 INT, c20 INT, c21 INT, c22 INT, c23 INT, c24 INT, c25 INT,
              c26 INT, c27 INT, c28 INT, c29 INT, c30 INT, c31 INT, c32 INT, c33 INT, c34 INT,
              c35 INT, c36 INT, c37 INT, c38 INT, c39 INT, c40 INT
            );

            CREATE TRIGGER t11_bu BEFORE UPDATE OF c1 ON t11 FOR EACH ROW BEGIN
              UPDATE t11 SET c31 = c31+1, c32=c32+1 WHERE c2 = old.c2;
            END;

            INSERT INTO t11 VALUES(
              0, 1,   2,  3,  4,  5,  6,  7,  8,  9, 10,
              11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
              21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
              31, 32, 33, 34, 35, 36, 37, 38, 39, 40
            );
        ]]
        -- Before the problem was fixed, table t10 would contain the tuple
        -- (world, 0) after running the following script (because the value
        -- 1 written to column "updatecnt" was clobbered by the old value 0).
        --
        return test:execsql [[
            UPDATE t11 SET c4=35, c33=22, c1=5;
            SELECT * FROM t11;
        ]]
    end, {
        -- <triggerC-10.3>
        0, 5, 2, 3, 35, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 32, 33, 22, 34, 35, 36, 37, 38, 39, 40
        -- </triggerC-10.3>
    })

-- #-------------------------------------------------------------------------
-- # Test that bug [371bab5d65] has been fixed. BEFORE INSERT and INSTEAD OF
-- # INSERT triggers with the DEFAULT VALUES INSERT syntax.
-- #
test:do_test(
    "triggerC-11.0",
    function()
        test:catchsql " DROP TABLE IF EXISTS log "
        return test:execsql " CREATE TABLE log(id INTEGER PRIMARY KEY, a INT, b TEXT) "
    end, {
        -- <triggerC-11.0>

        -- </triggerC-11.0>
    })

-- MUST_WORK_TEST
local
tests11 = {-- {"CREATE TABLE t1(a PRIMARY KEY, b)",                         {{}, {}}},
           {"CREATE TABLE t1(a INT PRIMARY KEY DEFAULT 1, b TEXT DEFAULT 'abc')", {1, "abc"}}}

--for _ in X(0, "X!foreach", [=[["testno tbl defaults","\n  1 \"CREATE TABLE t1(a PRIMARY KEY, b)\"                          {{} {}}\n  2 \"CREATE TABLE t1(a PRIMARY KEY DEFAULT 1, b DEFAULT 'abc')\"  {1 abc}\n  3 \"CREATE TABLE t1(a PRIMARY KEY, b DEFAULT 4.5)\"              {{} 4.5}\n"]]=]) do
for testno, v in ipairs(tests11) do
    test:do_test(
        "triggerC-11."..testno..".1",
        function()
            test:catchsql " DROP TABLE t1 "
            test:execsql " DELETE FROM log "
            test:execsql(v[1])
            return test:execsql [[
                CREATE TRIGGER tt1 BEFORE INSERT ON t1 FOR EACH ROW BEGIN
                  INSERT INTO log VALUES((SELECT coalesce(max(id),0) + 1 FROM log),
                                         new.a, new.b);
                END;
                INSERT INTO t1 DEFAULT VALUES;
                SELECT a,b FROM log;
            ]]
        end, v[2])

    -- Tarantool: we're unable to do double insert of default vals
    -- (PK will be not unique). Comment so far
    -- test:do_test(
    --     "triggerC-11."..testno..".2",
    --     function()
    --         test:execsql " DELETE FROM log "
    --         return test:execsql [[
    --             CREATE TRIGGER tt2 AFTER INSERT ON t1 FOR EACH ROW BEGIN
    --               INSERT INTO log VALUES(new.a, new.b);
    --             END;
    --             INSERT INTO t1 DEFAULT VALUES;
    --             SELECT * FROM log;
    --         ]]
    --     end, {
    --         -- X(891, "X!cmd", [=[["concat",["defaults"],["defaults"]]]=])
    --     })

    test:do_test(
        "triggerC-11."..testno..".3",
        function()
            test:execsql " DROP TRIGGER tt1 "
            test:execsql " DELETE FROM t1"
            test:execsql " DELETE FROM log "
            return test:execsql [[
                INSERT INTO t1 DEFAULT VALUES;
                SELECT a,b FROM log;
            ]]
        end, {
            defaults
        })

    --
end
test:do_test(
    "triggerC-11.4",
    function()
        test:catchsql " DROP TABLE t2 "
        return test:execsql [[
            DELETE FROM log;
            CREATE TABLE t2(a INT PRIMARY KEY, b INT);
            CREATE VIEW v2 AS SELECT * FROM t2;
            CREATE TRIGGER tv2 INSTEAD OF INSERT ON v2 FOR EACH ROW BEGIN
              INSERT INTO log VALUES((SELECT coalesce(max(id),0) + 1 FROM log),
                                     new.a, new.b);
            END;
            INSERT INTO v2 DEFAULT VALUES;
            SELECT a, b, a IS NULL, b IS NULL FROM log;
        ]]
    end, {
        -- <triggerC-11.4>
        "", "", true, true
        -- </triggerC-11.4>
    })

-- do_test triggerC-12.1 {
--   db close
--   forcedelete test.db
--   sql db test.db
test:execsql(
    [[
    DROP TABLE t1;
    CREATE TABLE t1(id INTEGER PRIMARY KEY, a INT, b INT);
    INSERT INTO t1 VALUES(1, 1, 2);
    INSERT INTO t1 VALUES(2, 3, 4);
    INSERT INTO t1 VALUES(3, 5, 6);
    CREATE TRIGGER tr1 AFTER INSERT ON t1 FOR EACH ROW BEGIN SELECT 1 ; END ;]])

test:do_execsql_test(
    "triggerC-13.1",
    [[
        UPDATE "_session_settings" SET "value" = true WHERE "name" = 'sql_recursive_triggers';
        CREATE TABLE t12(id INTEGER PRIMARY KEY, a INT, b INT);
        INSERT INTO t12 VALUES(1, 1, 2);
        CREATE TRIGGER tr12 AFTER UPDATE ON t12 FOR EACH ROW BEGIN
          UPDATE t12 SET a=new.a+1, b=new.b+1;
        END;
    ]], {
        -- <triggerC-13.1>

        -- </triggerC-13.1>
    })

test:do_catchsql_test(
    "triggerC-13.2",
    [[
        UPDATE t12 SET a=a+1, b=b+1;
    ]], {
        -- <triggerC-13.2>
        1, "Failed to execute SQL statement: too many levels of trigger recursion"
        -- </triggerC-13.2>
    })

---------------------------------------------------------------------------
-- The following tests seek to verify that constant values (i.e. literals)
-- are not factored out of loops within trigger programs. sql does
-- not factor constants out of loops within trigger programs as it may only
-- do so in code generated before the first table or index is opened. And
-- by the time a trigger program is coded, at least one table or index has
-- always been opened.
--
-- At one point, due to a bug allowing constant factoring within triggers,
-- the following SQL would produce the wrong result.
--
local SQL = [[
  DROP TABLE IF EXISTS t1;
  DROP VIEW v2;
  DROP TABLE IF EXISTS t2;
  DROP TABLE IF EXISTS t4;
  DROP TABLE IF EXISTS t5;
  CREATE TABLE t1(a INT PRIMARY KEY, b INT, c INT);
  CREATE INDEX i1 ON t1(a, c);
  CREATE INDEX i2 ON t1(b, c);
  INSERT INTO t1 VALUES(1, 2, 3);

  CREATE TABLE t2(e INT PRIMARY KEY, f INT);
  CREATE INDEX i3 ON t2(e);
  INSERT INTO t2 VALUES(1234567, 3);

  CREATE TABLE empty(x INT PRIMARY KEY);
  CREATE TABLE not_empty(x INT PRIMARY KEY);
  INSERT INTO not_empty VALUES(2);

  CREATE TABLE t4(x INT PRIMARY KEY);
  CREATE TABLE t5(g INT PRIMARY KEY, h INT, i INT);

  CREATE TRIGGER trig BEFORE INSERT ON t4 FOR EACH ROW BEGIN
    INSERT INTO t5 SELECT * FROM t1 WHERE
        (a IN (SELECT x FROM empty) OR b IN (SELECT x FROM not_empty))
        AND c IN (SELECT f FROM t2 WHERE e=1234567);
  END;

  INSERT INTO t4 VALUES(0);
  SELECT * FROM t5;
]]
-- reset_db
test:do_execsql_test(
    "triggerC-14.1",
    SQL, {
        -- <triggerC-14.1>
        1, 2, 3
        -- </triggerC-14.1>
    })

-- reset_db
-- optimization_control db factor-constants 0
-- do_execsql_test triggerC-14.2 $SQL {1 2 3}
-- MUST_WORK_TEST
---------------------------------------------------------------------------
-- Check that table names used by trigger programs are dequoted exactly
-- once.
--
test:do_execsql_test(
    "triggerC-15.1.1",
    [[
        UPDATE "_session_settings" SET "value" = true WHERE "name" = 'sql_recursive_triggers';
        CREATE TABLE node(
            id int not null primary key,
            pid int not null default 0,
            key TEXT not null,
            path TEXT default '',
            unique(pid, key)
            );
        CREATE TRIGGER node_delete_referencing AFTER DELETE ON node
          FOR EACH ROW
          BEGIN
          DELETE FROM node WHERE pid = old.id;
        END;
    ]])

test:do_execsql_test(
    "triggerC-15.1.2",
    [[
        INSERT INTO node(id, pid, key) VALUES(9, 0, 'test');
        INSERT INTO node(id, pid, key) VALUES(90, 9, 'test1');
        INSERT INTO node(id, pid, key) VALUES(900, 90, 'test2');
        DELETE FROM node WHERE id=9;
        SELECT * FROM node;
    ]])

-- Tarantool: such indentifiers are not working
-- Comment so far
-- test:do_execsql_test(
--     "15.2.1",
--     [[
--         CREATE TABLE   x1  (x PRIMARY KEY);

--         CREATE TABLE '"x2"'(a PRIMARY KEY, b);

--         INSERT INTO x2 VALUES(1, 2);
--         INSERT INTO x2 VALUES(3, 4);
--         INSERT INTO '"x2"' SELECT * FROM x2;

--         CREATE TRIGGER x1ai AFTER INSERT ON x1 FOR EACH ROW BEGIN
--           INSERT INTO """x2""" VALUES('x', 'y');
--           DELETE FROM """x2""" WHERE a=1;
--           UPDATE """x2""" SET b = 11 WHERE a = 3;
--         END;

--         INSERT INTO x1 VALUES('go!');
--     ]])

-- test:do_execsql_test(
--     "15.2.2",
--     [[
--         SELECT * FROM x2;
--     ]], {
--         -- <15.2.2>
--         1, 2, 3, 4
--         -- </15.2.2>
--     })

-- test:do_execsql_test(
--     "15.2.3",
--     [[
--         SELECT * FROM """x2""";
--     ]], {
--         -- <15.2.3>
--         3, 11, "x", "y"
--         -- </15.2.3>
--     })

test:finish_test()
