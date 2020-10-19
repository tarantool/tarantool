#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(25)

-- This file implements regression tests for foreign keys.

test:do_execsql_test(
    "fkey1-1.1",
    [[
        CREATE TABLE t2(x INT PRIMARY KEY, y TEXT, UNIQUE (x, y));
    ]], {
        -- <fkey1-1.1>
        -- </fkey1-1.1>
    })

test:do_execsql_test(
    "fkey1-1.2",
    [[
        CREATE TABLE t1(
            a INT PRIMARY KEY,
            b INTEGER
                REFERENCES t1 ON DELETE CASCADE
                REFERENCES t2 (x),
            c TEXT UNIQUE,
            FOREIGN KEY (b, c) REFERENCES t2(x, y) ON UPDATE CASCADE);
    ]], {
        -- <fkey1-1.1>
        -- </fkey1-1.1>
    })

test:do_execsql_test(
    "fkey1-1.3",
    [[
        CREATE TABLE t3(
            a INT PRIMARY KEY REFERENCES t2,
            b TEXT REFERENCES t1(c),
            FOREIGN KEY (a, b) REFERENCES t2(x, y));
    ]], {
        -- <fkey1-1.3>
        -- </fkey1-1.3>
    })

test:do_execsql_test(
    "fkey1-2.1",
    [[
        CREATE TABLE t4(a INTEGER PRIMARY KEY);
        CREATE TABLE t5(x INTEGER PRIMARY KEY REFERENCES t4);
        CREATE TABLE t6(x INTEGER PRIMARY KEY REFERENCES t4);
        CREATE TABLE t7(x INTEGER PRIMARY KEY REFERENCES t4);
        CREATE TABLE t8(x INTEGER PRIMARY KEY REFERENCES t4);
        CREATE TABLE t9(x INTEGER PRIMARY KEY REFERENCES t4);
        CREATE TABLE t10(x INTEGER PRIMARY KEY REFERENCES t4);
        DROP TABLE t7;
        DROP TABLE t9;
        DROP TABLE t5;
        DROP TABLE t8;
        DROP TABLE t6;
        DROP TABLE t10;
    ]], {
        -- <fkey1-2.1>
        -- </fkey1-1.1>
    })

test:do_execsql_test(
    "fkey1-3.1",
    [[
        CREATE TABLE t5(a INTEGER PRIMARY KEY, b INT , c INT UNIQUE, UNIQUE(a, b) );
        CREATE TABLE t6(d  INT REFERENCES t5, e  INT PRIMARY KEY REFERENCES t5(c));
        PRAGMA foreign_key_list(t6);
    ]], {
        -- <fkey1-3.1>
        0, 0, 'T5', 'D', 'A', 'no_action', 'no_action', 'NONE',
        1, 0, 'T5', 'E', 'C', 'no_action', 'no_action', 'NONE'
        -- </fkey1-3.1>
    })

test:do_execsql_test(
    "fkey1-3.2",
    [[
        CREATE TABLE t7(d  INT PRIMARY KEY, e INT , f INT , FOREIGN KEY (d, e) REFERENCES t5(a, b));
        PRAGMA foreign_key_list(t7);
    ]], {
        -- <fkey1-3.2>
        0, 0, 'T5', 'D', 'A', 'no_action', 'no_action', 'NONE',
        0, 1, 'T5', 'E', 'B', 'no_action', 'no_action', 'NONE'
        -- </fkey1-3.2>
    })

test:do_execsql_test(
    "fkey1-3.3",
    [[
        CREATE TABLE t8(
            d  INT PRIMARY KEY, e INT , f INT ,
            FOREIGN KEY (d, e) REFERENCES t5(a, b) ON DELETE CASCADE ON UPDATE SET NULL);
        PRAGMA foreign_key_list(t8);
    ]], {
        -- <fkey1-3.3>
        0, 0, 'T5', 'D', 'A', 'cascade', 'set_null', 'NONE',
        0, 1, 'T5', 'E', 'B', 'cascade', 'set_null', 'NONE'
        -- </fkey1-3.3>
    })

test:do_execsql_test(
    "fkey1-3.4",
    [[
        CREATE TABLE t9(
            d  INT PRIMARY KEY, e INT , f INT ,
            FOREIGN KEY (d, e) REFERENCES t5(a, b) ON DELETE CASCADE ON UPDATE SET DEFAULT);
        PRAGMA foreign_key_list(t9);
    ]], {
        -- <fkey1-3.4>
        0, 0, 'T5', 'D', 'A', 'cascade', 'set_default', 'NONE',
        0, 1, 'T5', 'E', 'B', 'cascade', 'set_default', 'NONE'
        -- </fkey1-3.4>
    })

