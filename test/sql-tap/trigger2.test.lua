#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(26)

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
-- Regression testing of FOR EACH ROW table triggers
--
-- 1. Trigger execution order tests.
-- These tests ensure that BEFORE and AFTER triggers are fired at the correct
-- times relative to each other and the triggering statement.
--
-- trigger2-1.1.*: ON UPDATE trigger execution model.
-- trigger2-1.2.*: DELETE trigger execution model.
-- trigger2-1.3.*: INSERT trigger execution model.
--
-- 2. Trigger program execution tests.
-- These tests ensure that trigger programs execute correctly (ie. that a
-- trigger program can correctly execute INSERT, UPDATE, DELETE * SELECT
-- statements, and combinations thereof).
--
-- 3. Selective trigger execution
-- This tests that conditional triggers (ie. UPDATE OF triggers and triggers
-- with WHEN clauses) are fired only fired when they are supposed to be.
--
-- trigger2-3.1: UPDATE OF triggers
-- trigger2-3.2: WHEN clause
--
-- 4. Cascaded trigger execution
-- Tests that trigger-programs may cause other triggers to fire. Also that a
-- trigger-program is never executed recursively.
--
-- trigger2-4.1: Trivial cascading trigger
-- trigger2-4.2: Trivial recursive trigger handling
--
-- 5. Count changes behaviour.
-- Verify that rows altered by triggers are not included in the return value
-- of the "count changes" interface.
--
-- 6. ON CONFLICT clause handling
-- trigger2-6.1[a-f]: INSERT statements
-- trigger2-6.2[a-f]: UPDATE statements
--
-- 7. & 8. Triggers on views fire correctly.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- The tests in this file were written before SQLite supported recursive
-- trigger invocation, and some tests depend on that to pass. So disable
-- recursive triggers for this file.
test:catchsql " pragma recursive_triggers = off "
-- 1.
ii = 0
tbl_definitions = { "CREATE TABLE tbl (a INTEGER PRIMARY KEY, b INT );",
                    "CREATE TABLE tbl (a  INT PRIMARY KEY, b INT );",
                    "CREATE TABLE tbl (a INT , b  INT PRIMARY KEY);",
                    "CREATE TABLE tbl (a INT , b INTEGER PRIMARY KEY);" }
-- Tarantool: temporary tables are not supported so far. #2119
-- table.insert(tbl_definitions,"CREATE TEMP TABLE tbl (a, b INTEGER PRIMARY KEY);")
-- table.insert(tbl_definitions,"CREATE TEMP TABLE tbl (a INTEGER PRIMARY KEY, b);")
-- table.insert(tbl_definitions,"CREATE TEMPORARY TABLE tbl (a INTEGER PRIMARY KEY, b);")


