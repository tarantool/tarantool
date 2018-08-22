#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(41)

test:do_execsql_test(
    "alter-1.1",
    [[
        CREATE TABLE t1(id PRIMARY KEY, a, b);
        INSERT INTO t1 VALUES(1, 1, 2);
        CREATE TABLE "t1x1"(c UNIQUE, b PRIMARY KEY);
        INSERT INTO "t1x1" VALUES(3, 4);
        CREATE INDEX t1i1 ON T1(B);
        CREATE INDEX t1i2 ON t1(a, b);
        CREATE INDEX i3 ON "t1x1"(b, c);
        CREATE TABLE "Space_Table"(id PRIMARY KEY, e, f, g UNIQUE);
        INSERT INTO "Space_Table" VALUES(1, 5, 6, 7);
        SELECT 't1', * FROM t1;
        SELECT 't1x1', * FROM "t1x1";
        SELECT 'Space_Table', * FROM "Space_Table";
    ]], {
        -- <alter-1.1>
        "Space_Table", 1, 5, 6, 7
        -- </alter-1.1>
    })

test:do_execsql_test(
    "alter-1.2",
    [[
        SELECT count(*) FROM "_space" WHERE "name" IN ('T1', 't1x1', 'Space_Table')
    ]], {
        -- <alter-1.2>
        3
        -- </alter-1.2>
    })

test:do_execsql_test(
    "alter-1.3",
    [[
        ALTER TABLE T1 RENAME to "t1";
        ALTER TABLE "t1x1" RENAME TO T2;
        ALTER TABLE "Space_Table" RENAME to SPACE_TABLE;
    ]], {
        -- <alter-1.3>
        -- </alter-1.3>
    })

test:do_execsql_test(
    "alter-1.4",
    [[
        SELECT 't1', * FROM "t1";
        SELECT 't2', * FROM t2;
        SELECT 'space_table', * FROM SPACE_TABLE;  
    ]], {
        -- <alter-1.4>
        "space_table", 1, 5, 6, 7
        -- </alter-1.4>
    })

test:do_execsql_test(
    "alter-1.5",
    [[
        SELECT count(*) FROM "_space" WHERE "name" IN ('T1', 't1x1', 'Space_Table', 'SPACE_TABLE', 't1', 'T2');
    ]], {
        -- <alter-1.5>
        3
        -- </alter-1.5>
    })

test:do_catchsql_test(
    "alter-2.1",
    [[
        ALTER TABLE none RENAME TO hi;
    ]], {
        -- <alter-2.1>
        1, "Space 'NONE' does not exist"
        -- </alter-2.1>
    })

test:do_catchsql_test(
    "alter-2.2",
    [[
        CREATE TABLE t3(id PRIMARY KEY, p, q, r);
        ALTER TABLE t2 RENAME TO t3;
    ]], {
        -- <alter-2.2>
        1, "Space 'T3' already exists"
        -- </alter-2.2>
    })

test:do_catchsql_test(
    "alter-2.3",
    [[
        ALTER TABLE "_space" RENAME TO space;
    ]], {
        -- <alter-2.3>
        1, "Failed to execute SQL statement: can't modify name of space created not via SQL facilities"
        -- </alter-2.3>
    })

test:do_execsql_test(
    "alter-3.1",
    [[
        CREATE TABLE t6(id PRIMARY KEY, a, b, c);
        CREATE TABLE tab(id PRIMARY KEY);
        CREATE TRIGGER trig1 AFTER INSERT ON T6 BEGIN INSERT INTO tab VALUES(new.id); END;
        INSERT INTO t6 VALUES(1, 1, 2, 3);
        SELECT * FROM tab;

    ]], {
        -- <alter-3.1>
        1
        -- </alter-3.1>
    })

test:do_execsql_test(
    "alter-3.2",
    [[
        ALTER TABLE t6 RENAME TO t7;
        INSERT INTO t7 VALUES(2, 4, 5, 6);
        SELECT * FROM tab;

    ]], {
        -- <alter-3.2>
        1, 2
        -- </alter-3.2>
    })

test:do_execsql_test(
    "alter-3.3",
    [[
            DROP TRIGGER trig1;
            SELECT * FROM "_trigger";
    ]], {
        -- <alter-3.3>
        -- </alter-3.3>
    })

test:do_execsql_test(
    "alter-3.4",
    [[
        CREATE TRIGGER trig2 AFTER INSERT ON t7 BEGIN INSERT INTO tab VALUES(new.id); END;
        INSERT INTO t7 VALUES(3, 1, 2, 3);
        SELECT * FROM tab;
    ]], {
        -- <alter-3.4>
        1, 2, 3
        -- </alter-3.4>
    })

test:do_execsql_test(
    "alter-3.5",
    [[
        ALTER TABLE t7 RENAME TO "t8";
        INSERT INTO "t8" VALUES(4, 4, 5, 6);
        SELECT * FROM tab;
    ]], {
        -- <alter-3.5>
        1, 2, 3, 4
        -- </alter-3.5>
    })