test:do_execsql_test(
    "fkey1-4.1",
    [[
        CREATE TABLE "xx1"("xx2" TEXT PRIMARY KEY, "xx3" TEXT);
        INSERT INTO "xx1"("xx2","xx3") VALUES('abc','def');
        CREATE TABLE "xx4"("xx5" TEXT PRIMARY KEY REFERENCES "xx1" ON DELETE CASCADE);
        INSERT INTO "xx4"("xx5") VALUES('abc');
        INSERT INTO "xx1"("xx2","xx3") VALUES('uvw','xyz');
        SELECT 1, "xx5" FROM "xx4";

    ]], {
        -- <fkey1-4.1>
        1, 'abc'
        -- </fkey1-4.1>
    })

test:do_execsql_test(
    "fkey1-4.2",
    [[
        DELETE FROM "xx1";
        SELECT 2, "xx5" FROM "xx4";
    ]], {
        -- <fkey1-4.2>
        -- </fkey1-4.2>
    })

test:do_execsql_test(
    "fkey1-5.1",
    [[
        CREATE TABLE t11(
            x INTEGER PRIMARY KEY,
            parent  INT REFERENCES t11 ON DELETE CASCADE);
        INSERT INTO t11 VALUES(1, NULL), (2, 1), (3, 2);
    ]], {
        -- <fkey1-5.1>
        -- </fkey1-5.1>
    })

test:do_catchsql_test(
    "fkey1-5.2",
    [[
        INSERT OR REPLACE INTO t11 VALUES (2, 3);
    ]], {
        -- <fkey1-5.2>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey1-5.2>
    })

test:do_execsql_test(
    "fkey1-5.3",
    [[
        SELECT * FROM t11;
    ]], {
        -- <fkey1-5.3>
        1, "", 2, 1, 3, 2
        -- </fkey1-5.3>
    })

test:do_execsql_test(
    "fkey1-5.4",
    [[
        CREATE TABLE Foo (
            Id INT PRIMARY KEY,
            ParentId INTEGER REFERENCES Foo(Id) ON DELETE CASCADE,
            C1 TEXT);
        INSERT OR REPLACE INTO Foo(Id, ParentId, C1) VALUES (1, null, 'A');
        INSERT OR REPLACE INTO Foo(Id, ParentId, C1) VALUES (2, 1, 'A-2-1');
        INSERT OR REPLACE INTO Foo(Id, ParentId, C1) VALUES (3, 2, 'A-3-2');
        INSERT OR REPLACE INTO Foo(Id, ParentId, C1) VALUES (4, 3, 'A-4-3');
    ]], {
        -- <fkey1-5.4>
        -- </fkey1-5.4>
    })

test:do_catchsql_test(
    "fkey1-5.5",
    [[
        INSERT OR REPLACE INTO Foo(Id, ParentId, C1) VALUES (2, 3, 'A-2-3');
    ]], {
        -- <fkey1-5.5>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey1-5.5>
    })

test:do_execsql_test(
    "fkey1-5.6",
    [[
        SELECT * FROM Foo;
    ]], {
        -- <fkey1-5.6>
        1, "", 'A', 2, 1, 'A-2-1', 3, 2, 'A-3-2', 4, 3, 'A-4-3'
        -- </fkey1-5.6>
    })

test:do_catchsql_test(
    "fkey1-6.1",
    [[
        CREATE TABLE p1(id INT PRIMARY KEY, x INT, y INT);
        CREATE INDEX p1x ON p1(x);
        INSERT INTO p1 VALUES(1, 1, 1);
        CREATE TABLE c1(a INT PRIMARY KEY REFERENCES p1(x));
    ]], {
        -- <fkey1-6.1>
        1, "Failed to create foreign key constraint 'fk_unnamed_C1_1': referenced fields don't compose unique index"
        -- </fkey1-6.1>
    })

test:do_execsql_test(
    "fkey1-6.3",
    [[
        CREATE UNIQUE INDEX p1x2 ON p1(x);
        DROP TABLE IF EXISTS c1;
        CREATE TABLE c1(a INT PRIMARY KEY REFERENCES p1(x));
        INSERT INTO c1 VALUES(1);
    ]], {
        -- <fkey1-6.3>
        -- </fkey1-6.3>
    })

-- gh-3645: update col=Null do not activates ON UPDATE trigger.

test:do_select_tests(
    "fkey1-7",
    {
        {"0",
            [[
                CREATE TABLE T12 (A INTEGER PRIMARY KEY,
                    B VARCHAR(5) UNIQUE);
                CREATE TABLE T13 (A INTEGER PRIMARY KEY,
                    B VARCHAR(5) UNIQUE,
                    FOREIGN KEY (B) REFERENCES T12 (B) ON UPDATE SET NULL);
                INSERT INTO T12 VALUES (1,'a');
                INSERT INTO T13 VALUES (1,'a');
            ]], {}},
        {"1", "UPDATE T12 SET B = NULL", {}},
        {"2", "SELECT * FROM T12", {1, ""}},
        {"3", "SELECT * FROM T13", {1, ""}},
        {"4", "UPDATE T12 SET B = 'a'", {}},
        {"5", "SELECT * FROM T12", {1, "a"}},
        {"6", "SELECT * FROM T13", {1, ""}},
    })

test:finish_test()
