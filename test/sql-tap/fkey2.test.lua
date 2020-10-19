#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(116)

-- This file implements regression tests for foreign keys.

test:do_execsql_test(
    "fkey2-1.1",
    [[
        CREATE TABLE t1(a INT UNIQUE, b INT, id INT PRIMARY KEY AUTOINCREMENT);
        CREATE TABLE t2(c INT UNIQUE REFERENCES t1(a), d INT, id INT PRIMARY KEY AUTOINCREMENT);

        CREATE TABLE t3(a INT UNIQUE, b INT, id INT PRIMARY KEY AUTOINCREMENT);
        CREATE TABLE t4(c INT UNIQUE REFERENCES t3(a), d INT, id INT PRIMARY KEY AUTOINCREMENT);

        CREATE TABLE t7(a INT, b INTEGER UNIQUE, id INT PRIMARY KEY AUTOINCREMENT);
        CREATE TABLE t8(c INT UNIQUE REFERENCES t7(b), d INT, id INT PRIMARY KEY AUTOINCREMENT);
    ]], {
        -- <fkey2-1.1>
        -- </fkey2-1.1>
    })

test:do_catchsql_test(
    "fkey2-1.2",
    [[
        INSERT INTO t2(c,d) VALUES(1, 3);
    ]], {
        -- <fkey2-1.2>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.2>
    })

test:do_catchsql_test(
    "fkey2-1.3",
    [[
        INSERT INTO t1(a, b) VALUES(1, 2);
    ]], {
        -- <fkey2-1.3>
        0
        -- </fkey2-1.3>
    })

test:do_catchsql_test(
    "fkey2-1.4",
    [[
        INSERT INTO t2(c,d) VALUES(1, 3);
    ]], {
        -- <fkey2-1.4>
        0
        -- </fkey2-1.4>
    })

test:do_catchsql_test(
    "fkey2-1.5",
    [[
        INSERT INTO t2(c,d) VALUES(2, 4);
    ]], {
        -- <fkey2-1.5>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.5>
    })

test:do_catchsql_test(
    "fkey2-1.6",
    [[
        INSERT INTO t1(a,b) VALUES(3, 5);
        INSERT INTO t2(c,d) VALUES(3, 4);
    ]], {
        -- <fkey2-1.6>
        0
        -- </fkey2-1.6>
    })

test:do_catchsql_test(
    "fkey2-1.7",
    [[
        UPDATE t2 SET c = 2 WHERE d = 4;
    ]], {
        -- <fkey2-1.7>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.7>
    })

test:do_catchsql_test(
    "fkey2-1.8",
    [[
        INSERT INTO t1(a,b) VALUES(6, 7);
        UPDATE t2 SET c = 6 WHERE d = 4;
    ]], {
        -- <fkey2-1.8>
        0
        -- </fkey2-1.8>
    })

test:do_catchsql_test(
    "fkey2-1.9",
    [[
        DELETE FROM t1 WHERE a = 1;
    ]], {
        -- <fkey2-1.9>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.9>
    })

test:do_catchsql_test(
    "fkey2-1.10",
    [[
        DELETE FROM t1 WHERE a = 6;
        UPDATE t1 SET a = 2;
    ]], {
        -- <fkey2-1.10>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.10>
    })

test:do_catchsql_test(
    "fkey2-1.11",
    [[
        DELETE FROM t2 WHERE c = 2;
        DELETE FROM t2 WHERE c = 6;
        UPDATE t2 SET c = 1;
    ]], {
        -- <fkey2-1.11>
        0
        -- </fkey2-1.11>
    })

test:do_catchsql_test(
    "fkey2-1.12",
    [[
        INSERT INTO t4(c,d) values (1,3);
    ]], {
        -- <fkey2-1.12>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.12>
    })

test:do_catchsql_test(
    "fkey2-1.13",
    [[
        INSERT INTO t3(a,b) values (1,2);
    ]], {
        -- <fkey2-1.13>
        0
        -- </fkey2-1.13>
    })

test:do_catchsql_test(
    "fkey2-1.14",
    [[
        INSERT INTO t4(c,d) values (1,3);
    ]], {
        -- <fkey2-1.14>
        0
        -- </fkey2-1.14>
    })

test:do_catchsql_test(
    "fkey2-1.15",
    [[
        INSERT INTO t8(c,d) values (1,3);
    ]], {
        -- <fkey2-1.15>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.15>
    })

test:do_catchsql_test(
    "fkey2-1.16",
    [[
        INSERT INTO t7(a,b) values (2,1);
    ]], {
        -- <fkey2-1.16>
        0
        -- </fkey2-1.16>
    })

test:do_catchsql_test(
    "fkey2-1.17",
    [[
        INSERT INTO t8(c,d) values (1,3);
    ]], {
        -- <fkey2-1.17>
        0
        -- </fkey2-1.17>
    })

test:do_catchsql_test(
    "fkey2-1.18",
    [[
        INSERT INTO t8(c,d) values (2,4);
    ]], {
        -- <fkey2-1.18>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.18>
    })

test:do_catchsql_test(
    "fkey2-1.19",
    [[
        INSERT INTO t8(c,d) values (6,4);
    ]], {
        -- <fkey2-1.19>
        1,"Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.19>
    })

test:do_catchsql_test(
    "fkey2-1.20",
    [[
        UPDATE t8 SET c = 2 WHERE d = 4;
    ]], {
        -- <fkey2-1.20>
        0
        -- </fkey2-1.20>
    })

