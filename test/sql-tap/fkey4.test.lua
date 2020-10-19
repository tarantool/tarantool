#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(17)

-- This file implements regression tests for foreign keys.

test:do_execsql_test(
    "fkey8-1.1",
    [[
        CREATE TABLE p1(a INT PRIMARY KEY);
        CREATE TABLE c1(id INT PRIMARY KEY AUTOINCREMENT, b INT UNIQUE REFERENCES p1 ON DELETE CASCADE);
        INSERT INTO p1 VALUES (1), (2), (3);
        INSERT INTO c1(b) VALUES (2);
        DELETE FROM p1 WHERE a = 2;
        SELECT * FROM c1;
    ]], {
        -- <fkey8-1.1>
        -- </fkey8-1.1>
    })

test:do_catchsql_test(
    "fkey8-1.2",
    [[
        DROP TABLE IF EXISTS c1;
        DROP TABLE IF EXISTS p1;
        CREATE TABLE p1(a INT PRIMARY KEY);
        CREATE TABLE c1(id INT PRIMARY KEY AUTOINCREMENT, b INT UNIQUE NOT NULL REFERENCES p1 ON DELETE SET NULL);
        INSERT INTO p1 VALUES (1), (2), (3);
        INSERT INTO c1(b) VALUES (2);
        DELETE FROM p1 WHERE a = 2;
    ]], {
        -- <fkey8-1.2>
        1, "Failed to execute SQL statement: NOT NULL constraint failed: C1.B"
        -- </fkey8-1.2>
    })

test:do_execsql_test(
    "fkey8-1.3",
    [[
        DROP TABLE IF EXISTS c1;
        DROP TABLE IF EXISTS p1;
        CREATE TABLE p1(a INT PRIMARY KEY);
        CREATE TABLE c1(id INT PRIMARY KEY AUTOINCREMENT, b INT UNIQUE DEFAULT 3 REFERENCES p1 ON DELETE SET DEFAULT);
        INSERT INTO p1 VALUES (1), (2), (3);
        INSERT INTO c1(b) VALUES (2);
        DELETE FROM p1 WHERE a = 2;
        SELECT b FROM c1;
    ]], {
        -- <fkey8-1.3>
        3
        -- </fkey8-1.3>
    })

test:do_execsql_test(
    "fkey8-1.4",
    [[
        DROP TABLE IF EXISTS c1;
        DROP TABLE IF EXISTS p1;
        CREATE TABLE p1(a INT PRIMARY KEY);
        CREATE TABLE c1(b INT PRIMARY KEY REFERENCES p1 ON DELETE CASCADE);
        CREATE TRIGGER ct1 AFTER DELETE ON c1 FOR EACH ROW BEGIN INSERT INTO p1 VALUES(0); END;
        INSERT INTO p1 VALUES (1), (2), (3);
        INSERT INTO c1 VALUES (2);
        DELETE FROM p1 WHERE a = 2;
        SELECT * FROM p1;
    ]], {
        -- <fkey8-1.4>
        0, 1, 3
        -- </fkey8-1.4>
    })

test:do_catchsql_test(
    "fkey8-1.5",
    [[
        DROP TABLE IF EXISTS c1;
        DROP TABLE IF EXISTS p1;
        CREATE TABLE p1(a INT PRIMARY KEY);
        CREATE TABLE c1(b INT PRIMARY KEY REFERENCES p1 ON DELETE CASCADE);
        CREATE TABLE cc1(d INT PRIMARY KEY REFERENCES c1(b));
        INSERT INTO p1 VALUES (1), (2), (3);
        INSERT INTO c1 VALUES (2), (3);
        INSERT INTO cc1 VALUES (2);
        DELETE FROM p1 WHERE a = 2;
    ]], {
        -- <fkey8-1.5>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey8-1.5>
    })

