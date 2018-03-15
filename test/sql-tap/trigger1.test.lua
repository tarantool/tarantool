#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(33)

--!./tcltestrunner.lua
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file tests creating and dropping triggers, and interaction thereof
-- with the database COMMIT/ROLLBACK logic.
--
-- 1. CREATE and DROP TRIGGER tests
-- trigger1-1.1: Error if table does not exist
-- trigger1-1.2: Error if trigger already exists
-- trigger1-1.3: Created triggers are deleted if the transaction is rolled back
-- trigger1-1.4: DROP TRIGGER removes trigger
-- trigger1-1.5: Dropped triggers are restored if the transaction is rolled back
-- trigger1-1.6: Error if dropped trigger doesn't exist
-- trigger1-1.7: Dropping the table automatically drops all triggers
-- trigger1-1.8: A trigger created on a TEMP table is not inserted into sqlite_master
-- trigger1-1.9: Ensure that we cannot create a trigger on sqlite_master
-- trigger1-1.10:
-- trigger1-1.11:
-- trigger1-1.12: Ensure that INSTEAD OF triggers cannot be created on tables
-- trigger1-1.13: Ensure that AFTER triggers cannot be created on views
-- trigger1-1.14: Ensure that BEFORE triggers cannot be created on views
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_catchsql_test(
    "trigger1-1.1.1",
    [[
        CREATE TRIGGER trig UPDATE ON no_such_table BEGIN
          SELECT * from sqlite_master;
        END;
    ]], {
        -- <trigger1-1.1.1>
        1, "no such table: NO_SUCH_TABLE"
        -- </trigger1-1.1.1>
    })

test:do_catchsql_test(
    "trigger1-1.1.2",
    [[
        CREATE TRIGGER trig UPDATE ON no_such_table BEGIN
          SELECT * from sqlite_master;
        END;
    ]], {
        -- <trigger1-1.1.2>
        1, "no such table: NO_SUCH_TABLE"
        -- </trigger1-1.1.2>
    })



test:execsql [[
    CREATE TABLE t1(a int PRIMARY KEY);
]]
test:do_catchsql_test(
    "trigger1-1.1.3",
    [[
        CREATE TRIGGER trig UPDATE ON t1 FOR EACH STATEMENT BEGIN
           SELECT * FROM sqlite_master;
        END;
    ]], {
        -- <trigger1-1.1.3>
        1, [[near "STATEMENT": syntax error]]
        -- </trigger1-1.1.3>
    })

test:execsql [[
    CREATE TRIGGER tr1 INSERT ON t1 BEGIN
      INSERT INTO t1 values(1);
     END;
]]
test:do_catchsql_test(
    "trigger1-1.2.0",
    [[
        CREATE TRIGGER IF NOT EXISTS tr1 DELETE ON t1 BEGIN
            SELECT * FROM sqlite_master;
         END
    ]], {
        -- <trigger1-1.2.0>
        0
        -- </trigger1-1.2.0>
    })

test:do_catchsql_test(
    "trigger1-1.2.1",
    [[
        CREATE TRIGGER tr1 DELETE ON t1 BEGIN
            SELECT * FROM sqlite_master;
         END
    ]], {
        -- <trigger1-1.2.1>
        1, "trigger TR1 already exists"
        -- </trigger1-1.2.1>
    })

test:do_catchsql_test(
    "trigger1-1.2.2",
    [[
        CREATE TRIGGER tr1 DELETE ON t1 BEGIN
            SELECT * FROM sqlite_master;
         END
    ]], {
        -- <trigger1-1.2.2>
        1, [[trigger TR1 already exists]]
        -- </trigger1-1.2.2>
    })

-- do_test trigger1-1.3 {
--     catchsql {
--         BEGIN;
--         CREATE TRIGGER tr2 INSERT ON t1 BEGIN
--             SELECT * from sqlite_master; END;
--         ROLLBACK;
--         CREATE TRIGGER tr2 INSERT ON t1 BEGIN
--             SELECT * from sqlite_master; END;
--     }
-- } {0 {}}
-- do_test trigger1-1.4 {
--     catchsql {
--         DROP TRIGGER IF EXISTS tr1;
--         CREATE TRIGGER tr1 DELETE ON t1 BEGIN
--             SELECT * FROM sqlite_master;
--         END
--     }
-- } {0 {}}
-- do_test trigger1-1.5 {
--     execsql {
--         BEGIN;
--         DROP TRIGGER tr2;
--         ROLLBACK;
--         DROP TRIGGER tr2;
--     }
-- } {}
test:do_catchsql_test(
    "trigger1-1.6.1",
    [[
        DROP TRIGGER IF EXISTS biggles;
    ]], {
        -- <trigger1-1.6.1>
        0
        -- </trigger1-1.6.1>
    })