test:do_catchsql_test(
    "fkey2-1.21",
    [[
        UPDATE t8 SET c = 1 WHERE d = 4;
    ]], {
        -- <fkey2-1.21>
        0
        -- </fkey2-1.21>
    })

test:do_catchsql_test(
    "fkey2-1.22",
    [[
        UPDATE t8 SET c = 7 WHERE d = 4;
    ]], {
        -- <fkey2-1.22>
        0
        -- </fkey2-1.22>
    })

test:do_catchsql_test(
    "fkey2-1.23",
    [[
        DELETE FROM t7 WHERE b = 1;
    ]], {
        -- <fkey2-1.23>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.23>
    })

test:do_catchsql_test(
    "fkey2-1.24",
    [[
        UPDATE t7 SET b = 2;
    ]], {
        -- <fkey2-1.24>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.24>
    })

test:do_catchsql_test(
    "fkey2-1.25",
    [[
        UPDATE t7 SET b = 1;
    ]], {
        -- <fkey2-1.25>
        0
        -- </fkey2-1.25>
    })

test:do_catchsql_test(
    "fkey2-1.26",
    [[
        INSERT INTO t8(c,d) VALUES(666, 54644);
    ]], {
        -- <fkey2-1.26>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.26>
    })

test:do_catchsql_test(
    "fkey2-1.27",
    [[
        UPDATE t7 SET b = 5;
    ]], {
        -- <fkey2-1.27>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-1.27>
    })

test:do_catchsql_test(
    "fkey2-1.28",
    [[
        UPDATE t7 SET a = 10;
    ]], {
        -- <fkey2-1.28>
        0
        -- </fkey2-1.28>
    })

test:do_catchsql_test(
    "fkey2-1.29",
    [[
        CREATE TABLE t9(a INT PRIMARY KEY REFERENCES nosuchtable, b);
    ]], {
        1, "Space 'NOSUCHTABLE' does not exist"
    })

test:do_catchsql_test(
    "fkey2-1.30",
    [[
        INSERT INTO t9 VALUES(1, 3);
    ]], {
        -- <fkey2-1.29>
        1, "Space 'T9' does not exist"
        -- </fkey2-1.29>
    })

test:do_execsql_test(
    "fkey2-2.1",
    [[
        CREATE TABLE i(i INTEGER PRIMARY KEY);
        CREATE TABLE j(j INT PRIMARY KEY REFERENCES i);
        INSERT INTO i VALUES(35);
        INSERT INTO j VALUES(35);
        SELECT j FROM j;
    ]], {
        -- <fkey2-2.1>
        35
        -- </fkey2-2.1>
    })

test:do_catchsql_test(
    "fkey2-2.2",
    [[
        DELETE FROM i;
    ]], {
        -- <fkey2-2.2>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-2.2>
    })

test:do_execsql_test(
    "fkey2-3.1",
    [[
        CREATE TABLE ab(id INT PRIMARY KEY AUTOINCREMENT, a INT UNIQUE, b TEXT);
        CREATE TABLE cd(
            id INT PRIMARY KEY AUTOINCREMENT,
            c  INT UNIQUE REFERENCES ab(a) ON UPDATE CASCADE ON DELETE CASCADE,
            d TEXT);
        CREATE TABLE ef(
            id INT PRIMARY KEY AUTOINCREMENT,
            e  INT UNIQUE REFERENCES cd(c) ON UPDATE CASCADE,
            f TEXT , CHECK (e!=5));

        INSERT INTO ab(a,b) VALUES(1, 'b');
        INSERT INTO cd(c,d) VALUES(1, 'd');
        INSERT INTO ef(e,f) VALUES(1, 'e');
    ]], {
    })

test:do_catchsql_test(
    "fkey2-3.2",
    [[
        UPDATE ab SET a = 5;
    ]], {
        -- <fkey2-3.2>
        1, "Check constraint failed 'ck_unnamed_EF_1': e!=5"
        -- </fkey2-3.2>
    })

test:do_execsql_test(
    "fkey2-3.3",
    [[
        SELECT a,b FROM ab;
    ]], {
        -- <fkey2-3.3>
        1, "b"
        -- </fkey2-3.3>
    })

test:do_catchsql_test(
    "fkey2-3.4",
    [[
        UPDATE ab SET a = 5;
    ]], {
        -- <fkey2-3.4>
        1, "Check constraint failed 'ck_unnamed_EF_1': e!=5"
        -- </fkey2-3.4>
    })

test:do_execsql_test(
    "fkey2-3.5",
    [[
        SELECT e,f FROM ef;
    ]], {
        -- <fkey2-3.5>
        1, "e"
        -- </fkey2-3.5>
    })


test:do_catchsql_test(
    "fkey2-3.6",
    [[
        DELETE FROM ab;
    ]], {
        -- <fkey2-3.6>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-3.6>
    })


test:do_execsql_test(
    "fkey2-3.7",
    [[
        SELECT a,b FROM ab;
    ]], {
        -- <fkey2-3.7>
        1, "b"
        -- </fkey2-3.7>
    })