for _, tbl_defn in ipairs(tbl_definitions) do
    ii = ii + 1
    test:catchsql [[
        DROP TABLE tbl;
        DROP TABLE rlog;
        DROP TABLE clog;
        DROP TABLE other_tbl;
    ]]
    test:execsql(tbl_defn)
    test:execsql [[
        INSERT INTO tbl VALUES(1, 2);
        INSERT INTO tbl VALUES(3, 4);
    ]]
    test:execsql [[
        CREATE TABLE rlog (idx INTEGER PRIMARY KEY, old_a INT , old_b INT , db_sum_a INT , db_sum_b INT , new_a INT , new_b INT );
        CREATE TABLE clog (idx INTEGER PRIMARY KEY, old_a INT , old_b INT , db_sum_a INT , db_sum_b INT , new_a INT , new_b INT );
    ]]
    test:execsql [[
        CREATE TRIGGER before_update_row BEFORE UPDATE ON tbl FOR EACH ROW
          BEGIN
            INSERT INTO rlog VALUES ((SELECT coalesce(max(idx),0) + 1 FROM rlog),
                                     old.a, old.b,
                                     (SELECT coalesce(sum(a),0) FROM tbl),
                                     (SELECT coalesce(sum(b),0) FROM tbl),
                                     new.a, new.b);
          END;

        CREATE TRIGGER after_update_row AFTER UPDATE ON tbl FOR EACH ROW
          BEGIN
            INSERT INTO rlog VALUES ( (SELECT coalesce(max(idx),0) + 1 FROM rlog),
                                      old.a, old.b,
                                      (SELECT coalesce(sum(a),0) FROM tbl),
                                      (SELECT coalesce(sum(b),0) FROM tbl),
                                      new.a, new.b);
          END;

        CREATE TRIGGER conditional_update_row AFTER UPDATE ON tbl FOR EACH ROW
          WHEN old.a = 1
            BEGIN
              INSERT INTO clog VALUES ( (SELECT coalesce(max(idx),0) + 1 FROM clog),
                                        old.a, old.b,
                                        (SELECT coalesce(sum(a),0) FROM tbl),
                                        (SELECT coalesce(sum(b),0) FROM tbl),
                                        new.a, new.b);
            END;
    ]]
    test:do_test(
        "trigger2-1."..ii..".1",
        function()
            r = {}
            test:execsql [[ UPDATE tbl SET a = a * 10, b = b * 10; ]]
            raw_result = test:execsql [[
                SELECT * FROM rlog ORDER BY idx;
            ]]
            for k,v in pairs(raw_result) do table.insert(r, v) end

            raw_result = test:execsql [[
                SELECT * FROM clog ORDER BY idx;
            ]]
            for k,v in pairs(raw_result) do table.insert(r, v) end
            
            return r
        end, {
            1, 1, 2, 4, 6, 10, 20, 2, 1, 2, 13, 24, 10, 20, 3, 3, 4, 13, 24, 30, 40, 4, 3, 4, 40, 60, 30, 40, 1, 1, 2, 13, 24, 10, 20
        })

    test:execsql [[
        DELETE FROM tbl;
        DELETE FROM rlog;
        INSERT INTO tbl VALUES (100, 100);
        INSERT INTO tbl VALUES (300, 200);
        CREATE TRIGGER delete_before_row BEFORE DELETE ON tbl FOR EACH ROW
          BEGIN
          INSERT INTO rlog VALUES ( (SELECT coalesce(max(idx),0) + 1 FROM rlog),
        old.a, old.b,
        (SELECT coalesce(sum(a),0) FROM tbl),
            (SELECT coalesce(sum(b),0) FROM tbl),
        0, 0);
        END;

        CREATE TRIGGER delete_after_row AFTER DELETE ON tbl FOR EACH ROW
          BEGIN
          INSERT INTO rlog VALUES ( (SELECT coalesce(max(idx),0) + 1 FROM rlog),
        old.a, old.b,
        (SELECT coalesce(sum(a),0) FROM tbl),
            (SELECT coalesce(sum(b),0) FROM tbl),
        0, 0);
        END;
    ]]
    test:do_test(
        "trigger2-1."..ii..".2",
        function()
            r = {}
            for _, v in ipairs(test:execsql [[
                DELETE FROM tbl ;
                SELECT * FROM rlog;
            ]]) do
                table.insert(r, v)
            end
            return r
        end, {
            1, 100, 100, 400, 300, 0, 0, 2, 100, 100, 300, 200, 0, 0, 3, 300, 200, 300, 200, 0, 0, 4, 300, 200, 0, 0, 0, 0
        })

    test:execsql [[
        DELETE FROM rlog;
        CREATE TRIGGER insert_before_row BEFORE INSERT ON tbl FOR EACH ROW
          BEGIN
          INSERT INTO rlog VALUES ( (SELECT coalesce(max(idx),0) + 1 FROM rlog),
        0, 0,
        (SELECT coalesce(sum(a),0) FROM tbl),
            (SELECT coalesce(sum(b),0) FROM tbl),
        new.a, new.b);
        END;

        CREATE TRIGGER insert_after_row AFTER INSERT ON tbl FOR EACH ROW
          BEGIN
          INSERT INTO rlog VALUES ( (SELECT coalesce(max(idx),0) + 1 FROM rlog),
        0, 0,
        (SELECT coalesce(sum(a),0) FROM tbl),
            (SELECT coalesce(sum(b),0) FROM tbl),
        new.a, new.b);
        END;
    ]]
    test:do_execsql_test(
        "trigger2-1."..ii..".3",
        [[

            CREATE TABLE other_tbl(a  INT PRIMARY KEY, b INT );
            INSERT INTO other_tbl VALUES(1, 2);
            INSERT INTO other_tbl VALUES(3, 4);
            -- INSERT INTO tbl SELECT * FROM other_tbl;
            INSERT INTO tbl VALUES(5, 6);
            DROP TABLE other_tbl;

            SELECT * FROM rlog;
        ]], {
            1, 0, 0, 0, 0, 5, 6, 2, 0, 0, 5, 6, 5, 6
        })

    --     integrity_check trigger2-1.$ii.4
