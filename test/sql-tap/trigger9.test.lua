#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(28)

--!./tcltestrunner.lua
-- 2008 January 1
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library. Specifically,
-- it tests some compiler optimizations for SQL statements featuring
-- triggers:
--
--
--
-- trigger9-1.* -   Test that if there are no references to OLD.* cols, or a
--                  reference to only OLD.rowid, the data is not loaded.
--
-- trigger9-2.* -   Test that for NEW.* records populated by UPDATE
--                  statements, unused fields are populated with NULL values.
--
-- trigger9-3.* -   Test that the temporary tables used for OLD.* references
--                  in "INSTEAD OF" triggers have NULL values in unused
--                  fields.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


testprefix = "trigger9"

local function has_rowdata(sql)
    local result = test:execsql('EXPLAIN '..sql)

    if test:lsearch(result, 'RowData') >= 0 then
        return {0};
    else
        return {1};
    end
--     X(41, "X!cmd", [=[["expr","[lsearch [execsql \"explain $sql\"] RowData]>=0"]]=])
end

box.internal.sql_create_function('randstr', test.randstr, 1)

-- MUST_WORK_TEST
test:do_execsql_test(
    "trigger9-1.1",
    [[
        CREATE TABLE t1(x PRIMARY KEY, y, z);
        INSERT INTO t1 VALUES('1', randstr(10000), '2');
        INSERT INTO t1 VALUES('2', randstr(10000), '4');
        INSERT INTO t1 VALUES('3', randstr(10000), '6');
        CREATE TABLE t2(x PRIMARY KEY);
    ]], {
        -- <trigger9-1.1>

        -- </trigger9-1.1>
    })

test:do_execsql_test(
    "trigger9-1.2.1",
    [[
        CREATE TRIGGER trig1 BEFORE DELETE ON t1 BEGIN
          INSERT INTO t2 VALUES(old.x);
        END;
        BEGIN;
          DELETE FROM t1;
          SELECT * FROM t2;
    ]], {
        -- <trigger9-1.2.1>
        '1', '2', '3'
        -- </trigger9-1.2.1>
    })

test:do_test(
    "trigger9-1.2.3",
    function()
        return has_rowdata("DELETE FROM t1")
    end, {
        -- <trigger9-1.2.3>
        0
        -- </trigger9-1.2.3>
    })

test:do_execsql_test(
    "trigger9-1.2.4",
    [[
        ROLLBACK
    ]], {
        -- <trigger9-1.2.4>

        -- </trigger9-1.2.4>
    })

test:do_execsql_test(
    "trigger9-1.3.1",
    [[
        DROP TRIGGER IF EXISTS trig1;
        CREATE TRIGGER trig1 BEFORE DELETE ON t1 BEGIN
          INSERT INTO t2 VALUES(old.x);
        END;
        BEGIN;
          DELETE FROM t1;
          SELECT * FROM t2;
    ]], {
        -- <trigger9-1.3.1>
        '1', '2', '3'
        -- </trigger9-1.3.1>
    })

test:do_test(
    "trigger9-1.3.2",
    function()
        return has_rowdata("DELETE FROM t1")
    end, {
        -- <trigger9-1.3.2>
        0
        -- </trigger9-1.3.2>
    })

test:do_execsql_test(
    "trigger9-1.3.3",
    [[
        ROLLBACK
    ]], {
        -- <trigger9-1.3.3>

        -- </trigger9-1.3.3>
    })

test:do_execsql_test(
    "trigger9-1.4.1",
    [[
        DROP TRIGGER IF EXISTS trig1;
        CREATE TRIGGER trig1 BEFORE DELETE ON t1 WHEN old.x='1' BEGIN
          INSERT INTO t2 VALUES(old.x);
        END;
        BEGIN;
          DELETE FROM t1;
          SELECT * FROM t2;
    ]], {
        -- <trigger9-1.4.1>
        '1'
        -- </trigger9-1.4.1>
    })

test:do_test(
    "trigger9-1.4.2",
    function()
        return has_rowdata("DELETE FROM t1")
    end, {
        -- <trigger9-1.4.2>
        0
        -- </trigger9-1.4.2>
    })