test:do_execsql_test(
    "fkey2-4.1",
    [[
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(
            id INT PRIMARY KEY AUTOINCREMENT,
            node  INT UNIQUE NOT NULL,
            parent  INT REFERENCES t1(node) ON DELETE CASCADE);
        CREATE TABLE t2(node  INT PRIMARY KEY, parent INT );
        CREATE TRIGGER t2t AFTER DELETE ON t2
        FOR EACH ROW
        BEGIN
            DELETE FROM t2 WHERE parent = old.node;
        END;
        INSERT INTO t1(node, parent) VALUES(1, NULL);
        INSERT INTO t1(node, parent) VALUES(2, 1);
        INSERT INTO t1(node, parent) VALUES(3, 1);
        INSERT INTO t1(node, parent) VALUES(4, 2);
        INSERT INTO t1(node, parent) VALUES(5, 2);
        INSERT INTO t1(node, parent) VALUES(6, 3);
        INSERT INTO t1(node, parent) VALUES(7, 3);
        INSERT INTO t2(node, parent) SELECT node,parent FROM t1;
    ]], {
        -- <fkey2-4.1>
        -- </fkey2-4.1>
    })

test:do_execsql_test(
    "fkey2-4.2",
    [[
        UPDATE "_session_settings" SET "value" = false WHERE "name" = 'sql_recursive_triggers';
        DELETE FROM t1 WHERE node = 1;
        SELECT node FROM t1;
    ]], {
        -- <fkey2-4.2>
        -- </fkey2-4.2>
    })


test:do_execsql_test(
    "fkey2-4.3",
    [[
	   DELETE FROM t2 WHERE node = 1;
	   SELECT node FROM t2;
    ]], {
        -- <fkey2-4.3>
        4, 5, 6, 7
        -- </fkey2-4.3>
    })

test:do_execsql_test(
    "fkey2-4.4",
    [[
        UPDATE "_session_settings" SET "value" = true WHERE "name" = 'sql_recursive_triggers';
        DROP TABLE t2;
        DROP TABLE t1;
        CREATE TABLE t1(
            node  INT PRIMARY KEY,
            parent  INT REFERENCES t1 ON DELETE CASCADE);
        CREATE TABLE t2(node  INT PRIMARY KEY, parent INT );
        CREATE TRIGGER t2t AFTER DELETE ON t2
        FOR EACH ROW
        BEGIN
            DELETE FROM t2 WHERE parent = old.node;
        END;
        INSERT INTO t1 VALUES(1, 1);
        INSERT INTO t1 VALUES(2, 1);
        INSERT INTO t1 VALUES(3, 1);
        INSERT INTO t1 VALUES(4, 2);
        INSERT INTO t1 VALUES(5, 2);
        INSERT INTO t1 VALUES(6, 3);
        INSERT INTO t1 VALUES(7, 3);
        INSERT INTO t2 SELECT * FROM t1;
        DELETE FROM t1 WHERE node = 1;
        SELECT node FROM t1;
        DELETE FROM t1 WHERE node = 1;
        SELECT node FROM t1;
    ]], {
        -- <fkey2-4.4>
        -- </fkey2-4.4>
    })

test:do_execsql_test(
    "fkey2-4.5",
    [[
       DELETE FROM t2 WHERE node = 1;
       SELECT node FROM t2;
    ]], {
        -- <fkey2-4.5>
        -- </fkey2-4.5>
    })

test:do_execsql_test(
    "fkey2-4.5",
    [[
       DELETE FROM t2 WHERE node = 1;
       SELECT node FROM t2;
    ]], {
        -- <fkey2-4.5>
        -- </fkey2-4.5>
    })

--------------------------------------------------------------------------
-- Test that it is possible to use an INTEGER PRIMARY KEY as the child key
-- of a foreign constraint.
---
test:do_execsql_test(
    "fkey2-5.1",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t1(a INT UNIQUE, b INT, id INT PRIMARY KEY AUTOINCREMENT);
        CREATE TABLE t2(id INT PRIMARY KEY AUTOINCREMENT, c INTEGER REFERENCES t1(a), b TEXT);
    ]], {
        -- <fkey2-5.1>
        -- </fkey2-5.1>
    })

test:do_catchsql_test(
    "fkey2-5.2",
    [[
        INSERT INTO t2(c,b) VALUES(1, 'A');
    ]], {
        -- <fkey2-5.2>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-5.2>
    })

test:do_execsql_test(
    "fkey2-5.3",
    [[
        INSERT INTO t1(a,b) VALUES(1, 2);
        INSERT INTO t1(a,b) VALUES(2, 3);
        INSERT INTO t2(c,b) VALUES(1, 'A');
        UPDATE t2 SET c = 2;
    ]], {
        -- <fkey2-5.3>
        -- </fkey2-5.3>
    })

test:do_catchsql_test(
    "fkey2-5.4",
    [[
        UPDATE t2 SET c = 3;
    ]], {
        -- <fkey2-5.4>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-5.4>
    })

test:do_catchsql_test(
    "fkey2-5.5",
    [[
        DELETE FROM t1 WHERE a = 2;
    ]], {
        -- <fkey2-5.5>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-5.5>
    })

test:do_execsql_test(
    "fkey2-5.6",
    [[
        DELETE FROM t1 WHERE a = 1;
    ]], {
        -- <fkey2-5.6>
        -- </fkey2-5.6>
    })

test:do_catchsql_test(
    "fkey2-5.7",
    [[
        UPDATE t1 SET a = 3;
    ]], {
        -- <fkey2-5.7>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-5.7>
    })

