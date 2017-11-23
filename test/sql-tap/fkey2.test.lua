#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(121)

-- This file implements regression tests for foreign keys.

test:do_execsql_test(
    "fkey2-1.1",
    [[
        CREATE TABLE t1(a PRIMARY KEY, b);
        CREATE TABLE t2(c PRIMARY KEY REFERENCES t1(a), d);

        CREATE TABLE t3(a PRIMARY KEY, b);
        CREATE TABLE t4(c PRIMARY KEY REFERENCES t3, d);

        CREATE TABLE t7(a, b INTEGER PRIMARY KEY);
        CREATE TABLE t8(c PRIMARY KEY REFERENCES t7, d);

        CREATE TABLE t9(a PRIMARY KEY REFERENCES nosuchtable, b);
        CREATE TABLE t10(a PRIMARY KEY REFERENCES t9(c), b);
    ]], {
        -- <fkey2-1.1>
        -- </fkey2-1.1>
    })

test:do_catchsql_test(
    "fkey2-1.2",
    [[
        INSERT INTO t2 VALUES(1, 3);
    ]], {
        -- <fkey2-1.2>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-1.2>
    })

test:do_catchsql_test(
    "fkey2-1.3",
    [[
        INSERT INTO t1 VALUES(1, 2);
    ]], {
        -- <fkey2-1.3>
        0
        -- </fkey2-1.3>
    })

test:do_catchsql_test(
    "fkey2-1.4",
    [[
        INSERT INTO t2 VALUES(1, 3);
    ]], {
        -- <fkey2-1.4>
        0
        -- </fkey2-1.4>
    })

test:do_catchsql_test(
    "fkey2-1.5",
    [[
        INSERT INTO t2 VALUES(2, 4);
    ]], {
        -- <fkey2-1.5>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-1.5>
    })

test:do_catchsql_test(
    "fkey2-1.6",
    [[
        INSERT INTO t1 VALUES(3, 5);
        INSERT INTO t2 VALUES(3, 4);
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-1.7>
    })

test:do_catchsql_test(
    "fkey2-1.8",
    [[
        INSERT INTO t1 VALUES(6, 7);
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-1.9>
    })

test:do_catchsql_test(
    "fkey2-1.10",
    [[
        DELETE FROM t1 WHERE a = 6;
        UPDATE t1 SET a = 2;
    ]], {
        -- <fkey2-1.10>
        1, "FOREIGN KEY constraint failed"
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
        INSERT INTO t4 values (1,3);
    ]], {
        -- <fkey2-1.12>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-1.12>
    })

test:do_catchsql_test(
    "fkey2-1.13",
    [[
        INSERT INTO t3 values (1,2);
    ]], {
        -- <fkey2-1.13>
        0
        -- </fkey2-1.13>
    })

test:do_catchsql_test(
    "fkey2-1.14",
    [[
        INSERT INTO t4 values (1,3);
    ]], {
        -- <fkey2-1.14>
        0
        -- </fkey2-1.14>
    })

test:do_catchsql_test(
    "fkey2-1.15",
    [[
        INSERT INTO t8 values (1,3);
    ]], {
        -- <fkey2-1.15>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-1.15>
    })

test:do_catchsql_test(
    "fkey2-1.16",
    [[
        INSERT INTO t7 values (2,1);
    ]], {
        -- <fkey2-1.16>
        0
        -- </fkey2-1.16>
    })

test:do_catchsql_test(
    "fkey2-1.17",
    [[
        INSERT INTO t8 values (1,3);
    ]], {
        -- <fkey2-1.17>
        0
        -- </fkey2-1.17>
    })

test:do_catchsql_test(
    "fkey2-1.18",
    [[
        INSERT INTO t8 values (2,4);
    ]], {
        -- <fkey2-1.18>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-1.18>
    })

test:do_catchsql_test(
    "fkey2-1.19",
    [[
        INSERT INTO t8 values (6,4);
    ]], {
        -- <fkey2-1.19>
        1,"FOREIGN KEY constraint failed"
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-1.23>
    })

test:do_catchsql_test(
    "fkey2-1.24",
    [[
        UPDATE t7 SET b = 2;
    ]], {
        -- <fkey2-1.24>
        1, "FOREIGN KEY constraint failed"
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
        INSERT INTO t8 VALUES(666, 'b');
    ]], {
        -- <fkey2-1.26>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-1.26>
    })