test:do_execsql_test(
    "fkey8-1.6",
    [[
        DROP TABLE IF EXISTS cc1;
        DROP TABLE IF EXISTS c1;
        DROP TABLE IF EXISTS p1;
        CREATE TABLE p1(a INT PRIMARY KEY);
        CREATE TABLE c1(b INT PRIMARY KEY REFERENCES p1 ON DELETE CASCADE);
        CREATE TABLE cc1(d INT PRIMARY KEY REFERENCES c1(b) ON DELETE CASCADE);
        INSERT INTO p1 VALUES (1), (2), (3);
        INSERT INTO c1 VALUES (2), (3);
        INSERT INTO cc1 VALUES (2);
        DELETE FROM p1 WHERE a = 2;
        SELECT * FROM cc1;
    ]], {
        -- <fkey8-1.6>
        -- </fkey8-1.6>
    })

test:do_execsql_test(
    "fkey8-1.7",
    [[
        DROP TABLE IF EXISTS cc1;
        DROP TABLE IF EXISTS c1;
        DROP TABLE IF EXISTS p1;
        CREATE TABLE p1(a INT PRIMARY KEY);
        CREATE TABLE c1(b INT PRIMARY KEY REFERENCES p1 ON DELETE CASCADE);
        CREATE TABLE cc1(c INT PRIMARY KEY, d INT REFERENCES c1(b) ON DELETE SET NULL);
        INSERT INTO p1 VALUES (1), (2), (3);
        INSERT INTO c1 VALUES (2), (3);
        INSERT INTO cc1 VALUES (2, 2);
        DELETE FROM p1 WHERE a = 2;
        SELECT * FROM cc1;
    ]], {
        -- <fkey8-1.7>
        2, ""
        -- </fkey8-1.7>
    })

test:do_execsql_test(
    "fkey8-1.8",
    [[
        DROP TABLE IF EXISTS cc1;
        DROP TABLE IF EXISTS c1;
        DROP TABLE IF EXISTS p1;
        CREATE TABLE p1(a INT PRIMARY KEY);
        CREATE TABLE c1(b INT PRIMARY KEY REFERENCES p1 ON DELETE CASCADE);
        CREATE TABLE cc1(c INT PRIMARY KEY, d INT DEFAULT 3 REFERENCES c1(b) ON DELETE SET DEFAULT);
        INSERT INTO p1 VALUES (1), (2), (3);
        INSERT INTO c1 VALUES (2), (3);
        INSERT INTO cc1 VALUES (2, 2);
        DELETE FROM p1 WHERE a = 2;
        SELECT * FROM cc1;
    ]], {
        -- <fkey8-1.8>
        2, 3
        -- </fkey8-1.8>
    })

test:do_catchsql_test(
    "fkey8-1.9",
    [[
        DROP TABLE IF EXISTS cc1;
        DROP TABLE IF EXISTS c1;
        DROP TABLE IF EXISTS p1;
        CREATE TABLE p1(a INT UNIQUE, id INT PRIMARY KEY AUTOINCREMENT);
        CREATE TABLE c1(b INT UNIQUE NOT NULL REFERENCES p1(a) ON UPDATE SET NULL, c INT, id INT PRIMARY KEY AUTOINCREMENT);
        INSERT INTO p1(a) VALUES (1), (2), (3);
        INSERT INTO c1(b, c) VALUES (2, 1), (3, 2);
        UPDATE OR IGNORE p1 SET a = 4 WHERE a = 2;
    ]], {
        -- <fkey8-1.9>
        1, "Failed to execute SQL statement: NOT NULL constraint failed: C1.B"
        -- </fkey8-1.9>
    })

test:do_execsql_test(
    "fkey8-1.10",
    [[
        DROP TABLE IF EXISTS c1;
        DROP TABLE IF EXISTS p1;
        CREATE TABLE p1(a INT UNIQUE, id INT PRIMARY KEY AUTOINCREMENT);
        CREATE TABLE c1(b INT UNIQUE NOT NULL REFERENCES p1(a) ON UPDATE CASCADE, c INT, id INT PRIMARY KEY AUTOINCREMENT);
        INSERT INTO p1(a) VALUES (1), (2), (3);
        INSERT INTO c1(b,c) VALUES (2, 1), (3, 2);
        UPDATE OR IGNORE p1 SET a = 4 WHERE a = 2;
        SELECT b,c FROM c1 ORDER BY b,c;
    ]], {
        -- <fkey8-1.10>
        3, 2, 4, 1
        -- </fkey8-1.10>
    })