--------------------------------------------------------------------------
-- The following suite tests SET DEFAULT actions.
---
test:do_execsql_test(
    "fkey2-6.1",
    [[
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b TEXT);
        CREATE TABLE t2(
            c INTEGER PRIMARY KEY,
            d INTEGER DEFAULT 1 REFERENCES t1 ON DELETE SET DEFAULT);
        DELETE FROM t1;
    ]], {
        -- <fkey2-6.1>
        -- </fkey2-6.1>
    })

test:do_execsql_test(
    "fkey2-6.2",
    [[
        INSERT INTO t1 VALUES(1, 'one');
        INSERT INTO t1 VALUES(2, 'two');
        INSERT INTO t2 VALUES(1, 2);
        DELETE FROM t1 WHERE a = 2;
        SELECT * FROM t2;
    ]], {
        -- <fkey2-6.2>
        1, 1
        -- </fkey2-6.2>
    })

test:do_execsql_test(
    "fkey2-6.3",
    [[
        INSERT INTO t1 VALUES(2, 'two');
        UPDATE t2 SET d = 2;
        DELETE FROM t1 WHERE a = 1;
        SELECT * FROM t2;
    ]], {
        -- <fkey2-6.3>
        1, 2
        -- </fkey2-6.3>
    })

test:do_execsql_test(
    "fkey2-6.4",
    [[
        SELECT * FROM t1;
    ]], {
        -- <fkey2-6.4>
        2, "two"
        -- </fkey2-6.4>
    })

test:do_catchsql_test(
    "fkey2-6.5",
    [[
        DELETE FROM t1;
    ]], {
        -- <fkey2-6.5>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-6.5>
    })

test:do_execsql_test(
    "fkey2-6.6",
    [[
        CREATE TABLE pp(a INT , b INT , c INT , PRIMARY KEY(b, c));
        CREATE TABLE cc(d  INT DEFAULT 3, e TEXT DEFAULT '1', f  INT DEFAULT 2, id  INT PRIMARY KEY,
            FOREIGN KEY(f, d) REFERENCES pp
            ON UPDATE SET DEFAULT
            ON DELETE SET NULL);
        INSERT INTO pp VALUES(1, 2, 3);
        INSERT INTO pp VALUES(4, 5, 6);
        INSERT INTO pp VALUES(7, 1, 9);
        INSERT INTO cc VALUES(6, 'A', 5, 1);
        INSERT INTO cc VALUES(6, 'B', 5, 2);
        INSERT INTO cc VALUES(3, 'A', 2, 3);
        INSERT INTO cc VALUES(3, 'B', 2, 4);
        SELECT * FROM cc;
    ]], {
        -- <fkey2-6.6>
        6, "A", 5, 1, 6, "B", 5, 2, 3, "A", 2, 3, 3, "B", 2, 4
        -- </fkey2-6.6>
    })

test:do_execsql_test(
    "fkey2-6.7",
    [[
        DELETE FROM pp WHERE a = 4;
        SELECT * FROM cc;
    ]], {
        -- <fkey2-6.7>
        "", "A", "", 1, "", "B", "", 2, 3, "A", 2, 3, 3, "B", 2, 4
        -- </fkey2-6.7>
    })

test:do_execsql_test(
    "fkey2-6.8",
    [[
        DROP TABLE IF EXISTS t4;
        DROP TABLE IF EXISTS t3;
        CREATE TABLE t3(x  INT PRIMARY KEY REFERENCES t3 ON DELETE SET NULL);
        INSERT INTO t3(x) VALUES(12345);
        DROP TABLE t3;
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t1;
    ]], {
        -- <fkey2-6.8>
        -- </fkey2-6.8>
    })

--------------------------------------------------------------------------
-- The following tests test "foreign key mismatch" and other errors.
---
test:do_catchsql_test(
    "fkey2-7.1",
    [[
        CREATE TABLE p(a INT PRIMARY KEY, b INT);
        CREATE TABLE c(x INT PRIMARY KEY REFERENCES p(c));
    ]], {
        -- <fkey2-7.1>
        1, "Failed to create foreign key constraint 'fk_unnamed_C_1': foreign key refers to nonexistent field C"
        -- </fkey2-7.1>
    })

test:do_catchsql_test(
    "fkey2-7.2",
    [[
        CREATE VIEW v AS SELECT b AS y FROM p;
        CREATE TABLE c(x  INT PRIMARY KEY REFERENCES v(y));
    ]], {
        -- <fkey2-7.2>
        1, "Failed to create foreign key constraint 'fk_unnamed_C_1': referenced space can't be VIEW"
        -- </fkey2-7.2>
    })

test:do_catchsql_test(
    "fkey2-7.3",
    [[
        DROP VIEW v;
        DROP TABLE IF EXISTS c;
        DROP TABLE IF EXISTS p;
        CREATE TABLE p(a TEXT COLLATE "unicode_ci", b INT PRIMARY KEY);
        CREATE UNIQUE INDEX idx ON p(a);
        CREATE TABLE c(x TEXT PRIMARY KEY REFERENCES p(a));
    ]], {
        -- <fkey2-7.3>
        1, "Failed to create foreign key constraint 'fk_unnamed_C_1': field collation mismatch"
        -- </fkey2-7.3>
    })

test:do_catchsql_test(
    "fkey2-7.4",
    [[
        DROP TABLE IF EXISTS c;
        DROP TABLE IF EXISTS p;
        CREATE TABLE p(a INT, b INT, PRIMARY KEY(a, b));
        CREATE TABLE c(x INT PRIMARY KEY REFERENCES p);
    ]], {
        -- <fkey2-7.4>
        1, "Failed to create foreign key constraint 'fk_unnamed_C_1': number of columns in foreign key does not match the number of columns in the primary index of referenced table"
        -- </fkey2-7.4>
    })