test:do_catchsql_test(
    "fkey2-1.27",
    [[
        UPDATE t7 SET b = 5;
    ]], {
        -- <fkey2-1.27>
        1, "FOREIGN KEY constraint failed"
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
        INSERT INTO t9 VALUES(1, 3);
    ]], {
        -- <fkey2-1.29>
        1, "no such table: NOSUCHTABLE"
        -- </fkey2-1.29>
    })

test:do_catchsql_test(
    "fkey2-1.30",
    [[
        INSERT INTO t10 VALUES(1, 3);
    ]], {
        -- <fkey2-1.30>
        1, "foreign key mismatch - \"T10\" referencing \"T9\""
        -- </fkey2-1.30>
    })

test:do_execsql_test(
    "fkey2-2.1",
    [[
        CREATE TABLE i(i INTEGER PRIMARY KEY);
        CREATE TABLE j(j PRIMARY KEY REFERENCES i);
        INSERT INTO i VALUES(35);
        INSERT INTO j VALUES('35.0');
        SELECT j, typeof(j) FROM j;
    ]], {
        -- <fkey2-2.1>
        "35.0", "text"
        -- </fkey2-2.1>
    })

test:do_catchsql_test(
    "fkey2-2.2",
    [[
        DELETE FROM i;
    ]], {
        -- <fkey2-2.2>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-2.2>
    })

test:do_execsql_test(
    "fkey2-3.1",
    [[
        CREATE TABLE ab(a PRIMARY KEY, b);
        CREATE TABLE cd(
            c PRIMARY KEY REFERENCES ab ON UPDATE CASCADE ON DELETE CASCADE,
            d);
        CREATE TABLE ef(
            e PRIMARY KEY REFERENCES cd ON UPDATE CASCADE,
            f, CHECK (e!=5));

        INSERT INTO ab VALUES(1, 'b');
        INSERT INTO cd VALUES(1, 'd');
        INSERT INTO ef VALUES(1, 'e');
    ]], {
    })

test:do_catchsql_test(
    "fkey2-3.2",
    [[
        UPDATE ab SET a = 5;
    ]], {
        -- <fkey2-3.2>
        1, "CHECK constraint failed: EF"
        -- </fkey2-3.2>
    })