test:do_catchsql_test(
    "trigger1-1.6.2",
    [[
        DROP TRIGGER biggles;
    ]], {
        -- <trigger1-1.6.2>
        1, "no such trigger: BIGGLES"
        -- </trigger1-1.6.2>
    })

test:do_catchsql_test(
    "trigger1-1.7",
    [[
        DROP TABLE t1;
        DROP TRIGGER tr1;
    ]], {
        -- <trigger1-1.7>
        1, "no such trigger: TR1"
        -- </trigger1-1.7>
    })

-- MUST_WORK_TEST
-- ifcapable tempdb {
--   execsql {
--     CREATE TEMP TABLE temp_table(a int PRIMARY KEY);
--   }
--   do_test trigger1-1.8 {
--     execsql {
--           CREATE TRIGGER temp_trig UPDATE ON temp_table BEGIN
--               SELECT * from _space;
--           END;
--           SELECT count(*) FROM _trigger WHERE name = 'temp_trig';
--     }
--   } {0}
-- }
-- do_test trigger1-1.9 {
--   catchsql {
--     CREATE TRIGGER tr1 AFTER UPDATE ON sqlite_master BEGIN
--        SELECT * FROM sqlite_master;
--     END;
--   }
-- } {1 {cannot create trigger on system table}}
-- Check to make sure that a DELETE statement within the body of
-- a trigger does not mess up the DELETE that caused the trigger to
-- run in the first place.
--
test:do_execsql_test(
    "trigger1-1.10",
    [[
        create table t1(a int PRIMARY KEY,b);
        insert into t1 values(1,'a');
        insert into t1 values(2,'b');
        insert into t1 values(3,'c');
        insert into t1 values(4,'d');
        create trigger r1 after delete on t1 for each row begin
          delete from t1 WHERE a=old.a+2;
        end;
        delete from t1 where a=1 OR a=3;
        select * from t1;
        drop table t1;
    ]], {
        -- <trigger1-1.10>
        2, "b", 4, "d"
        -- </trigger1-1.10>
    })

test:do_execsql_test(
    "trigger1-1.11",
    [[
        create table t1(a int PRIMARY KEY,b);
        create table tt1(a int PRIMARY KEY);
        insert into t1 values(1,'a');
        insert into t1 values(2,'b');
        insert into t1 values(3,'c');
        insert into t1 values(4,'d');
        create trigger r11 after update on t1 for each row begin
          delete from t1 WHERE a=old.a+2;
          insert into tt1 values(1);
        end;
        update t1 set b='x-' || b where a=1 OR a=3;
        select * from t1;
        drop table t1;
    ]], {
        -- <trigger1-1.11>
        1, "x-a", 2, "b", 4, "d"
        -- </trigger1-1.11>
    })

-- Ensure that we cannot create INSTEAD OF triggers on tables
test:do_catchsql_test(
    "trigger1-1.12",
    [[
        create table t1(a int PRIMARY KEY,b);
        create trigger t1t instead of update on t1 for each row begin
          delete from t1 WHERE a=old.a+2;
        end;
    ]], {
        -- <trigger1-1.12>
        1, "cannot create INSTEAD OF trigger on table: T1"
        -- </trigger1-1.12>
    })

-- Ensure that we cannot create BEFORE triggers on views
test:do_catchsql_test(
    "trigger1-1.13",
    [[
        create view v1 as select * from t1;
        create trigger v1t before update on v1 for each row begin
          delete from t1 WHERE a=old.a+2;
        end;
    ]], {
        -- <trigger1-1.13>
        1, "cannot create BEFORE trigger on view: V1"
        -- </trigger1-1.13>
    })

-- Ensure that we cannot create AFTER triggers on views
test:do_catchsql_test(
    "trigger1-1.14",
    [[
        drop view v1;
        create view v1 as select * from t1;
        create trigger v1t AFTER update on v1 for each row begin
          delete from t1 WHERE a=old.a+2;
        end;
    ]], {
        -- <trigger1-1.14>
        1, "cannot create AFTER trigger on view: V1"
        -- </trigger1-1.14>
    })



