#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(5)

--!./tcltestrunner.lua
-- 2014-03-04
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file implements tests to verify that ticket [4ef7e3cfca] has been
-- fixed.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
testprefix = "tkt-4ef7e3cfca"

test:do_catchsql_test(
    1.1,
    [[
        CREATE TABLE x(a  INT primary key);
        CREATE TRIGGER t AFTER INSERT ON x BEGIN
          SELECT * FROM x WHERE abc.a = 1;
        END;
        INSERT INTO x VALUES('assert');
    ]], {
        -- <1.1>
        1, "no such column: ABC.A"
        -- </1.1>
    })

test:execsql("DROP TABLE x;");
--reset_db()
test:do_execsql_test(
    2.1,
    [[
        CREATE TABLE w(a  INT primary key);
        CREATE TABLE x(a  INT primary key);
        CREATE TABLE y(a  INT primary key);
        CREATE TABLE z(a  INT primary key);

        INSERT INTO x(a) VALUES(5);
        INSERT INTO y(a) VALUES(10);

        CREATE TRIGGER t AFTER INSERT ON w BEGIN
          INSERT INTO z
          SELECT (SELECT x.a + y.a FROM y) FROM x;
        END;
        INSERT INTO w VALUES(1);
    ]])

test:do_execsql_test(
    2.2,
    [[
        SELECT * FROM z;
    ]], {
        -- <2.2>
        15
        -- </2.2>
    })

test:execsql([[
    DROP TABLE x;
    DROP TABLE w;
    DROP TABLE y;
    DROP TABLE z;
    ]]);
--reset_db()
test:do_execsql_test(
    3.1,
    [[
        CREATE TABLE w(a  INT primary key);
        CREATE TABLE x(b  INT primary key);
        CREATE TABLE y(a  INT primary key);
        CREATE TABLE z(a  INT primary key);

        INSERT INTO x(b) VALUES(5);
        INSERT INTO y(a) VALUES(10);

        CREATE TRIGGER t AFTER INSERT ON w BEGIN
          INSERT INTO z
          SELECT (SELECT x.b + y.a FROM y) FROM x;
        END;
        INSERT INTO w VALUES(2);
    ]])

test:do_execsql_test(
    3.2,
    [[
        SELECT * FROM z;
    ]], {
        -- <3.2>
        15
        -- </3.2>
    })

test:finish_test()