end
test:catchsql [[
    DROP TABLE rlog;
    DROP TABLE clog;
    DROP TABLE tbl;
    DROP TABLE other_tbl;
]]


-- MUST_WORK_TEST
-- # 2.
-- set ii 0
-- foreach tr_program {
--   {UPDATE tbl SET b = old.b;}
--   {INSERT INTO log VALUES(new.c, 2, 3);}
--   {DELETE FROM log WHERE a = 1;}
--   {INSERT INTO tbl VALUES(500, new.b * 10, 700);
--     UPDATE tbl SET c = old.c;
--     DELETE FROM log;}
--   {INSERT INTO log select * from tbl;}
-- } {
--   foreach test_varset [ list \
--     {
--       set statement {UPDATE tbl SET c = 10 WHERE a = 1;}
--       set prep      {INSERT INTO tbl VALUES(1, 2, 3);}
--       set newC 10
--       set newB 2
--       set newA 1
--       set oldA 1
--       set oldB 2
--       set oldC 3
--     } \
--     {
--       set statement {DELETE FROM tbl WHERE a = 1;}
--       set prep      {INSERT INTO tbl VALUES(1, 2, 3);}
--       set oldA 1
--       set oldB 2
--       set oldC 3
--     } \
--     {
--       set statement {INSERT INTO tbl VALUES(1, 2, 3);}
--       set newA 1
--       set newB 2
--       set newC 3
--     }
--   ] \
--   {
--     set statement {}
--     set prep {}
--     set newA {''}
--     set newB {''}
--     set newC {''}
--     set oldA {''}
--     set oldB {''}
--     set oldC {''}
--     incr ii
--     eval $test_varset
--     set statement_type [string range $statement 0 5]
--     set tr_program_fixed $tr_program
--     if {$statement_type == "DELETE"} {
--       regsub -all new\.a $tr_program_fixed {''} tr_program_fixed
--       regsub -all new\.b $tr_program_fixed {''} tr_program_fixed
--       regsub -all new\.c $tr_program_fixed {''} tr_program_fixed
--     }
--     if {$statement_type == "INSERT"} {
--       regsub -all old\.a $tr_program_fixed {''} tr_program_fixed
--       regsub -all old\.b $tr_program_fixed {''} tr_program_fixed
--       regsub -all old\.c $tr_program_fixed {''} tr_program_fixed
--     }
--     set tr_program_cooked $tr_program
--     regsub -all new\.a $tr_program_cooked $newA tr_program_cooked
--     regsub -all new\.b $tr_program_cooked $newB tr_program_cooked
--     regsub -all new\.c $tr_program_cooked $newC tr_program_cooked
--     regsub -all old\.a $tr_program_cooked $oldA tr_program_cooked
--     regsub -all old\.b $tr_program_cooked $oldB tr_program_cooked
--     regsub -all old\.c $tr_program_cooked $oldC tr_program_cooked
--     catchsql {
--       DROP TABLE tbl;
--       DROP TABLE log;
--     }
--     execsql {
--       CREATE TABLE tbl(a  INT PRIMARY KEY, b INT , c INT );
--       CREATE TABLE log(a INT , b INT , c INT );
--     }
--     set query {SELECT * FROM tbl; SELECT * FROM log;}
--     set prep "$prep; INSERT INTO log VALUES(1, 2, 3);\
--              INSERT INTO log VALUES(10, 20, 30);"
-- # Check execution of BEFORE programs:
--     set before_data [ execsql "$prep $tr_program_cooked $statement $query" ]
--     execsql "DELETE FROM tbl; DELETE FROM log; $prep";
--     execsql "CREATE TRIGGER the_trigger BEFORE [string range $statement 0 6]\
--              ON tbl BEGIN $tr_program_fixed END;"
--     do_test trigger2-2.$ii-before "execsql {$statement $query}" $before_data
--     execsql "DROP TRIGGER the_trigger;"
--     execsql "DELETE FROM tbl; DELETE FROM log;"
-- # Check execution of AFTER programs
--     set after_data [ execsql "$prep $statement $tr_program_cooked $query" ]
--     execsql "DELETE FROM tbl; DELETE FROM log; $prep";
--     execsql "CREATE TRIGGER the_trigger AFTER [string range $statement 0 6]\
--              ON tbl BEGIN $tr_program_fixed END;"
--     do_test trigger2-2.$ii-after "execsql {$statement $query}" $after_data
--     execsql "DROP TRIGGER the_trigger;"
--     integrity_check trigger2-2.$ii-integrity
--   }
-- }
-- catchsql {
--   DROP TABLE tbl;
--   DROP TABLE log;
-- }
-- # 3.
-- MUST_WORK_TEST
-- trigger2-3.1: UPDATE OF triggers
-- execsql {
--   CREATE TABLE tbl (a  INT PRIMARY KEY, b INT , c INT , d INT );
--   CREATE TABLE log (a  INT PRIMARY KEY);
--   INSERT INTO log VALUES (0);
--   INSERT INTO tbl VALUES (0, 0, 0, 0);
--   INSERT INTO tbl VALUES (1, 0, 0, 0);
--   CREATE TRIGGER tbl_after_update_cd BEFORE UPDATE OF c, d ON tbl
--     BEGIN
--       UPDATE log SET a = a + 1;
--     END;
-- }
-- do_test trigger2-3.1 {
--   execsql {
--     UPDATE tbl SET b = 1, c = 10; -- 2
--     UPDATE tbl SET b = 10; -- 0
--     UPDATE tbl SET d = 4 WHERE a = 0; --1
--     UPDATE tbl SET a = 4, b = 10; --0
--     SELECT * FROM log;
--   }
-- } {3}
-- execsql {
--   DROP TABLE tbl;
--   DROP TABLE log;
-- }
-- trigger2-3.2: WHEN clause
when_triggers = { "t1 BEFORE INSERT ON tbl WHEN new.a > 20" }
table.insert(when_triggers,"t2 BEFORE INSERT ON tbl WHEN (SELECT count(*) FROM tbl) = 0")