test:do_execsql_test(
    "alter-3.6",
    [[
        DROP TRIGGER trig2;
    ]], {
        -- <alter-3.6>
        -- </alter-3.6>
    })

test:do_execsql_test(
    "alter-3.7",
    [[
        CREATE TRIGGER trig3 AFTER INSERT ON "t8" BEGIN INSERT INTO tab VALUES(new.id); END;
        INSERT INTO "t8" VALUES(5, 1, 2, 3);
        SELECT * FROM tab;
    ]], {
        -- <alter-3.7>
        1, 2, 3, 4, 5
        -- </alter-3.7>
    })

test:do_execsql_test(
    "alter-3.8",
    [[
        ALTER TABLE "t8" RENAME TO t9;
        INSERT INTO t9 VALUES(6, 4, 5, 6);
        SELECT * FROM tab;
    ]], {
        -- <alter-3.8>
        1, 2, 3, 4, 5, 6
        -- </alter-3.8>
    })

test:do_execsql_test(
    "alter-4.1",
    [[
        CREATE TABLE tbl1(a INTEGER PRIMARY KEY AUTOINCREMENT);
        INSERT INTO tbl1 VALUES(10);
        INSERT INTO tbl1 VALUES(NULL);
        SELECT a FROM tbl1;
    ]], {
        -- <alter-4.1>
        10, 11
        -- </alter-4.1>
    })

test:do_execsql_test(
    "alter-4.2",
    [[
        ALTER TABLE tbl1 RENAME TO tbl2;
        DELETE FROM tbl2;
        INSERT INTO tbl2 VALUES(NULL);
        SELECT a FROM tbl2;
    ]], {
        -- <alter-4.2>
        12
        -- </alter-4.2>
    })

test:do_execsql_test(
    "alter-4.3",
    [[
        DROP TABLE tbl2;
    ]], {
        -- <alter-4.3>
        -- </alter-4.3>
    })

test:do_execsql_test(
    "alter-5.1",
    [[
        CREATE TABLE xyz(x PRIMARY KEY);
        ALTER TABLE xyz RENAME TO "xyz1234abc";
        SELECT "name" FROM "_space" WHERE "name" GLOB 'xyz*';
    ]], {
        -- <alter-5.1>
        "xyz1234abc"
        -- </alter-5.1>
    })

test:do_execsql_test(
    "alter-5.2",
    [[
        ALTER TABLE "xyz1234abc" RENAME TO xyzabc;
        SELECT "name" FROM "_space" WHERE "name" GLOB 'XYZ*';
    ]], {
        -- <alter-5.2>
        "XYZABC"
        -- </alter-5.2>
    })

test:do_execsql_test(
    "alter-6.1",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        DROP TRIGGER trig3;
        CREATE TABLE t1(id PRIMARY KEY, b, c);
        INSERT INTO t1 VALUES(1,2,3), (3,2,1);
        CREATE TABLE t2(id PRIMARY KEY);
        CREATE TRIGGER on_t1 AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(new.id + 100); END;
        CREATE TRIGGER on_t2 AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(new.id + 101); END;
        CREATE TRIGGER on_t3 AFTER INSERT ON t1 BEGIN INSERT INTO t2 values(new.id + 102); END;
        ALTER TABLE t1 RENAME TO "a";
        INSERT INTO "a" VALUES(8, 5, 9);
        SELECT * FROM t2;
    ]], {
        -- <alter-6.1>
        108, 109, 110
        -- </alter-6.1>
    })

test:do_execsql_test(
    "alter-6.2",
    [[
        DROP TRIGGER on_t2;
        SELECT "name" FROM "_trigger";
    ]], {
        -- <alter-6.2>
        "ON_T1", "ON_T3"
        -- </alter-6.2>
    })

test:do_execsql_test(
    "alter-6.3",
    [[
        DELETE FROM t2;
        INSERT INTO "a" VALUES(100,5,9);
        SELECT * FROM t2;
    ]], {
        -- <alter-6.3>
        200, 202
        -- </alter-6.3>
    })

test:do_execsql_test(
    "alter-6.4",
    [[
        DROP TABLE "a";
        SELECT * FROM "_trigger";
    ]], {
        -- <alter-6.4>
        -- </alter-6.4>
    })

-- Make sure that foreign keys remain after renaming
-- as for parent table, as for child table.
--
test:do_execsql_test(
    "alter-7.1",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t3;
        CREATE TABLE t2(id PRIMARY KEY);
        CREATE TABLE t3(id PRIMARY KEY);
        CREATE TABLE t1(a PRIMARY KEY, b, c, FOREIGN KEY(b) REFERENCES t2(id), FOREIGN KEY(c) REFERENCES t3(id));
        INSERT INTO t2 VALUES(1);
        INSERT INTO t3 VALUES(2);
        INSERT INTO t1 VALUES(1, 1, 2);
        ALTER TABLE t1 RENAME TO t5;
        SELECT * FROM t5;
    ]], {
        -- <alter-7.1>
        1, 1, 2
        -- </alter-7.1>
    })