test:do_execsql_test(
    "trigger9-1.4.3",
    [[
        ROLLBACK
    ]], {
        -- <trigger9-1.4.3>

        -- </trigger9-1.4.3>
    })

test:do_execsql_test(
    "trigger9-1.5.1",
    [[
        DROP TRIGGER IF EXISTS trig1;
        CREATE TRIGGER trig1 BEFORE UPDATE ON t1 BEGIN
          INSERT INTO t2 VALUES(old.x);
        END;
        BEGIN;
          UPDATE t1 SET y = '';
          SELECT * FROM t2;
    ]], {
        -- <trigger9-1.5.1>
        '1', '2', '3'
        -- </trigger9-1.5.1>
    })

test:do_test(
    "trigger9-1.5.2",
    function()
        return has_rowdata("UPDATE t1 SET y = ''")
    end, {
        -- <trigger9-1.5.2>
        0
        -- </trigger9-1.5.2>
    })

test:do_execsql_test(
    "trigger9-1.5.3",
    [[
        ROLLBACK
    ]], {
        -- <trigger9-1.5.3>

        -- </trigger9-1.5.3>
    })

test:do_execsql_test(
    "trigger9-1.6.1",
    [[
        DROP TRIGGER IF EXISTS trig1;
        CREATE TRIGGER trig1 BEFORE UPDATE ON t1 BEGIN
          INSERT INTO t2 VALUES(old.x);
        END;
        BEGIN;
          UPDATE t1 SET y = '';
          SELECT * FROM t2;
    ]], {
        -- <trigger9-1.6.1>
        '1', '2', '3'
        -- </trigger9-1.6.1>
    })

test:do_test(
    "trigger9-1.6.2",
    function()
        return has_rowdata("UPDATE t1 SET y = ''")
    end, {
        -- <trigger9-1.6.2>
        0
        -- </trigger9-1.6.2>
    })

test:do_execsql_test(
    "trigger9-1.6.3",
    [[
        ROLLBACK
    ]], {
        -- <trigger9-1.6.3>

        -- </trigger9-1.6.3>
    })

test:do_execsql_test(
    "trigger9-1.7.1",
    [[
        DROP TRIGGER IF EXISTS trig1;
        CREATE TRIGGER trig1 BEFORE UPDATE ON t1 WHEN old.x>='2' BEGIN
          INSERT INTO t2 VALUES(old.x);
        END;
        BEGIN;
          UPDATE t1 SET y = '';
          SELECT * FROM t2;
    ]], {
        -- <trigger9-1.7.1>
        '2', '3'
        -- </trigger9-1.7.1>
    })

test:do_test(
    "trigger9-1.7.2",
    function()
        return has_rowdata("UPDATE t1 SET y = ''")
    end, {
        -- <trigger9-1.7.2>
        0
        -- </trigger9-1.7.2>
    })

test:do_execsql_test(
    "trigger9-1.7.3",
    [[
        ROLLBACK
    ]], {
        -- <trigger9-1.7.3>

        -- </trigger9-1.7.3>
    })

test:do_execsql_test(
    "trigger9-3.1",
    [[
        CREATE TABLE t3(id INTEGER PRIMARY KEY, a, b);
        INSERT INTO t3 VALUES(1, 1, 'one');
        INSERT INTO t3 VALUES(2, 2, 'two');
        INSERT INTO t3 VALUES(3, 3, 'three');
    ]], {
        -- <trigger9-3.1>

        -- </trigger9-3.1>
    })

test:do_execsql_test(
    "trigger9-3.2",
    [[
        CREATE VIEW v1 AS SELECT * FROM t3;
        DROP TRIGGER IF EXISTS trig1;
        CREATE TRIGGER trig1 INSTEAD OF UPDATE ON v1 BEGIN
          INSERT INTO t2 VALUES(old.a);
        END;
        BEGIN;
          UPDATE v1 SET b = 'hello';
          SELECT * FROM t2;
        ROLLBACK;
        DROP VIEW v1;
    ]], {
        -- <trigger9-3.2>
        1, 2, 3
        -- </trigger9-3.2>
    })