-- ifcapable view
-- Check for memory leaks in the trigger parser
--
test:do_catchsql_test(
    "trigger1-2.1",
    [[
        CREATE TRIGGER r1 AFTER INSERT ON t1 BEGIN
          SELECT * FROM;  -- Syntax error
        END;
    ]], {
        -- <trigger1-2.1>
        1, [[near ";": syntax error]]
        -- </trigger1-2.1>
    })

test:do_catchsql_test(
    "trigger1-2.2",
    [[
        CREATE TRIGGER r1 AFTER INSERT ON t1 BEGIN
          SELECT * FROM t1;
          SELECT * FROM;  -- Syntax error
        END;
    ]], {
        -- <trigger1-2.2>
        1, [[near ";": syntax error]]
        -- </trigger1-2.2>
    })

-- MUST_WORK_TEST
-- Create a trigger that refers to a table that might not exist.
--
-- ifcapable tempdb {
--   do_test trigger1-3.1 {
--     execsql {
--       CREATE TEMP TABLE t2(x int PRIMARY KEY,y);
--     }
--     catchsql {
--       CREATE TRIGGER r1 AFTER INSERT ON t1 BEGIN
--         INSERT INTO t2 VALUES(NEW.a,NEW.b);
--       END;
--     }
--   } {0 {}}
--   do_test trigger1-3.2 {
--     catchsql {
--       INSERT INTO t1 VALUES(1,2);
--       SELECT * FROM t2;
--     }
--   } {1 {no such table: main.t2}}
-- do_test trigger1-3.3 {
--   db close
--   set rc [catch {sqlite3 db test.db} err]
--   if {$rc} {lappend rc $err}
--   set rc
-- } {0}
-- do_test trigger1-3.4 {
--   catchsql {
--     INSERT INTO t1 VALUES(1,2);
--     SELECT * FROM t2;
--   }
-- } {1 {no such table: main.t2}}
--   do_test trigger1-3.5 {
--     catchsql {
--       CREATE TEMP TABLE t2(x,y);
--       INSERT INTO t1 VALUES(1,2);
--       SELECT * FROM t2;
--     }
--   } {1 {no such table: main.t2}}
--   do_test trigger1-3.6.1 {
--     catchsql {
--       DROP TRIGGER r1;
--       CREATE TEMP TRIGGER r1 AFTER INSERT ON t1 BEGIN
--         INSERT INTO t2 VALUES(NEW.a,NEW.b), (NEW.b*100, NEW.a*100);
--       END;
--       INSERT INTO t1 VALUES(1,2);
--       SELECT * FROM t2;
--     }
--   } {0 {1 2 200 100}}
--   do_test trigger1-3.6.2 {
--     catchsql {
--       DROP TRIGGER r1;
--       DELETE FROM t1;
--       DELETE FROM t2;
--       CREATE TEMP TRIGGER r1 AFTER INSERT ON t1 BEGIN
--         INSERT INTO t2 VALUES(NEW.a,NEW.b);
--       END;
--       INSERT INTO t1 VALUES(1,2);
--       SELECT * FROM t2;
--     }
--   } {0 {1 2}}
--   do_test trigger1-3.7 {
--     execsql {
--       DROP TABLE t2;
--       CREATE TABLE t2(x,y);
--       SELECT * FROM t2;
--     }
--   } {}
--   # There are two versions of trigger1-3.8 and trigger1-3.9. One that uses
--   # compound SELECT statements, and another that does not.
--   ifcapable compound {
--   do_test trigger1-3.8 {
--     execsql {
--       INSERT INTO t1 VALUES(3,4);
--       SELECT * FROM t1 UNION ALL SELECT * FROM t2;
--     }
--   } {1 2 3 4 3 4}
--   do_test trigger1-3.9 {
--     db close
--     sqlite3 db test.db
--     execsql {
--       INSERT INTO t1 VALUES(5,6);
--       SELECT * FROM t1 UNION ALL SELECT * FROM t2;
--     }
--   } {1 2 3 4 5 6 3 4}
--   } ;# ifcapable compound
--   ifcapable !compound {
--   do_test trigger1-3.8 {
--     execsql {
--       INSERT INTO t1 VALUES(3,4);
--       SELECT * FROM t1;
--       SELECT * FROM t2;
--     }
--   } {1 2 3 4 3 4}
--   do_test trigger1-3.9 {
--     db close
--     sqlite3 db test.db
--     execsql {
--       INSERT INTO t1 VALUES(5,6);
--       SELECT * FROM t1;
--       SELECT * FROM t2;
--     }
--   } {1 2 3 4 5 6 3 4}
--   } ;# ifcapable !compound
--   do_test trigger1-4.1 {
--     execsql {
--       CREATE TEMP TRIGGER r1 BEFORE INSERT ON t1 BEGIN
--         INSERT INTO t2 VALUES(NEW.a,NEW.b);
--       END;
--       INSERT INTO t1 VALUES(7,8);
--       SELECT * FROM t2;
--     }
--   } {3 4 7 8}
--   do_test trigger1-4.2 {
--     sqlite3 db2 test.db
--     execsql {
--       INSERT INTO t1 VALUES(9,10);
--     } db2;
--     db2 close
--     execsql {
--       SELECT * FROM t2;
--     }
--   } {3 4 7 8}
--   do_test trigger1-4.3 {
--     execsql {
--       DROP TABLE t1;
--       SELECT * FROM t2;
--     };
--   } {3 4 7 8}
--   do_test trigger1-4.4 {
--     db close
--     sqlite3 db test.db
--     execsql {
--       SELECT * FROM t2;
--     };
--   } {3 4 7 8}
-- } else {
--   execsql {
--     CREATE TABLE t2(x,y);
--     DROP TABLE t1;
--     INSERT INTO t2 VALUES(3, 4);
--     INSERT INTO t2 VALUES(7, 8);
--   }
-- }
test:execsql [[
    CREATE TABLE t2(x int PRIMARY KEY,y);
    DROP TABLE t1;
    INSERT INTO t2 VALUES(3, 4);
    INSERT INTO t2 VALUES(7, 8);
]]
-- integrity_check trigger1-5.1
-- Create a trigger with the same name as a table.  Make sure the
-- trigger works.  Then drop the trigger.  Make sure the table is
-- still there.
--
view_v1 = ""
view_v1 = "view v1"