test:do_catchsql_test(
    "alter-7.2",
    [[
        INSERT INTO t5 VALUES(2, 1, 3);
    ]], {
        -- <alter-7.2>
        1, "FOREIGN KEY constraint failed"
        -- </alter-7.2>
    })

test:do_catchsql_test(
    "alter-7.3",
    [[
        INSERT INTO t5 VALUES(2, 2, 2);
    ]], {
        -- <alter-7.3>
        1, "FOREIGN KEY constraint failed"
        -- </alter-7.3>
    })

test:do_execsql_test(
    "alter-7.4",
    [[
        INSERT INTO t5 VALUES(2, 1, 2);
        SELECT * FROM t5;
    ]], {
        -- <alter-7.4>
        1, 1, 2, 2, 1, 2
        -- </alter-7.4>
    })

test:do_execsql_test(
    "alter-7.5",
    [[
        INSERT INTO t2 VALUES(2);
        INSERT INTO t3 VALUES(3);
        INSERT INTO t5 VALUES(3, 2, 3);
        SELECT * FROM t5;
    ]], {
        -- <alter-7.5>
        1, 1, 2, 2, 1, 2, 3, 2, 3
        -- </alter-7.5>
    })

test:do_catchsql_test(
    "alter-7.6",
    [[
        SELECT * FROM t1;
    ]], {
        -- <alter-7.6>
        1, "no such table: T1"
        -- </alter-7.6>
    })


test:do_execsql_test(
    "alter-7.7",
    [[
        ALTER TABLE t2 RENAME TO t4;
        SELECT * FROM t4;
    ]], {
        -- <alter-7.7>
        1, 2
        -- </alter-7.7>
    })

test:do_execsql_test(
    "alter-7.8",
    [[
        SELECT * FROM t5;
    ]], {
        -- <alter-7.8>
        1, 1, 2, 2, 1, 2, 3, 2, 3
        -- </alter-7.8>
    })

test:do_catchsql_test(
    "alter-7.9",
    [[
        INSERT INTO t5 VALUES(4, 5, 3);
    ]], {
        -- <alter-7.9>
        1, "FOREIGN KEY constraint failed"
        -- </alter-7.9>
    })

test:do_execsql_test(
    "alter-7.10",
    [[
        INSERT INTO t5 VALUES(4, 1, 2);
        SELECT * FROM t5;
    ]], {
        -- <alter-7.10>
        1, 1, 2, 2, 1, 2, 3, 2, 3, 4, 1, 2
        -- </alter-7.10>
    })

test:do_catchsql_test(
    "alter-7.11",
    [[
        INSERT INTO t4 VALUES(5);
        INSERT INTO t3 VALUES(4);
        INSERT INTO t5 VALUES(5, 5, 4);
    ]], {
        -- <alter-7.11>
        0
        -- </alter-7.11>
    })

test:do_execsql_test(
    "alter-7.12",
    [[
        ALTER TABLE t3 RENAME TO t6;
        SELECT * FROM t6;

    ]], {
        -- <alter-7.12>
        2, 3, 4
        -- </alter-7.12>
    })

test:do_execsql_test(
    "alter-7.13",
    [[
        SELECT * FROM t5;
    ]], {
        -- <alter-7.13>
        1, 1, 2, 2, 1, 2, 3, 2, 3, 4, 1, 2, 5, 5, 4
        -- </alter-7.13>
    })

test:do_catchsql_test(
    "alter-7.14",
    [[
        INSERT INTO t5 VALUES(6, 5, 10);
    ]], {
        -- <alter-7.14>
        1, "FOREIGN KEY constraint failed"
        -- </alter-7.14>
    })

test:do_execsql_test(
    "alter-7.15",
    [[
        INSERT INTO t5 VALUES(6, 1, 2);
        SELECT * FROM t5;
    ]], {
        -- <alter-7.15>
        1, 1, 2, 2, 1, 2, 3, 2, 3, 4, 1, 2, 5, 5, 4, 6, 1, 2
        -- </alter-7.15>
    })

test:do_catchsql_test(
    "alter-7.16",
    [[
        INSERT INTO t4 VALUES(6);
        INSERT INTO t6 VALUES(10);
        INSERT INTO t5 VALUES(7, 6, 10);
    ]], {
        -- <alter-7.11>
        0
        -- </alter-7.11>
    })


-- Commented due to #2953
--
-- test:do_catchsql_test(
--     "alter-7.11",
--     [[
--         DROP TABLE t4;
--     ]], {
--         -- <alter-7.11>
--         1, "FOREIGN KEY constraint failed"
--         -- </alter-7.11>
--     })

test:finish_test()