--------------------------------------------------------------------------
-- The following tests test CASCADE actions.
---
test:do_execsql_test(
    "fkey2-8.1",
    [[
        CREATE TABLE t1(a INT UNIQUE, b INT, id INT PRIMARY KEY AUTOINCREMENT);
        CREATE TABLE t2(id INT PRIMARY KEY AUTOINCREMENT, c INT UNIQUE, d INT, FOREIGN KEY(c) REFERENCES t1(a) ON UPDATE CASCADE);

        INSERT INTO t1(a,b) VALUES(10, 100);
        INSERT INTO t2(c,d) VALUES(10, 100);
        UPDATE t1 SET a = 15;
        SELECT c,d FROM t2;
    ]], {
        -- <fkey2-8.1>
        15, 100
        -- </fkey2-8.1>
    })

--------------------------------------------------------------------------
-- The following tests, fkey2-12.*, test RESTRICT actions.
---

test:do_execsql_test(
    "fkey2-9.1",
    [[
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a INT, b TEXT UNIQUE, id INT PRIMARY KEY AUTOINCREMENT);
        CREATE TABLE t2(
            x TEXT PRIMARY KEY REFERENCES t1(b) ON UPDATE RESTRICT);
        INSERT INTO t1(a,b) VALUES(1, 'four');
        INSERT INTO t1(a,b) VALUES(2, 'two');
        INSERT INTO t1(a,b) VALUES(3, 'three');
        INSERT INTO t2 VALUES('two');
        UPDATE t1 SET b = 'four' WHERE b = 'one';
    ]], {
        -- <fkey2-9.1>
        -- </fkey2-9.1>
    })

test:do_catchsql_test(
    "fkey2-9.2",
    [[
      UPDATE t1 SET b = 'five' WHERE b = 'two';
    ]], {
        -- <fkey2-9.2>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-9.2>
    })

test:do_catchsql_test(
    "fkey2-9.3",
    [[
        DELETE FROM t1 WHERE b = 'two';
    ]], {
        -- <fkey2-9.3>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-9.3>
    })

test:do_catchsql_test(
    "fkey2-9.4",
    [[
        INSERT INTO t2 VALUES('five');
    ]], {
        -- <fkey2-9.4>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-9.4>
    })

test:do_execsql_test(
    "fkey2-9.5",
    [[
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(x  TEXT COLLATE "unicode_ci" PRIMARY KEY);
        CREATE TRIGGER tt1 AFTER DELETE ON t1
        FOR EACH ROW
            WHEN EXISTS ( SELECT 1 FROM t2 WHERE old.x = y )
        BEGIN
            INSERT INTO t1 VALUES(old.x);
        END;
        CREATE TABLE t2(y TEXT COLLATE "unicode_ci" PRIMARY KEY REFERENCES t1);
        INSERT INTO t1 VALUES('A');
        INSERT INTO t1 VALUES('B');
        INSERT INTO t2 VALUES('A');
        INSERT INTO t2 VALUES('B');
        DELETE FROM t1;
        SELECT * FROM t1;

    ]], {
        -- <fkey2-9.5>
        "A", "B"
        -- </fkey2-9.5>
    })

test:do_execsql_test(
    "fkey2-9.6",
    [[
        SELECT * FROM t2;
    ]], {
        -- <fkey2-9.6>
        "A", "B"
        -- </fkey2-9.6>
    })

test:do_execsql_test(
    "fkey2-9.7",
    [[
        DROP TABLE t2;
        CREATE TABLE t2(y TEXT COLLATE "unicode_ci" PRIMARY KEY REFERENCES t1 ON DELETE RESTRICT);
        INSERT INTO t2 VALUES('A');
        INSERT INTO t2 VALUES('B');
    ]], {
        -- <fkey2-9.7>
        -- </fkey2-9.7>
    })

test:do_catchsql_test(
    "fkey2-9.8",
    [[
        DELETE FROM t1;
    ]], {
        -- <fkey2-9.8>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-9.8>
    })

test:do_execsql_test(
    "fkey2-9.9",
    [[
        SELECT * FROM t1;
    ]], {
        -- <fkey2-9.9>
        "A", "B"
        -- </fkey2-9.9>
    })

test:do_execsql_test(
    "fkey2-9.10",
    [[
        SELECT * FROM t2;
    ]], {
        -- <fkey2-9.10>
        "A", "B"
        -- </fkey2-9.10>
    })