test:execsql [[
    CREATE TABLE tbl (a  INT , b  INT PRIMARY KEY, c INT , d INT );
    CREATE TABLE log (a  INT PRIMARY KEY);
    INSERT INTO log VALUES (0);
]]
for _, trig in ipairs(when_triggers) do
    test:execsql("CREATE TRIGGER "..trig.." BEGIN UPDATE log set a = a + 1; END;")
end

test:do_test(
    "trigger2-3.2",
    function()
        local r = {}
        table.insert(r, test:execsql([[
        INSERT INTO tbl VALUES(0, 1, 0, 0);     -- 1 (ifcapable subquery)
        SELECT * FROM log;
        UPDATE log SET a = 0;]])[1])

        table.insert(r, test:execsql([[
        INSERT INTO tbl VALUES(0, 2, 0, 0);     -- 0
        SELECT * FROM log;
        UPDATE log SET a = 0;]])[1])

        table.insert(r, test:execsql([[
        INSERT INTO tbl VALUES(200, 3, 0, 0);     -- 1
        SELECT * FROM log;
        UPDATE log SET a = 0;]])[1])

        return r
    end
    , {
        -- <trigger2-3.2>
        1, 0, 1
        -- </trigger2-3.2>
    })

test:execsql [[
    DROP TABLE tbl;
    DROP TABLE log;
]]
-- integrity_check trigger2-3.3
-- # Simple cascaded trigger
test:execsql [[
    CREATE TABLE tblA(a  INT PRIMARY KEY, b INT );
    CREATE TABLE tblB(a  INT PRIMARY KEY, b INT );
    CREATE TABLE tblC(a  INT PRIMARY KEY, b INT );

    CREATE TRIGGER tr1 BEFORE INSERT ON tblA BEGIN
      INSERT INTO tblB values(new.a, new.b);
    END;

    CREATE TRIGGER tr2 BEFORE INSERT ON tblB BEGIN
      INSERT INTO tblC values(new.a, new.b);
    END;
]]
test:do_test(
    "trigger2-4.1",
    function()
        local r = {}
        local raw
        raw = test:execsql [[
        INSERT INTO tblA values(1, 2);
        SELECT * FROM tblA; ]]
        for _, v in ipairs(raw) do
            table.insert(r, v)
        end

        raw = test:execsql [[ SELECT * FROM tblB; ]]
        for _, v in ipairs(raw) do
            table.insert(r, v)
        end
        
        raw = test:execsql [[ SELECT * FROM tblC; ]]
        for _, v in ipairs(raw) do
            table.insert(r, v)
        end

        return r
    end, {
        -- <trigger2-4.1>
        1, 2, 1, 2, 1, 2
        -- </trigger2-4.1>
    })