-- do_test trigger1-6.1 {
--   execsql {SELECT type, name FROM sqlite_master}
-- } [concat $view_v1 {table t2}]
test:do_execsql_test(
    "trigger1-6.2",
    [[
        CREATE TRIGGER t2 BEFORE DELETE ON t2 BEGIN
          SELECT RAISE(ABORT,'deletes are not permitted');
        END;
    ]], {
        -- <trigger1-6.2>
        -- X(430, "X!expr", "[]")
        -- </trigger1-6.2>
    })

test:do_catchsql_test(
    "trigger1-6.3",
    [[
        DELETE FROM t2
    ]], {
        -- <trigger1-6.3>
        1, "deletes are not permitted"
        -- </trigger1-6.3>
    })

-- verify_ex_errcode trigger1-6.3b SQLITE_CONSTRAINT_TRIGGER
test:do_execsql_test(
    "trigger1-6.4",
    [[
        SELECT * FROM t2
    ]], {
        -- <trigger1-6.4>
        3, 4, 7, 8
        -- </trigger1-6.4>
    })

-- do_test trigger1-6.5 {
--   db close
--   sqlite3 db test.db
--   execsql {SELECT type, name FROM sqlite_master}
-- } [concat $view_v1 {table t2 trigger t2}]
-- do_test trigger1-6.6 {
--   execsql {
--     DROP TRIGGER t2;
--     SELECT type, name FROM sqlite_master;
--   }
-- } [concat $view_v1 {table t2}]
-- do_test trigger1-6.7 {
--   execsql {SELECT * FROM t2}
-- } {3 4 7 8}
-- do_test trigger1-6.8 {
--   db close
--   sqlite3 db test.db
--   execsql {SELECT * FROM t2}
-- } {3 4 7 8}
test:execsql "DROP TRIGGER IF EXISTS t2"
-- integrity_check trigger1-7.1
-- Check to make sure the name of a trigger can be quoted so that keywords
-- can be used as trigger names.  Ticket #468
--
test:do_execsql_test(
    "trigger1-8.1",
    [[
        CREATE TRIGGER "trigger" AFTER INSERT ON t2 BEGIN SELECT 1; END;
        SELECT "name" FROM "_trigger" WHERE "name"='trigger';
    ]], {
        -- <trigger1-8.1>
        "trigger"
        -- </trigger1-8.1>
    })