test:do_execsql_test(
    "fkey8-2.1",
    [[
        DROP TABLE IF EXISTS c1;
        DROP TABLE IF EXISTS p1;
        CREATE TABLE p1(a INT PRIMARY KEY, b TEXT);
        CREATE TABLE c1(x INT PRIMARY KEY REFERENCES p1 DEFERRABLE INITIALLY DEFERRED);
        INSERT INTO p1 VALUES (1, 'one');
        INSERT INTO p1 VALUES (2, 'two');
        INSERT INTO c1 VALUES (1);
        INSERT INTO c1 VALUES (2);
    ]], {
        -- <fkey8-2.1>
        -- </fkey8-2.1>
    })

test:do_catchsql_test(
    "fkey8-2.2",
    [[
        DELETE FROM p1 WHERE a=1;
        INSERT OR REPLACE INTO p1 VALUES(2, 'two');
    ]], {
        -- <fkey8-2.2>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey8-2.2>
    })

test:do_execsql_test(
    "fkey8-3.1",
    [[
        DROP TABLE IF EXISTS c2;
        DROP TABLE IF EXISTS p2;
        CREATE TABLE p2(a INT PRIMARY KEY, b INT);
        CREATE TABLE c2(x INT PRIMARY KEY, y INT REFERENCES p2 DEFERRABLE INITIALLY DEFERRED);
    ]], {
        -- <fkey8-3.1>
        -- </fkey8-3.1>
    })

test:do_catchsql_test(
    "fkey8-3.2",
    [[
        START TRANSACTION;
            INSERT INTO c2 VALUES(13, 13);
            INSERT OR REPLACE INTO c2 VALUES(13, 13);
            DELETE FROM c2;
        COMMIT;
    ]], {
        -- <fkey8-3.2>
        0
        -- </fkey8-3.2>
    })

test:do_execsql_test(
    "fkey8-4.1",
    [[
        CREATE TABLE p3(a INT PRIMARY KEY, b TEXT);
        CREATE TABLE c3(x INT PRIMARY KEY REFERENCES p3);
        INSERT INTO p3 VALUES(1, 'one');
        INSERT INTO p3 VALUES(2, 'two');
        INSERT INTO c3 VALUES(1);
        INSERT INTO c3 VALUES(2);

        CREATE TRIGGER p3d AFTER DELETE ON p3 FOR EACH ROW WHEN old.a=1 BEGIN
            INSERT OR REPLACE INTO p3 VALUES(2, 'three');
        END;
    ]], {
        -- <fkey8-4.1>
        -- </fkey8-4.1>
    })

test:do_execsql_test(
    "fkey8-4.2",
    [[
        DROP TABLE IF EXISTS c3;
        DROP TABLE IF EXISTS p3;
        CREATE TABLE p3(a INT PRIMARY KEY, b TEXT);
        CREATE TABLE c3(x INT PRIMARY KEY REFERENCES p3);
        INSERT INTO p3 VALUES(1, 'one');
        INSERT INTO p3 VALUES(2, 'two');
        INSERT INTO c3 VALUES(1);
        INSERT INTO c3 VALUES(2);

        CREATE TRIGGER p3d AFTER DELETE ON p3 FOR EACH ROW WHEN old.a=1 BEGIN
            INSERT OR REPLACE INTO p3 VALUES(2, 'three');
        END;
    ]], {
        -- <fkey8-4.2>
        -- </fkey8-4.2>
    })


test:do_catchsql_test(
    "fkey8-4.3",
    [[
        DELETE FROM p3 WHERE a=1;
    ]], {
        -- <fkey8-4.3>
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
        -- </fkey8-4.3>
    })

test:finish_test()