test:execsql [[
    DROP TABLE tblA;
    DROP TABLE tblB;
    DROP TABLE tblC;
]]
-- Simple recursive trigger
test:execsql [[
    CREATE TABLE tbl(a  INT PRIMARY KEY, b INT , c INT );
    CREATE TRIGGER tbl_trig BEFORE INSERT ON tbl
      BEGIN
        INSERT INTO tbl VALUES (new.a + 1, new.b + 1, new.c + 1);
      END;
]]
test:do_execsql_test(
    "trigger2-4.2",
    [[
        INSERT INTO tbl VALUES (1, 2, 3);
        select * from tbl;
    ]], {
        -- <trigger2-4.2>
        1, 2, 3, 2, 3, 4
        -- </trigger2-4.2>
    })

test:execsql [[
    DROP TABLE tbl;
]]
-- MUST_WORK_TEST
-- 5.
-- execsql {
--   CREATE TABLE tbl(a  INT PRIMARY KEY, b INT , c INT );
--   CREATE TRIGGER tbl_trig BEFORE INSERT ON tbl
--     BEGIN
--       INSERT INTO tbl VALUES (1, 2, 3);
--       INSERT INTO tbl VALUES (2, 2, 3);
--       UPDATE tbl set b = 10 WHERE a = 1;
--       DELETE FROM tbl WHERE a = 1;
--       DELETE FROM tbl;
--     END;
-- }
-- do_test trigger2-5 {
--   execsql {
--     INSERT INTO tbl VALUES(100, 200, 300);
--   }
--   db changes
-- } {1}
-- execsql {
--   DROP TABLE tbl;
-- }
-- MUST_WORK_TEST
-- ifcapable conflict {
--   # Handling of ON CONFLICT by INSERT statements inside triggers
--   execsql {
--     CREATE TABLE tbl (a  INT primary key, b INT , c INT );
--     CREATE TRIGGER ai_tbl AFTER INSERT ON tbl BEGIN
--       INSERT OR IGNORE INTO tbl values (new.a, 0, 0);
--     END;
--   }
--   do_test trigger2-6.1a {
--     execsql {
--       BEGIN;
--       INSERT INTO tbl values (1, 2, 3);
--       SELECT * from tbl;
--     }
--   } {1 2 3}
--   do_test trigger2-6.1b {
--     catchsql {
--       INSERT OR ABORT INTO tbl values (2, 2, 3);
--     }
--   } {1 {UNIQUE constraint failed: tbl.a}}
--   do_test trigger2-6.1c {
--     execsql {
--       SELECT * from tbl;
--     }
--   } {1 2 3}
--   do_test trigger2-6.1d {
--     catchsql {
--       INSERT OR FAIL INTO tbl values (2, 2, 3);
--     }
--   } {1 {UNIQUE constraint failed: tbl.a}}
--   do_test trigger2-6.1e {
--     execsql {
--       SELECT * from tbl;
--     }
--   } {1 2 3 2 2 3}
--   do_test trigger2-6.1f {
--     execsql {
--       INSERT OR REPLACE INTO tbl values (2, 2, 3);
--       SELECT * from tbl;
--     }
--   } {1 2 3 2 0 0}
--   do_test trigger2-6.1g {
--     catchsql {
--       INSERT OR ROLLBACK INTO tbl values (3, 2, 3);
--     }
--   } {1 {UNIQUE constraint failed: tbl.a}}
--   do_test trigger2-6.1h {
--     execsql {
--       SELECT * from tbl;
--     }
--   } {}
--   execsql {DELETE FROM tbl}
--   # Handling of ON CONFLICT by UPDATE statements inside triggers
--   execsql {
--     INSERT INTO tbl values (4, 2, 3);
--     INSERT INTO tbl values (6, 3, 4);
--     CREATE TRIGGER au_tbl AFTER UPDATE ON tbl BEGIN
--       UPDATE OR IGNORE tbl SET a = new.a, c = 10;
--     END;
--   }
--   do_test trigger2-6.2a {
--     execsql {
--       BEGIN;
--       UPDATE tbl SET a = 1 WHERE a = 4;
--       SELECT * from tbl;
--     }
--   } {1 2 10 6 3 4}
--   do_test trigger2-6.2b {
--     catchsql {
--       UPDATE OR ABORT tbl SET a = 4 WHERE a = 1;
--     }
--   } {1 {UNIQUE constraint failed: tbl.a}}
--   do_test trigger2-6.2c {
--     execsql {
--       SELECT * from tbl;
--     }
--   } {1 2 10 6 3 4}
--   do_test trigger2-6.2d {
--     catchsql {
--       UPDATE OR FAIL tbl SET a = 4 WHERE a = 1;
--     }
--   } {1 {UNIQUE constraint failed: tbl.a}}
--   do_test trigger2-6.2e {
--     execsql {
--       SELECT * from tbl;
--     }
--   } {4 2 10 6 3 4}
--   do_test trigger2-6.2f.1 {
--     execsql {
--       UPDATE OR REPLACE tbl SET a = 1 WHERE a = 4;
--       SELECT * from tbl;
--     }
--   } {1 3 10}
--   do_test trigger2-6.2f.2 {
--     execsql {
--       INSERT INTO tbl VALUES (2, 3, 4);
--       SELECT * FROM tbl;
--     }
--   } {1 3 10 2 3 4}
--   do_test trigger2-6.2g {
--     catchsql {
--       UPDATE OR ROLLBACK tbl SET a = 4 WHERE a = 1;
--     }
--   } {1 {UNIQUE constraint failed: tbl.a}}
--   do_test trigger2-6.2h {
--     execsql {
--       SELECT * from tbl;
--     }
--   } {4 2 3 6 3 4}
--   execsql {
--     DROP TABLE tbl;
--   }
-- } ; # ifcapable conflict
-- 7. Triggers on views
test:do_execsql_test(
    "trigger2-7.1",
    [[
        CREATE TABLE ab(a  INT PRIMARY KEY, b INT );
        CREATE TABLE cd(c  INT PRIMARY KEY, d INT );
        INSERT INTO ab VALUES (1, 2);
        INSERT INTO ab VALUES (0, 0);
        INSERT INTO cd VALUES (3, 4);

        CREATE TABLE tlog(ii INTEGER PRIMARY KEY,
            olda INT , oldb INT , oldc INT , oldd INT , newa INT , newb INT , newc INT , newd INT );

        CREATE VIEW abcd AS SELECT a, b, c, d FROM ab, cd;

        CREATE TRIGGER before_update INSTEAD OF UPDATE ON abcd BEGIN
          INSERT INTO tlog VALUES( (SELECT coalesce(max(ii),0) + 1 FROM tlog),
        old.a, old.b, old.c, old.d, new.a, new.b, new.c, new.d);
        END;
        CREATE TRIGGER after_update INSTEAD OF UPDATE ON abcd BEGIN
          INSERT INTO tlog VALUES( (SELECT coalesce(max(ii),0) + 1 FROM tlog),
        old.a, old.b, old.c, old.d, new.a, new.b, new.c, new.d);
        END;

        CREATE TRIGGER before_delete INSTEAD OF DELETE ON abcd BEGIN
          INSERT INTO tlog VALUES( (SELECT coalesce(max(ii),0) + 1 FROM tlog),
        old.a, old.b, old.c, old.d, 0, 0, 0, 0);
        END;
        CREATE TRIGGER after_delete INSTEAD OF DELETE ON abcd BEGIN
          INSERT INTO tlog VALUES( (SELECT coalesce(max(ii),0) + 1 FROM tlog),
        old.a, old.b, old.c, old.d, 0, 0, 0, 0);
        END;

        CREATE TRIGGER before_insert INSTEAD OF INSERT ON abcd BEGIN
          INSERT INTO tlog VALUES( (SELECT coalesce(max(ii),0) + 1 FROM tlog),
        0, 0, 0, 0, new.a, new.b, new.c, new.d);
        END;
         CREATE TRIGGER after_insert INSTEAD OF INSERT ON abcd BEGIN
          INSERT INTO tlog VALUES( (SELECT coalesce(max(ii),0) + 1 FROM tlog),
        0, 0, 0, 0, new.a, new.b, new.c, new.d);
         END;
    ]], {
        -- <trigger2-7.1>
        
        -- </trigger2-7.1>
    })

