#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(19)

-- This file implements regression tests for foreign keys.

test:do_execsql_test(
    "fkey1-1.1",
    [[
        CREATE TABLE t1(
            a INTEGER PRIMARY KEY,
            b INTEGER
                REFERENCES t1 ON DELETE CASCADE
                REFERENCES t2,
            c TEXT,
            FOREIGN KEY (b, c) REFERENCES t2(x, y) ON UPDATE CASCADE);
    ]], {
        -- <fkey1-1.1>
        -- </fkey1-1.1>
    })

test:do_execsql_test(
    "fkey1-1.2",
    [[
        CREATE TABLE t2(x PRIMARY KEY, y TEXT);
    ]], {
        -- <fkey1-1.2>
        -- </fkey1-1.2>
    })

test:do_execsql_test(
    "fkey1-1.3",
    [[
        CREATE TABLE t3(
            a INTEGER PRIMARY KEY REFERENCES t2,
            b INTEGER REFERENCES t1,
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
        CREATE TABLE t5(a INTEGER PRIMARY KEY, b, c);
        CREATE TABLE t6(d REFERENCES t5, e PRIMARY KEY REFERENCES t5(c));
        PRAGMA foreign_key_list(t6);
    ]], {
        -- <fkey1-3.1>
        0, 0, 'T5', 'E', 'C', 'NO ACTION', 'NO ACTION', 'NONE',
        1, 0, 'T5', 'D', '', 'NO ACTION', 'NO ACTION', 'NONE'
        -- </fkey1-3.1>
    })

test:do_execsql_test(
    "fkey1-3.2",
    [[
        CREATE TABLE t7(d PRIMARY KEY, e, f, FOREIGN KEY (d, e) REFERENCES t5(a, b));
        PRAGMA foreign_key_list(t7);
    ]], {
        -- <fkey1-3.2>
        0, 0, 'T5', 'D', 'A', 'NO ACTION', 'NO ACTION', 'NONE',
        0, 1, 'T5', 'E', 'B', 'NO ACTION', 'NO ACTION', 'NONE'
        -- </fkey1-3.2>
    })

test:do_execsql_test(
    "fkey1-3.3",
    [[
        CREATE TABLE t8(
            d PRIMARY KEY, e, f,
            FOREIGN KEY (d, e) REFERENCES t5 ON DELETE CASCADE ON UPDATE SET NULL);
        PRAGMA foreign_key_list(t8);
    ]], {
        -- <fkey1-3.3>
        0, 0, 'T5', 'D', '', 'SET NULL', 'CASCADE', 'NONE',
        0, 1, 'T5', 'E', '', 'SET NULL', 'CASCADE', 'NONE'
        -- </fkey1-3.3>
    })

test:do_execsql_test(
    "fkey1-3.4",
    [[
        CREATE TABLE t9(
            d PRIMARY KEY, e, f,
            FOREIGN KEY (d, e) REFERENCES t5 ON DELETE CASCADE ON UPDATE SET DEFAULT);
        PRAGMA foreign_key_list(t9);
    ]], {
        -- <fkey1-3.4>
        0, 0, 'T5', 'D', '', 'SET DEFAULT', 'CASCADE', 'NONE',
        0, 1, 'T5', 'E', '', 'SET DEFAULT', 'CASCADE', 'NONE'
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
            parent REFERENCES t11 ON DELETE CASCADE);
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
        1, "FOREIGN KEY constraint failed"
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
            Id INTEGER PRIMARY KEY,
            ParentId INTEGER REFERENCES Foo(Id) ON DELETE CASCADE,
            C1);
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
        1, "FOREIGN KEY constraint failed"
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

test:do_execsql_test(
    "fkey1-6.1",
    [[
        CREATE TABLE p1(id PRIMARY KEY, x, y);
        CREATE INDEX p1x ON p1(x);
        INSERT INTO p1 VALUES(1, 1, 1);
        CREATE TABLE c1(a PRIMARY KEY REFERENCES p1(x));
    ]], {
        -- <fkey1-6.1>
        -- </fkey1-6.1>
    })

test:do_catchsql_test(
    "fkey1-6.2",
    [[
        INSERT INTO c1 VALUES(1);
    ]], {
        -- <fkey1-6.2>
        1, "foreign key mismatch - \"C1\" referencing \"P1\""
        -- </fkey1-6.2>
    })

test:do_execsql_test(
    "fkey1-6.3",
    [[
        CREATE UNIQUE INDEX p1x2 ON p1(x);
        INSERT INTO c1 VALUES(1);
    ]], {
        -- <fkey1-6.3>
        -- </fkey1-6.3>
    })

test:finish_test()