test:do_execsql_test(
    "fkey2-9.11",
    [[
        CREATE TABLE up(
            c00 TEXT , c01 TEXT , c02 TEXT , c03 TEXT , c04 TEXT , c05 TEXT , c06 TEXT , c07 TEXT , c08 TEXT , c09 TEXT ,
            c10 TEXT , c11 TEXT , c12 TEXT , c13 TEXT , c14 TEXT , c15 TEXT , c16 TEXT , c17 TEXT , c18 TEXT , c19 TEXT ,
            c20 TEXT , c21 TEXT , c22 TEXT , c23 TEXT , c24 TEXT , c25 TEXT , c26 TEXT , c27 TEXT , c28 TEXT , c29 TEXT ,
            c30 TEXT , c31 TEXT , c32 TEXT , c33 TEXT , c34 TEXT , c35 TEXT , c36 TEXT , c37 TEXT , c38 TEXT , c39 TEXT ,
            UNIQUE(c34, c35), id INT PRIMARY KEY AUTOINCREMENT);
        CREATE TABLE down(
            c00 TEXT , c01 TEXT , c02 TEXT , c03 TEXT , c04 TEXT , c05 TEXT , c06 TEXT , c07 TEXT , c08 TEXT , c09 TEXT ,
            c10 TEXT , c11 TEXT , c12 TEXT , c13 TEXT , c14 TEXT , c15 TEXT , c16 TEXT , c17 TEXT , c18 TEXT , c19 TEXT ,
            c20 TEXT , c21 TEXT , c22 TEXT , c23 TEXT , c24 TEXT , c25 TEXT , c26 TEXT , c27 TEXT , c28 TEXT , c29 TEXT ,
            c30 TEXT , c31 TEXT , c32 TEXT , c33 TEXT , c34 TEXT , c35 TEXT , c36 TEXT , c37 TEXT , c38 TEXT , c39 TEXT ,
            UNIQUE(c39, c38),
            FOREIGN KEY(c39, c38) REFERENCES up(c34,c35) ON UPDATE CASCADE, id INT PRIMARY KEY AUTOINCREMENT);
        INSERT INTO up(c34, c35) VALUES('yes', 'no');
        INSERT INTO down(c39, c38) VALUES('yes', 'no');
        UPDATE up SET c34 = 'possibly';
        SELECT c38, c39 FROM down;
    ]], {
        -- <fkey2-9.11>
        "no", "possibly"
        -- </fkey2-9.11>
    })

test:do_catchsql_test(
    "fkey2-9.12",
    [[
        DELETE FROM down;
        INSERT INTO down(c39, c38) VALUES('yes', 'no');
    ]], {
        -- <fkey2-9.12>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-9.12>
    })

test:do_catchsql_test(
    "fkey2-9.13",
    [[
        INSERT INTO up(c34, c35) VALUES('yes', 'no');
        INSERT INTO down(c39, c38) VALUES('yes', 'no');
        DELETE FROM up WHERE c34 = 'yes';
    ]], {
        -- <fkey2-9.13>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-9.13>
    })

test:do_execsql_test(
    "fkey2-9.14",
    [[
        DELETE FROM up WHERE c34 = 'possibly';
        SELECT c34, c35 FROM up;
    ]], {
        -- <fkey2-9.14>
        "yes", "no"
        -- </fkey2-9.14>
    })

test:do_execsql_test(
    "fkey2-9.15",
    [[
        SELECT c39, c38 FROM down;
    ]], {
        -- <fkey2-9.15>
        "yes", "no"
        -- </fkey2-9.15>
    })

--------------------------------------------------------------------------
-- The following tests test that the "DROP TABLE" and "ALTER
-- TABLE" commands work as expected with foreign key constraints.
---

-- Commented till #2204 isn't pushed to master.
-- test:do_execsql_test(
--     "fkey2-10.1",
--     [[
--         CREATE TABLE t1(a  INT PRIMARY KEY, b  INT REFERENCES t1);
--         CREATE TABLE t2(a  INT PRIMARY KEY, b  INT REFERENCES t1, c  INT REFERENCES t2);
--         CREATE TABLE t3(a  INT PRIMARY KEY REFERENCES t1, b  INT REFERENCES t2, c  INT REFERENCES t1);
--         INSERT INTO t1 VALUES(1, 1);
--         ALTER TABLE t1 RENAME TO t4;
--         SELECT * FROM t4;
--     ]], {
--         -- <fkey2-10.1>
--         1, 1
--         -- </fkey2-10.1>
--     })

-- test:do_catchsql_test(
--     "fkey2-10.2",
--     [[
--         DELETE * FROM t4;
--         INSERT INTO t3 VALUES(1, 2, 3);
--     ]], {
--         -- <fkey2-10.2>
--         1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
--         -- </fkey2-10.2>
--     })

-- test:do_execsql_test(
--     "fkey2-10.3",
--     [[
--         INSERT INTO t4 VALUES(1, NULL);
--     ]], {
--         -- <fkey2-10.3>
--         1, 1
--         -- </fkey2-10.3>
--     })

-- test:do_catchsql_test(
--     "fkey2-10.4",
--     [[
--         UPDATE t4 SET b = 5;
--     ]], {
--         -- <fkey2-10.4>
--         1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
--         -- </fkey2-10.4>
--     })

-- test:do_execsql_test(
--     "fkey2-10.5",
--     [[
--         UPDATE t4 SET b = 1;
--         INSERT INTO t3 VALUES(1, NULL, 1);
--     ]], {
--         -- <fkey2-10.5>
--         -- </fkey2-10.5>
--     })

test:do_catchsql_test(
    "fkey2-10.6",
    [[
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a INT PRIMARY KEY, b INT REFERENCES nosuchtable);
    ]], {
        -- <fkey2-10.6>
        1, "Space 'NOSUCHTABLE' does not exist"
        -- </fkey2-10.6>
    })

test:do_execsql_test(
    "fkey2-10.7",
    [[
        CREATE TABLE t1(a TEXT PRIMARY KEY, b INT );
        INSERT INTO t1 VALUES('a', 1);
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t2(x TEXT PRIMARY KEY REFERENCES t1);
        INSERT INTO t2 VALUES('a');
    ]], {
        -- <fkey2-10.7>
        -- </fkey2-10.7>
    })