test:do_execsql_test(
    "fkey2-3.3",
    [[
        SELECT * FROM ab;
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
        1, "CHECK constraint failed: EF"
        -- </fkey2-3.4>
    })

test:do_execsql_test(
    "fkey2-3.5",
    [[
        SELECT * FROM ef;
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-3.6>
    })


test:do_execsql_test(
    "fkey2-3.7",
    [[
        SELECT * FROM ab;
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
            node PRIMARY KEY,
            parent REFERENCES t1 ON DELETE CASCADE);
        CREATE TABLE t2(node PRIMARY KEY, parent);
        CREATE TRIGGER t2t AFTER DELETE ON t2 BEGIN
            DELETE FROM t2 WHERE parent = old.node;
        END;
        INSERT INTO t1 VALUES(1, NULL);
        INSERT INTO t1 VALUES(2, 1);
        INSERT INTO t1 VALUES(3, 1);
        INSERT INTO t1 VALUES(4, 2);
        INSERT INTO t1 VALUES(5, 2);
        INSERT INTO t1 VALUES(6, 3);
        INSERT INTO t1 VALUES(7, 3);
        INSERT INTO t2 SELECT * FROM t1;
    ]], {
        -- <fkey2-4.1>
        -- </fkey2-4.1>
    })

test:do_execsql_test(
    "fkey2-4.2",
    [[
        PRAGMA recursive_triggers = off;
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
        PRAGMA recursive_triggers = on;
        DROP TABLE t2;
        DROP TABLE t1;
        CREATE TABLE t1(
            node PRIMARY KEY,
            parent REFERENCES t1 ON DELETE CASCADE);
        CREATE TABLE t2(node PRIMARY KEY, parent);
        CREATE TRIGGER t2t AFTER DELETE ON t2 BEGIN
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
        CREATE TABLE t1(a PRIMARY KEY, b);
        CREATE TABLE t2(c INTEGER PRIMARY KEY REFERENCES t1, b);
    ]], {
        -- <fkey2-5.1>
        -- </fkey2-5.1>
    })

test:do_catchsql_test(
    "fkey2-5.2",
    [[
        INSERT INTO t2 VALUES(1, 'A');
    ]], {
        -- <fkey2-5.2>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-5.2>
    })

test:do_execsql_test(
    "fkey2-5.3",
    [[
        INSERT INTO t1 VALUES(1, 2);
        INSERT INTO t1 VALUES(2, 3);
        INSERT INTO t2 VALUES(1, 'A');
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-5.4>
    })

test:do_catchsql_test(
    "fkey2-5.5",
    [[
        DELETE FROM t1 WHERE a = 2;
    ]], {
        -- <fkey2-5.5>
        1, "FOREIGN KEY constraint failed"
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
        1, "FOREIGN KEY constraint failed"
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
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b);
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-6.5>
    })

test:do_execsql_test(
    "fkey2-6.6",
    [[
        CREATE TABLE pp(a, b, c, PRIMARY KEY(b, c));
        CREATE TABLE cc(d DEFAULT 3, e DEFAULT 1, f DEFAULT 2, id PRIMARY KEY,
            FOREIGN KEY(f, d) REFERENCES pp
            ON UPDATE SET DEFAULT
            ON DELETE SET NULL);
        INSERT INTO pp VALUES(1, 2, 3);
        INSERT INTO pp VALUES(4, 5, 6);
        INSERT INTO pp VALUES(7, 8, 9);
        INSERT INTO cc VALUES(6, 'A', 5, 1);
        INSERT INTO cc VALUES(6, 'B', 5, 2);
        INSERT INTO cc VALUES(9, 'A', 8, 3);
        INSERT INTO cc VALUES(9, 'B', 8, 4);
        UPDATE pp SET b = 1 WHERE a = 7;
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
        CREATE TABLE t3(x PRIMARY KEY REFERENCES t3 ON DELETE SET NULL);
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
        CREATE TABLE p(a PRIMARY KEY, b);
        CREATE TABLE c(x PRIMARY KEY REFERENCES p(c));
        INSERT INTO c DEFAULT VALUES;
    ]], {
        -- <fkey2-7.1>
        1, "foreign key mismatch - \"C\" referencing \"P\""
        -- </fkey2-7.1>
    })

test:do_catchsql_test(
    "fkey2-7.2",
    [[
        DROP TABLE IF EXISTS c;
        DROP TABLE IF EXISTS p;
        CREATE TABLE c(x PRIMARY KEY REFERENCES v(y));
        CREATE VIEW v AS SELECT x AS y FROM c;
        INSERT INTO c DEFAULT VALUES;
    ]], {
        -- <fkey2-7.2>
        1, "foreign key mismatch - \"C\" referencing \"V\""
        -- </fkey2-7.2>
    })

test:do_catchsql_test(
    "fkey2-7.3",
    [[
        DROP TABLE IF EXISTS c;
        CREATE TABLE p(a COLLATE binary, b PRIMARY KEY);
        CREATE UNIQUE INDEX idx ON p(a COLLATE "unicode_ci");
        CREATE TABLE c(x PRIMARY KEY REFERENCES p(a));
        INSERT INTO c DEFAULT VALUES;
    ]], {
        -- <fkey2-7.3>
        1, "foreign key mismatch - \"C\" referencing \"P\""
        -- </fkey2-7.3>
    })

test:do_catchsql_test(
    "fkey2-7.4",
    [[
        DROP TABLE IF EXISTS c;
        DROP TABLE IF EXISTS p;
        CREATE TABLE p(a, b, PRIMARY KEY(a, b));
        CREATE TABLE c(x PRIMARY KEY REFERENCES p);
        INSERT INTO c DEFAULT VALUES;
    ]], {
        -- <fkey2-7.4>
        1, "foreign key mismatch - \"C\" referencing \"P\""
        -- </fkey2-7.4>
    })

--------------------------------------------------------------------------
-- The following tests test CASCADE actions.
---
test:do_execsql_test(
    "fkey2-8.1",
    [[
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b);
        CREATE TABLE t2(c PRIMARY KEY, d, FOREIGN KEY(c) REFERENCES t1(a) ON UPDATE CASCADE);

        INSERT INTO t1 VALUES(10, 100);
        INSERT INTO t2 VALUES(10, 100);
        UPDATE t1 SET a = 15;
        SELECT * FROM t2;
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
        CREATE TABLE t1(a, b PRIMARY KEY);
        CREATE TABLE t2(
            x PRIMARY KEY REFERENCES t1
                ON UPDATE RESTRICT DEFERRABLE INITIALLY DEFERRED);
        INSERT INTO t1 VALUES(1, 'one');
        INSERT INTO t1 VALUES(2, 'two');
        INSERT INTO t1 VALUES(3, 'three');
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-9.2>
    })

test:do_catchsql_test(
    "fkey2-9.3",
    [[
        DELETE FROM t1 WHERE b = 'two';
    ]], {
        -- <fkey2-9.3>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-9.3>
    })

test:do_catchsql_test(
    "fkey2-9.4",
    [[
        INSERT INTO t2 VALUES('five');
    ]], {
        -- <fkey2-9.4>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-9.4>
    })

test:do_execsql_test(
    "fkey2-9.5",
    [[
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(x COLLATE "unicode_ci" PRIMARY KEY);
        CREATE TRIGGER tt1 AFTER DELETE ON t1
            WHEN EXISTS ( SELECT 1 FROM t2 WHERE old.x = y )
        BEGIN
            INSERT INTO t1 VALUES(old.x);
        END;
        CREATE TABLE t2(y PRIMARY KEY REFERENCES t1);
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
        CREATE TABLE t2(y PRIMARY KEY REFERENCES t1 ON DELETE RESTRICT);
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
        1, "FOREIGN KEY constraint failed"
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
            c00, c01, c02, c03, c04, c05, c06, c07, c08, c09,
            c10, c11, c12, c13, c14, c15, c16, c17, c18, c19,
            c20, c21, c22, c23, c24, c25, c26, c27, c28, c29,
            c30, c31, c32, c33, c34, c35, c36, c37, c38, c39,
            PRIMARY KEY(c34, c35));
        CREATE TABLE down(
            c00, c01, c02, c03, c04, c05, c06, c07, c08, c09,
            c10, c11, c12, c13, c14, c15, c16, c17, c18, c19,
            c20, c21, c22, c23, c24, c25, c26, c27, c28, c29,
            c30, c31, c32, c33, c34, c35, c36, c37, c38, c39,
            PRIMARY KEY(c39, c38),
            FOREIGN KEY(c39, c38) REFERENCES up ON UPDATE CASCADE);
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
        1, "FOREIGN KEY constraint failed"
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
        1, "FOREIGN KEY constraint failed"
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
--         CREATE TABLE t1(a PRIMARY KEY, b REFERENCES t1);
--         CREATE TABLE t2(a PRIMARY KEY, b REFERENCES t1, c REFERENCES t2);
--         CREATE TABLE t3(a PRIMARY KEY REFERENCES t1, b REFERENCES t2, c REFERENCES t1);
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
--         1, "FOREIGN KEY constraint failed"
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
--         1, "FOREIGN KEY constraint failed"
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

test:do_execsql_test(
    "fkey2-10.6",
    [[
        DROP TABLE IF EXISTS t2;
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a PRIMARY KEY, b REFERENCES nosuchtable);
        DROP TABLE t1;
    ]], {
        -- <fkey2-10.6>
        -- </fkey2-10.6>
    })

test:do_execsql_test(
    "fkey2-10.7",
    [[
        CREATE TABLE t1(a PRIMARY KEY, b);
        INSERT INTO t1 VALUES('a', 1);
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t2(x PRIMARY KEY REFERENCES t1);
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-10.8>
    })

test:do_execsql_test(
    "fkey2-10.9",
    [[
        DELETE FROM t2;
        DROP TABLE t1;
    ]], {
        -- <fkey2-10.9>
        -- </fkey2-10.9>
    })

test:do_catchsql_test(
    "fkey2-10.10",
    [[
        INSERT INTO t2 VALUES('x');
    ]], {
        -- <fkey2-10.10>
        1, "no such table: T1"
        -- </fkey2-10.10>
    })

test:do_execsql_test(
    "fkey2-10.11",
    [[
        CREATE TABLE t1(x PRIMARY KEY);
        INSERT INTO t1 VALUES('x');
        INSERT INTO t2 VALUES('x');
    ]], {
        -- <fkey2-10.11>
        -- </fkey2-10.11>
    })

test:do_catchsql_test(
    "fkey2-10.12",
    [[
        DROP TABLE t1;
    ]], {
        -- <fkey2-10.12>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-10.12>
    })

test:do_execsql_test(
    "fkey2-10.13",
    [[
        DROP TABLE t2;
        DROP TABLE t1;
    ]], {
        -- <fkey2-10.13>
        -- </fkey2-10.13>
    })

test:do_execsql_test(
    "fkey2-10.14",
    [[
        DROP TABLE IF EXISTS cc;
        DROP TABLE IF EXISTS pp;
        CREATE TABLE pp(x, y, PRIMARY KEY(x, y));
        CREATE TABLE cc(a PRIMARY KEY, b, FOREIGN KEY(a, b) REFERENCES pp(x, z));
    ]], {
        -- <fkey2-10.14>
        -- </fkey2-10.14>
    })

test:do_catchsql_test(
    "fkey2-10.15",
    [[
        INSERT INTO cc VALUES(1, 2);
    ]], {
        -- <fkey2-10.15>
        1, "foreign key mismatch - \"CC\" referencing \"PP\""
        -- </fkey2-10.15>
    })

test:do_execsql_test(
    "fkey2-10.16",
    [[
        DROP TABLE cc;
        CREATE TABLE cc(
            a PRIMARY KEY, b,
            FOREIGN KEY(a, b) REFERENCES pp DEFERRABLE INITIALLY DEFERRED);

        INSERT INTO pp VALUES('a', 'b');
        INSERT INTO cc VALUES('a', 'b');
        DROP TABLE cc;
        DROP TABLE pp;
        CREATE TABLE pp(a, b, c, PRIMARY KEY(b, c));
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

test:do_execsql_test(
    "fkey2-10.18",
    [[
        CREATE TABLE b1(a PRIMARY KEY, b);
        CREATE TABLE b2(a PRIMARY KEY, b REFERENCES b1);
        DROP TABLE b1;
    ]], {
        -- <fkey2-10.18>
        -- </fkey2-10.18>
    })

test:do_execsql_test(
    "fkey2-10.19",
    [[
        CREATE TABLE b3(a PRIMARY KEY, b REFERENCES b2 DEFERRABLE INITIALLY DEFERRED);
        DROP TABLE b2;
    ]], {
        -- <fkey2-10.19>
        -- </fkey2-10.19>
    })

test:do_execsql_test(
    "fkey2-10.20",
    [[
        DROP VIEW IF EXISTS v;
        CREATE TABLE t1(x PRIMARY KEY REFERENCES v);
        CREATE VIEW v AS SELECT * FROM t1;
        DROP VIEW v;
    ]], {
        -- <fkey2-10.20>
        -- </fkey2-10.20>
    })


--------------------------------------------------------------------------
-- This next block of tests, fkey2-16.*, test that rows that refer to
-- themselves may be inserted and deleted.
---
test:do_execsql_test(
    "fkey2-11.1",
    [[
        CREATE TABLE self(a INTEGER PRIMARY KEY, b REFERENCES self(a));
        INSERT INTO self VALUES(13, 13);
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-11.2>
    })

test:do_catchsql_test(
    "fkey2-11.3",
    [[
        UPDATE self SET a = 15;
    ]], {
        -- <fkey2-11.3>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-11.3>
    })

test:do_catchsql_test(
    "fkey2-11.4",
    [[
        UPDATE self SET a = 15, b = 16;
    ]], {
        -- <fkey2-11.4>
        1, "FOREIGN KEY constraint failed"
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
        INSERT INTO self VALUES(20, 21);
    ]], {
        -- <fkey2-11.7>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-11.7>
    })

test:do_execsql_test(
    "fkey2-11.8",
    [[
        DROP TABLE IF EXISTS self;
        CREATE TABLE self(a UNIQUE, b INTEGER PRIMARY KEY REFERENCES self(a));
        INSERT INTO self VALUES(13, 13);
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-11.9>
    })

test:do_catchsql_test(
    "fkey2-11.10",
    [[
        UPDATE self SET a = 15;
    ]], {
        -- <fkey2-11.10>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-11.10>
    })

test:do_catchsql_test(
    "fkey2-11.11",
    [[
        UPDATE self SET a = 15, b = 16;
    ]], {
        -- <fkey2-11.11>
        1, "FOREIGN KEY constraint failed"
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
        INSERT INTO self VALUES(20, 21);
    ]], {
        -- <fkey2-11.14>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-11.14>
    })

--------------------------------------------------------------------------
-- Complex tests
---
test:do_execsql_test(
    "fkey2-12.1",
    [[
        CREATE TABLE tdd08(a INTEGER PRIMARY KEY, b);
        CREATE UNIQUE INDEX idd08 ON tdd08(a,b);
        INSERT INTO tdd08 VALUES(200,300);

        CREATE TABLE tdd08_b(w PRIMARY KEY,x,y, FOREIGN KEY(x,y) REFERENCES tdd08(a,b));
        INSERT INTO tdd08_b VALUES(100,200,300);
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-12.2>
    })

test:do_execsql_test(
    "fkey2-12.3",
    [[
        SELECT * FROM tdd08;
    ]], {
        -- <fkey2-12.3>
        200, 300
        -- </fkey2-12.3>
    })

test:do_catchsql_test(
    "fkey2-12.4",
    [[
        INSERT INTO tdd08_b VALUES(400,500,300);
    ]], {
        -- <fkey2-12.4>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-12.4>
    })

test:do_catchsql_test(
    "fkey2-12.5",
    [[
        UPDATE tdd08_b SET x=x+1;
    ]], {
        -- <fkey2-12.5>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-12.5>
    })

test:do_catchsql_test(
    "fkey2-12.6",
    [[
        UPDATE tdd08 SET a=a+1;
    ]], {
        -- <fkey2-12.6>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-12.6>
    })

test:do_execsql_test(
    "fkey2-13.1",
    [[
        CREATE TABLE tce71(a INTEGER PRIMARY KEY, b);
        CREATE UNIQUE INDEX ice71 ON tce71(a,b);
        INSERT INTO tce71 VALUES(100,200);
        CREATE TABLE tce72(w PRIMARY KEY, x, y, FOREIGN KEY(x,y) REFERENCES tce71(a,b));
        INSERT INTO tce72 VALUES(300,100,200);
        UPDATE tce71 set b = 200 where a = 100;
        SELECT * FROM tce71, tce72;
    ]], {
        -- <fkey2-13.1>
        100, 200, 300, 100, 200
        -- </fkey2-13.1>
    })

test:do_catchsql_test(
    "fkey2-13.2",
    [[
        UPDATE tce71 set b = 201 where a = 100;
    ]], {
        -- <fkey2-13.2>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-13.2>
    })

test:do_catchsql_test(
    "fkey2-13.3",
    [[
        UPDATE tce71 set a = 101 where a = 100;
    ]], {
        -- <fkey2-13.3>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-13.3>
    })

test:do_execsql_test(
    "fkey2-14.1",
    [[
        CREATE TABLE tce73(a INTEGER PRIMARY KEY, b, UNIQUE(a,b));
        INSERT INTO tce73 VALUES(100,200);
        CREATE TABLE tce74(w INTEGER PRIMARY KEY, x, y, FOREIGN KEY(x,y) REFERENCES tce73(a,b));
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
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-14.2>
    })

test:do_catchsql_test(
    "fkey2-14.3",
    [[
        UPDATE tce71 set a = 101 where a = 100;
    ]], {
        -- <fkey2-14.3>
        1, "FOREIGN KEY constraint failed"
        -- </fkey2-14.3>
    })

test:finish_test()