test:do_test(
    "trigger9-3.3",
    function()
        -- In this test the 'c' column of the view is not required by
        -- the INSTEAD OF trigger, but the expression is reused internally as
        -- part of the view's WHERE clause. Check that this does not cause
        -- a problem.
        --
        return test:execsql([[
            CREATE VIEW v1 AS SELECT a, b AS c FROM t3 WHERE c > 'one';
            DROP TRIGGER IF EXISTS trig1;
            CREATE TRIGGER trig1 INSTEAD OF UPDATE ON v1 BEGIN
              INSERT INTO t2 VALUES(old.a);
            END;
            BEGIN;
              UPDATE v1 SET c = 'hello';
              SELECT * FROM t2;
            ROLLBACK;
            DROP VIEW v1;
        ]])
    end, {
        -- <trigger9-3.3>
        2, 3
        -- </trigger9-3.3>
    })

test:do_execsql_test(
    "trigger9-3.4",
    [[
        CREATE VIEW v1 AS SELECT DISTINCT a, b FROM t3;
        DROP TRIGGER IF EXISTS trig1;
        CREATE TRIGGER trig1 INSTEAD OF UPDATE ON v1 BEGIN
          INSERT INTO t2 VALUES(old.a);
        END;
        BEGIN;
          INSERT INTO t3 VALUES(4, 3, 'three');
          UPDATE v1 SET b = 'hello';
          SELECT * FROM t2;
        ROLLBACK;
        DROP VIEW v1;
    ]], {
        -- <trigger9-3.4>
        1, 2, 3
        -- </trigger9-3.4>
    })

test:do_execsql_test(
    "trigger9-3.5",
    [[
        CREATE VIEW v1 AS SELECT a, b FROM t3 EXCEPT SELECT 1, 'one';
        DROP TRIGGER IF EXISTS trig1;
        CREATE TRIGGER trig1 INSTEAD OF UPDATE ON v1 BEGIN
          INSERT INTO t2 VALUES(old.a);
        END;
        BEGIN;
          INSERT INTO t3 VALUES(5, 1, 'uno');
          UPDATE v1 SET b = 'hello';
          SELECT * FROM t2;
        ROLLBACK;
        DROP VIEW v1;
    ]], {
        -- <trigger9-3.5>
        1, 2, 3
        -- </trigger9-3.5>
    })

test:do_execsql_test(
    "trigger9-3.6",
    [[
        CREATE VIEW v1 AS
          SELECT sum(a) AS a, max(b) AS b FROM t3 GROUP BY t3.a HAVING b>'two';
        DROP TRIGGER IF EXISTS trig1;
        CREATE TRIGGER trig1 INSTEAD OF UPDATE ON v1 BEGIN
          INSERT INTO t2 VALUES(old.a);
        END;
        BEGIN;
          INSERT INTO t3 VALUES(6, 1, 'zero');
          UPDATE v1 SET b = 'hello';
          SELECT * FROM t2;
        ROLLBACK;
        DROP VIEW v1;
    ]], {
        -- <trigger9-3.6>
        2
        -- </trigger9-3.6>
    })



--reset_db
test:do_execsql_test(
    4.1,
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        DROP TABLE t3;
        CREATE TABLE t1(a PRIMARY KEY, b);
        CREATE TABLE log(x PRIMARY KEY);
        INSERT INTO t1 VALUES(1, 2);
        INSERT INTO t1 VALUES(3, 4);
        CREATE VIEW v1 AS SELECT a, b FROM t1;

        CREATE TRIGGER tr1 INSTEAD OF DELETE ON v1 BEGIN
          INSERT INTO log VALUES('delete');
        END;

        CREATE TRIGGER tr2 INSTEAD OF UPDATE ON v1 BEGIN
          INSERT INTO log VALUES('update');
        END;

        CREATE TRIGGER tr3 INSTEAD OF INSERT ON v1 BEGIN
          INSERT INTO log VALUES('insert');
        END;
    ]])

test:do_execsql_test(
    4.2,
    [[
        DELETE FROM v1 WHERE a=1;
    ]], {
        -- <4.2>

        -- </4.2>
    })

test:do_execsql_test(
    4.3,
    [[
        UPDATE v1 SET a=b WHERE a=2;
    ]], {
        -- <4.3>

        -- </4.3>
    })


test:finish_test()