test:do_execsql_test(
    "trigger2-7.2",
    [[
        UPDATE abcd SET a = 100, b = 5*5 WHERE a = 1;
        DELETE FROM abcd WHERE a = 1;
        INSERT INTO abcd VALUES(10, 20, 30, 40);
        SELECT * FROM tlog;
    ]], {
        -- <trigger2-7.2>
        1, 1, 2, 3, 4, 100, 25, 3, 4, 2, 1, 2, 3, 4, 100, 25, 3, 4, 3, 1, 2, 3, 4, 0, 0, 0, 0, 4, 1, 2, 3, 4, 0, 0, 0, 0, 5, 0, 0, 0, 0, 10, 20, 30, 40, 6, 0, 0, 0, 0, 10, 20, 30, 40
        -- </trigger2-7.2>
    })

test:do_execsql_test(
    "trigger2-7.3",
    [[
        DELETE FROM tlog;
        INSERT INTO abcd VALUES(10, 20, 30, 40);
        UPDATE abcd SET a = 100, b = 5*5 WHERE a = 1;
        DELETE FROM abcd WHERE a = 1;
        SELECT * FROM tlog;
    ]], {
        -- <trigger2-7.3>
        1, 0, 0, 0, 0, 10, 20, 30, 40, 2, 0, 0, 0, 0, 10, 20, 30, 40, 3, 1, 2, 3, 4, 100, 25, 3, 4, 4, 1, 2, 3, 4, 100, 25, 3, 4, 5, 1, 2, 3, 4, 0, 0, 0, 0, 6, 1, 2, 3, 4, 0, 0, 0, 0
        -- </trigger2-7.3>
    })