test:do_execsql_test(
    "trigger1-8.2",
    [[
        DROP TRIGGER "trigger";
        SELECT "name" FROM "_trigger" WHERE "name"='trigger';
    ]], {
        -- <trigger1-8.2>
        
        -- </trigger1-8.2>
    })

test:do_execsql_test(
    "trigger1-8.3",
    [[
        CREATE TRIGGER "trigger" AFTER INSERT ON t2 BEGIN SELECT 1; END;
        SELECT "name" FROM "_trigger" WHERE "name"='trigger';
    ]], {
        -- <trigger1-8.3>
        "trigger"
        -- </trigger1-8.3>
    })

test:do_execsql_test(
    "trigger1-8.4",
    [[
        DROP TRIGGER "trigger";
        SELECT "name" FROM "_trigger" WHERE "name"='trigger';
    ]], {
        -- <trigger1-8.4>
        
        -- </trigger1-8.4>
    })

-- ifcapable conflict {
--   # Make sure REPLACE works inside of triggers.
--   #
--   # There are two versions of trigger1-9.1 and trigger1-9.2. One that uses
--   # compound SELECT statements, and another that does not.
--   ifcapable compound {
-- MUST_WORK_TEST
--     do_test trigger1-9.1 {
--       execsql {
--         CREATE TABLE t3(a,b int PRIMARY KEY);
--         CREATE TABLE t4(x int PRIMARY KEY, b);
--         CREATE TRIGGER r34 AFTER INSERT ON t3 BEGIN
--           REPLACE INTO t4 VALUES(new.a,new.b);
--         END;
--         INSERT INTO t3 VALUES(1,2);
--         SELECT * FROM t3 UNION ALL SELECT 99, 99 UNION ALL SELECT * FROM t4;
--       }
--     } {1 2 99 99 1 2}
-- MUST_WORK_TEST
--     do_test trigger1-9.2 {
--       execsql {
--         INSERT INTO t3 VALUES(1,3);
--       }
--     #   SELECT * FROM t3 UNION ALL SELECT 99, 99 UNION ALL SELECT * FROM t4;
--     } {1 2 1 3 99 99 1 3}
--   } else {
--     do_test trigger1-9.1 {
--       execsql {
--         CREATE TABLE t3(a, b int PRIMARY KEY);
--         CREATE TABLE t4(x int PRIMARY KEY, b);
--         CREATE TRIGGER r34 AFTER INSERT ON t3 BEGIN
--           REPLACE INTO t4 VALUES(new.a,new.b);
--         END;
--         INSERT INTO t3 VALUES(1,2);
--         SELECT * FROM t3; SELECT 99, 99; SELECT * FROM t4;
--       }
--     } {1 2 99 99 1 2}
--     do_test trigger1-9.2 {
--       execsql {
--         INSERT INTO t3 VALUES(1,3);
--         SELECT * FROM t3; SELECT 99, 99; SELECT * FROM t4;
--       }
--     } {1 2 1 3 99 99 1 3}
--   }
--   execsql {
--     DROP TABLE t3;
--     DROP TABLE t4;
--   }
-- }
-- # Ticket #764. At one stage TEMP triggers would fail to re-install when the
-- # schema was reloaded. The following tests ensure that TEMP triggers are
-- # correctly re-installed.
-- #
-- # Also verify that references within trigger programs are resolved at
-- # statement compile time, not trigger installation time. This means, for
-- # example, that you can drop and re-create tables referenced by triggers.
-- ifcapable tempdb&&attach {
--   do_test trigger1-10.0 {
--     forcedelete test2.db
--     forcedelete test2.db-journal
--     execsql {
--       ATTACH 'test2.db' AS aux;
--     }
--   } {}
--   do_test trigger1-10.1 {
--     execsql {
--       CREATE TABLE main.t4(a, b, c);
--       CREATE TABLE temp.t4(a, b, c);
--       CREATE TABLE aux.t4(a, b, c);
--       CREATE TABLE insert_log(db, a, b, c);
--     }
--   } {}
--   do_test trigger1-10.2 {
--     execsql {
--       CREATE TEMP TRIGGER trig1 AFTER INSERT ON main.t4 BEGIN
--         INSERT INTO insert_log VALUES('main', new.a, new.b, new.c);
--       END;
--       CREATE TEMP TRIGGER trig2 AFTER INSERT ON temp.t4 BEGIN
--         INSERT INTO insert_log VALUES('temp', new.a, new.b, new.c);
--       END;
--       CREATE TEMP TRIGGER trig3 AFTER INSERT ON aux.t4 BEGIN
--         INSERT INTO insert_log VALUES('aux', new.a, new.b, new.c);
--       END;
--     }
--   } {}
--   do_test trigger1-10.3 {
--     execsql {
--       INSERT INTO main.t4 VALUES(1, 2, 3);
--       INSERT INTO temp.t4 VALUES(4, 5, 6);
--       INSERT INTO aux.t4  VALUES(7, 8, 9);
--     }
--   } {}
--   do_test trigger1-10.4 {
--     execsql {
--       SELECT * FROM insert_log;
--     }
--   } {main 1 2 3 temp 4 5 6 aux 7 8 9}
--   do_test trigger1-10.5 {
--     execsql {
--       BEGIN;
--       INSERT INTO main.t4 VALUES(1, 2, 3);
--       INSERT INTO temp.t4 VALUES(4, 5, 6);
--       INSERT INTO aux.t4  VALUES(7, 8, 9);
--       ROLLBACK;
--     }
--   } {}
--   do_test trigger1-10.6 {
--     execsql {
--       SELECT * FROM insert_log;
--     }
--   } {main 1 2 3 temp 4 5 6 aux 7 8 9}
--   do_test trigger1-10.7 {
--     execsql {
--       DELETE FROM insert_log;
--       INSERT INTO main.t4 VALUES(11, 12, 13);
--       INSERT INTO temp.t4 VALUES(14, 15, 16);
--       INSERT INTO aux.t4  VALUES(17, 18, 19);
--     }
--   } {}
--   do_test trigger1-10.8 {
--     execsql {
--       SELECT * FROM insert_log;
--     }
--   } {main 11 12 13 temp 14 15 16 aux 17 18 19}
--   do_test trigger1-10.9 {
--   # Drop and re-create the insert_log table in a different database. Note
--   # that we can change the column names because the trigger programs don't
--   # use them explicitly.
--     execsql {
--       DROP TABLE insert_log;
--       CREATE TABLE aux.insert_log(db, d, e, f);
--     }
--   } {}
--   do_test trigger1-10.10 {
--     execsql {
--       INSERT INTO main.t4 VALUES(21, 22, 23);
--       INSERT INTO temp.t4 VALUES(24, 25, 26);
--       INSERT INTO aux.t4  VALUES(27, 28, 29);
--     }
--   } {}
--   do_test trigger1-10.11 {
--     execsql {
--       SELECT * FROM insert_log;
--     }
--   } {main 21 22 23 temp 24 25 26 aux 27 28 29}
-- }
test:do_catchsql_test(
    "trigger1-11.1",
    [[
        SELECT raise(abort,'message');
    ]], {
        -- <trigger1-11.1>
        1, "RAISE() may only be used within a trigger-program"
        -- </trigger1-11.1>
    })