test:do_catchsql_test(
    "fkey2-10.8",
    [[
        DROP TABLE t1;
    ]], {
        -- <fkey2-10.8>
        1, "Can't drop space 'T1': other objects depend on it"
        -- </fkey2-10.8>
    })

test:do_execsql_test(
    "fkey2-10.9",
    [[
        DROP TABLE t2;
        DROP TABLE t1;
    ]], {
        -- <fkey2-10.9>
        -- </fkey2-10.9>
    })

test:do_catchsql_test(
    "fkey2-10.14",
    [[
        DROP TABLE IF EXISTS cc;
        DROP TABLE IF EXISTS pp;
        CREATE TABLE pp(x TEXT, y TEXT, PRIMARY KEY(x, y));
        CREATE TABLE cc(a  INT PRIMARY KEY, b INT , FOREIGN KEY(a, b) REFERENCES pp(x, z));
    ]], {
        -- <fkey2-10.14>
        1, "Failed to create foreign key constraint 'fk_unnamed_CC_1': foreign key refers to nonexistent field Z"
        -- </fkey2-10.14>
    })

test:do_execsql_test(
    "fkey2-10.16",
    [[
        CREATE TABLE cc(
            a  TEXT PRIMARY KEY, b TEXT,
            FOREIGN KEY(a, b) REFERENCES pp DEFERRABLE INITIALLY DEFERRED);

        INSERT INTO pp VALUES('a', 'b');
        INSERT INTO cc VALUES('a', 'b');
        DROP TABLE cc;
        DROP TABLE pp;
        CREATE TABLE pp(a INT , b TEXT, c TEXT, PRIMARY KEY(b, c));
        INSERT INTO pp VALUES(1, 'a', 'b');
    ]], {
        -- <fkey2-10.16>
        -- </fkey2-10.16>
    })

test:do_execsql_test(
    "fkey2-10.17",
    [[
        DROP TABLE pp;
    ]], {
        -- <fkey2-10.17>
        -- </fkey2-10.17>
    })

test:do_catchsql_test(
    "fkey2-10.18",
    [[
        CREATE TABLE b1(a  INT PRIMARY KEY, b INT );
        CREATE TABLE b2(a  INT PRIMARY KEY, b  INT REFERENCES b1);
        DROP TABLE b1;
    ]], {
        -- <fkey2-10.18>
        1, "Can't drop space 'B1': other objects depend on it"
        -- </fkey2-10.18>
    })

test:do_catchsql_test(
    "fkey2-10.19",
    [[
        CREATE TABLE b3(a  INT PRIMARY KEY, b  INT REFERENCES b2 DEFERRABLE INITIALLY DEFERRED);
        DROP TABLE b2;
    ]], {
        -- <fkey2-10.19>
        1, "Can't drop space 'B2': other objects depend on it"
        -- </fkey2-10.19>
    })

test:do_catchsql_test(
    "fkey2-10.20",
    [[
        DROP VIEW IF EXISTS v;
        CREATE VIEW v AS SELECT * FROM b1;
        CREATE TABLE t1(x INT PRIMARY KEY REFERENCES v);
    ]], {
        -- <fkey2-10.20>
        1, "Failed to create foreign key constraint 'fk_unnamed_T1_1': referenced space can't be VIEW"
        -- </fkey2-10.20>
    })


--------------------------------------------------------------------------
-- This next block of tests, fkey2-16.*, test that rows that refer to
-- themselves may be inserted and deleted.
---
test:do_execsql_test(
    "fkey2-11.1",
    [[
        CREATE TABLE self(a INT UNIQUE, b INT REFERENCES self(a), id INT PRIMARY KEY AUTOINCREMENT);
        INSERT INTO self(a,b) VALUES(13, 13);
        UPDATE self SET a = 14, b = 14;
    ]], {
        -- <fkey2-11.1>
        -- </fkey2-11.1>
    })

test:do_catchsql_test(
    "fkey2-11.2",
    [[
        UPDATE self SET b = 15;
    ]], {
        -- <fkey2-11.2>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-11.2>
    })

test:do_catchsql_test(
    "fkey2-11.3",
    [[
        UPDATE self SET a = 15;
    ]], {
        -- <fkey2-11.3>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-11.3>
    })

test:do_catchsql_test(
    "fkey2-11.4",
    [[
        UPDATE self SET a = 15, b = 16;
    ]], {
        -- <fkey2-11.4>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-11.4>
    })

test:do_execsql_test(
    "fkey2-11.5",
    [[
        UPDATE self SET a = 17, b = 17;
    ]], {
        -- <fkey2-11.5>
        -- </fkey2-11.5>
    })

test:do_execsql_test(
    "fkey2-11.6",
    [[
        DELETE FROM self;
    ]], {
        -- <fkey2-11.6>
        -- </fkey2-11.6>
    })

test:do_catchsql_test(
    "fkey2-11.7",
    [[
        INSERT INTO self(a,b) VALUES(20, 21);
    ]], {
        -- <fkey2-11.7>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-11.7>
    })

test:do_execsql_test(
    "fkey2-11.8",
    [[
        DROP TABLE IF EXISTS self;
        CREATE TABLE self(a INT UNIQUE, b INT UNIQUE REFERENCES self(a), id INT PRIMARY KEY AUTOINCREMENT);
        INSERT INTO self(a,b) VALUES(13, 13);
        UPDATE self SET a = 14, b = 14;
    ]], {
        -- <fkey2-11.8>
        -- </fkey2-11.8>
    })