test:do_execsql_test(
    "trigger2-7.4",
    [[
        DELETE FROM tlog;
        DELETE FROM abcd WHERE a = 1;
        INSERT INTO abcd VALUES(10, 20, 30, 40);
        UPDATE abcd SET a = 100, b = 5*5 WHERE a = 1;
        SELECT * FROM tlog;
    ]], {
        -- <trigger2-7.4>
        1, 1, 2, 3, 4, 0, 0, 0, 0, 2, 1, 2, 3, 4, 0, 0, 0, 0, 3, 0, 0, 0, 0, 10, 20, 30, 40, 4, 0, 0, 0, 0, 10, 20, 30, 40, 5, 1, 2, 3, 4, 100, 25, 3, 4, 6, 1, 2, 3, 4, 100, 25, 3, 4
        -- </trigger2-7.4>
    })

test:do_execsql_test(
    "trigger2-8.1",
    [[
        CREATE TABLE t1(a  INT PRIMARY KEY,b INT ,c INT );
        INSERT INTO t1 VALUES(1,2,3);
        CREATE VIEW v1 AS
          SELECT a+b AS x, b+c AS y, a+c AS z FROM t1;
        SELECT * FROM v1;
    ]], {
        -- <trigger2-8.1>
        3, 5, 4
        -- </trigger2-8.1>
    })