-- MUST_WORK_TEST
-- do_test trigger1-15.1 {
--   execsql {
--     CREATE TABLE tA(a INTEGER PRIMARY KEY, b, c);
--     CREATE TRIGGER tA_trigger BEFORE UPDATE ON "tA" BEGIN SELECT 1; END;
--     INSERT INTO tA VALUES(1, 2, 3);
--   }
--   catchsql { UPDATE tA SET a = 'abc' }
-- } {1 {datatype mismatch}}
-- do_test trigger1-15.2 {
--   catchsql { INSERT INTO tA VALUES('abc', 2, 3) }
-- } {1 {datatype mismatch}}
test:execsql [[
    CREATE TABLE tA(a INTEGER PRIMARY KEY, b, c);
    CREATE TRIGGER tA_trigger BEFORE UPDATE ON tA BEGIN SELECT 1; END;
    INSERT INTO tA VALUES(1, 2, 3);
]]
-- Ticket #3947:  Do not allow qualified table names on INSERT, UPDATE, and
-- DELETE statements within triggers.  Actually, this has never been allowed
-- by the grammar.  But the error message is confusing: one simply gets a
-- "syntax error".  That has now been changed to give a full error message.
--
test:do_test(
    "trigger1-16.1",
    function()
        test:execsql [[
            CREATE TABLE t16(a int PRIMARY KEY,b,c);
            CREATE INDEX t16b ON t16(b);
        ]]
        return test:catchsql [[
            CREATE TRIGGER t16err1 AFTER INSERT ON tA BEGIN
              INSERT INTO t16 VALUES(1,2,3);
            END;
        ]]
    end, {
        -- <trigger1-16.1>
        --1, "near \".\": syntax error"
	0
        -- </trigger1-16.1>
    })