test:do_catchsql_test(
    "fkey2-11.9",
    [[
        UPDATE self SET b = 15;
    ]], {
        -- <fkey2-11.9>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-11.9>
    })

test:do_catchsql_test(
    "fkey2-11.10",
    [[
        UPDATE self SET a = 15;
    ]], {
        -- <fkey2-11.10>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-11.10>
    })

test:do_catchsql_test(
    "fkey2-11.11",
    [[
        UPDATE self SET a = 15, b = 16;
    ]], {
        -- <fkey2-11.11>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-11.11>
    })

test:do_execsql_test(
    "fkey2-11.12",
    [[
        UPDATE self SET a = 17, b = 17;
    ]], {
        -- <fkey2-11.12>
        -- </fkey2-11.12>
    })

test:do_execsql_test(
    "fkey2-11.13",
    [[
        DELETE FROM self;
    ]], {
        -- <fkey2-11.13>
        -- </fkey2-11.13>
    })

test:do_catchsql_test(
    "fkey2-11.14",
    [[
        INSERT INTO self(a,b) VALUES(20, 21);
    ]], {
        -- <fkey2-11.14>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-11.14>
    })

--------------------------------------------------------------------------
-- Complex tests
---
test:do_execsql_test(
    "fkey2-12.1",
    [[
        CREATE TABLE tdd08(a INT UNIQUE, b INT, id INT PRIMARY KEY AUTOINCREMENT);
        CREATE UNIQUE INDEX idd08 ON tdd08(a,b);
        INSERT INTO tdd08(a,b) VALUES(200,300);

        CREATE TABLE tdd08_b(w INT UNIQUE, x INT ,y INT , FOREIGN KEY(x,y) REFERENCES tdd08(a,b), id INT PRIMARY KEY AUTOINCREMENT);
        INSERT INTO tdd08_b(w,x,y) VALUES(100,200,300);
    ]], {
        -- <fkey2-12.1>
        -- </fkey2-12.1>
    })

test:do_catchsql_test(
    "fkey2-12.2",
    [[
        DELETE FROM tdd08;
    ]], {
        -- <fkey2-12.2>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-12.2>
    })

test:do_execsql_test(
    "fkey2-12.3",
    [[
        SELECT a,b FROM tdd08;
    ]], {
        -- <fkey2-12.3>
        200, 300
        -- </fkey2-12.3>
    })

test:do_catchsql_test(
    "fkey2-12.4",
    [[
        INSERT INTO tdd08_b(w,x,y) VALUES(400,500,300);
    ]], {
        -- <fkey2-12.4>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-12.4>
    })

test:do_catchsql_test(
    "fkey2-12.5",
    [[
        UPDATE tdd08_b SET x=x+1;
    ]], {
        -- <fkey2-12.5>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-12.5>
    })

test:do_catchsql_test(
    "fkey2-12.6",
    [[
        UPDATE tdd08 SET a=a+1;
    ]], {
        -- <fkey2-12.6>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-12.6>
    })

test:do_execsql_test(
    "fkey2-13.1",
    [[
        CREATE TABLE tce71(a INT UNIQUE, b INT, id INT PRIMARY KEY AUTOINCREMENT);
        CREATE UNIQUE INDEX ice71 ON tce71(a,b);
        INSERT INTO tce71(a,b) VALUES(100,200);
        CREATE TABLE tce72(w  INT PRIMARY KEY, x INT , y INT , FOREIGN KEY(x,y) REFERENCES tce71(a,b));
        INSERT INTO tce72 VALUES(300,100,200);
        UPDATE tce71 set b = 200 where a = 100;
        SELECT * FROM tce71, tce72;
    ]], {
        -- <fkey2-13.1>
        100, 200, 1, 300, 100, 200
        -- </fkey2-13.1>
    })

test:do_catchsql_test(
    "fkey2-13.2",
    [[
        UPDATE tce71 set b = 201 where a = 100;
    ]], {
        -- <fkey2-13.2>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-13.2>
    })

test:do_catchsql_test(
    "fkey2-13.3",
    [[
        UPDATE tce71 set a = 101 where a = 100;
    ]], {
        -- <fkey2-13.3>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-13.3>
    })

test:do_execsql_test(
    "fkey2-14.1",
    [[
        CREATE TABLE tce73(a INTEGER PRIMARY KEY, b INT , UNIQUE(a,b));
        INSERT INTO tce73 VALUES(100,200);
        CREATE TABLE tce74(w INTEGER PRIMARY KEY, x INT , y INT , FOREIGN KEY(x,y) REFERENCES tce73(a,b));
        INSERT INTO tce74 VALUES(300,100,200);
        UPDATE tce73 set b = 200 where a = 100;
        SELECT * FROM tce73, tce74;
    ]], {
        -- <fkey2-14.1>
        100, 200, 300, 100, 200
        -- </fkey2-14.1>
    })

test:do_catchsql_test(
    "fkey2-14.2",
    [[
        UPDATE tce73 set b = 201 where a = 100;
    ]], {
        -- <fkey2-14.2>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-14.2>
    })

test:do_catchsql_test(
    "fkey2-14.3",
    [[
        UPDATE tce71 set a = 101 where a = 100;
    ]], {
        -- <fkey2-14.3>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey2-14.3>
    })

test:finish_test()