test:do_execsql_test(
    "trigger2-8.2",
    [[
        CREATE TABLE v1log(id  INT PRIMARY KEY, a INT ,b INT ,c INT ,d INT ,e INT ,f INT );
        CREATE TRIGGER r1 INSTEAD OF DELETE ON v1 BEGIN
          INSERT INTO v1log VALUES(OLD.x, OLD.x,NULL,OLD.y,NULL,OLD.z,NULL);
        END;
        DELETE FROM v1 WHERE x=1;
        SELECT * FROM v1log;
    ]], {
        -- <trigger2-8.2>
        
        -- </trigger2-8.2>
    })

test:do_execsql_test(
    "trigger2-8.3",
    [[
        DELETE FROM v1 WHERE x=3;
        SELECT * FROM v1log;
    ]], {
        -- <trigger2-8.3>
        3, 3, "", 5, "", 4, ""
        -- </trigger2-8.3>
    })

test:do_execsql_test(
    "trigger2-8.4",
    [[
        INSERT INTO t1 VALUES(4,5,6);
        DELETE FROM v1log;
        DELETE FROM v1 WHERE y=11;
        SELECT * FROM v1log;
    ]], {
        -- <trigger2-8.4>
        9, 9, "", 11, "", 10, ""
        -- </trigger2-8.4>
    })

test:do_execsql_test(
    "trigger2-8.5",
    [[
        CREATE TRIGGER r2 INSTEAD OF INSERT ON v1 BEGIN
          INSERT INTO v1log VALUES(NEW.x, NULL,NEW.x,NULL,NEW.y,NULL,NEW.z);
        END;
        DELETE FROM v1log;
        INSERT INTO v1 VALUES(1,2,3);
        SELECT * FROM v1log;
    ]], {
        -- <trigger2-8.5>
        1, "", 1, "", 2, "", 3
        -- </trigger2-8.5>
    })

test:do_execsql_test(
    "trigger2-8.6",
    [[
        CREATE TRIGGER r3 INSTEAD OF UPDATE ON v1 BEGIN
          INSERT INTO v1log VALUES(OLD.x, OLD.x,NEW.x,OLD.y,NEW.y,OLD.z,NEW.z);
        END;
        DELETE FROM v1log;
        UPDATE v1 SET x=x+100, y=y+200, z=z+300;
        SELECT * FROM v1log;
    ]], {
        -- <trigger2-8.6>
        3, 3, 103, 5, 205, 4, 304, 9, 9, 109, 11, 211, 10, 310
        -- </trigger2-8.6>
    })

-- At one point the following was causing a segfault.
test:do_execsql_test(
    "trigger2-9.1",
    [[
        CREATE TABLE t3(a TEXT PRIMARY KEY, b TEXT);
        CREATE VIEW v3 AS SELECT t3.a FROM t3;
        CREATE TRIGGER trig1 INSTEAD OF DELETE ON v3 BEGIN
          SELECT 1;
        END;
        DELETE FROM v3 WHERE a = 1;
    ]], {
        -- <trigger2-9.1>
        
        -- </trigger2-9.1>
    })

--


-- ifcapable view
-- integrity_check trigger2-9.9
-- MUST_WORK_TEST
test:finish_test()