test:do_catchsql_test(
    "trigger1-16.2",
    [[
        CREATE TRIGGER t16err2 AFTER INSERT ON tA BEGIN
          UPDATE t16 SET rowid=rowid+1;
        END;
    ]], {
        -- <trigger1-16.2>
        --1, "near \".\": syntax error"
	0
        -- </trigger1-16.2>
    })

test:do_catchsql_test(
    "trigger1-16.3",
    [[
        CREATE TRIGGER t16err3 AFTER INSERT ON tA BEGIN
          DELETE FROM t16;
        END;
    ]], {
        -- <trigger1-16.3>
        --1, "near \".\": syntax error"
	0
        -- </trigger1-16.3>
    })

test:do_catchsql_test(
    "trigger1-16.4",
    [[
        CREATE TRIGGER t16err4 AFTER INSERT ON tA BEGIN
          UPDATE t16 NOT INDEXED SET rowid=rowid+1;
        END;
    ]], {
        -- <trigger1-16.4>
        1, "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements within triggers"
        -- </trigger1-16.4>
    })

test:do_catchsql_test(
    "trigger1-16.5",
    [[
        CREATE TRIGGER t16err5 AFTER INSERT ON tA BEGIN
          UPDATE t16 INDEXED BY t16a SET rowid=rowid+1 WHERE a=1;
        END;
    ]], {
        -- <trigger1-16.5>
        1, "the INDEXED BY clause is not allowed on UPDATE or DELETE statements within triggers"
        -- </trigger1-16.5>
    })

test:do_catchsql_test(
    "trigger1-16.6",
    [[
        CREATE TRIGGER t16err6 AFTER INSERT ON tA BEGIN
          DELETE FROM t16 NOT INDEXED WHERE a=123;
        END;
    ]], {
        -- <trigger1-16.6>
        1, "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements within triggers"
        -- </trigger1-16.6>
    })

test:do_catchsql_test(
    "trigger1-16.7",
    [[
        CREATE TRIGGER t16err7 AFTER INSERT ON tA BEGIN
          DELETE FROM t16 INDEXED BY t16a WHERE a=123;
        END;
    ]], {
        -- <trigger1-16.7>
        1, "the INDEXED BY clause is not allowed on UPDATE or DELETE statements within triggers"
        -- </trigger1-16.7>
    })

test:do_catchsql_test(
    "trigger1-16.8",
    [[
        BEGIN;
          CREATE TRIGGER tr168 INSERT ON tA BEGIN
            INSERT INTO t16 values(1);
          END;
   ]], {
        1, [[Space _trigger does not support multi-statement transactions]]
})

test:execsql [[
    ROLLBACK;
]]

test:do_catchsql_test(
    "trigger1-16.9",
    [[
        BEGIN;
          DROP TRIGGER t16err3;
   ]], {
        1, [[Space _trigger does not support multi-statement transactions]]
})
-- MUST_WORK_TEST
-- #-------------------------------------------------------------------------
-- # Test that bug [34cd55d68e0e6e7c] has been fixed.
-- #
-- do_execsql_test trigger1-17.0 {
--   CREATE TABLE t17a(ii INT);
--   CREATE TABLE t17b(tt TEXT PRIMARY KEY, ss);
--   CREATE TRIGGER t17a_ai AFTER INSERT ON t17a BEGIN
--     INSERT INTO t17b(tt) VALUES(new.ii);
--   END;
--   CREATE TRIGGER t17b_ai AFTER INSERT ON t17b BEGIN
--     UPDATE t17b SET ss = 4;
--   END;
--   INSERT INTO t17a(ii) VALUES('1');
--   PRAGMA integrity_check;
-- } {ok}
-- MUST_WORK_TEST
test:finish_test()

