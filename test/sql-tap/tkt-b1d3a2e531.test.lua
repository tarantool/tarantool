#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(11)

--!./tcltestrunner.lua
-- 2011 August 22
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.
--
-- This file implements tests for foreign keys. Specifically, it tests
-- that ticket b1d3a2e531 has been fixed.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

test:do_execsql_test(
    1.1,
    [[
        CREATE TABLE pp(x TEXT PRIMARY KEY);
        CREATE TABLE cc(y TEXT primary key REFERENCES pp DEFERRABLE INITIALLY DEFERRED);
        INSERT INTO pp VALUES('abc');
        INSERT INTO cc VALUES('abc');
    ]])

test:do_execsql_test(
    1.2,
    [[
        DROP TABLE cc;
        DROP TABLE pp;
    ]])

test:do_execsql_test(
    1.3,
    [[
        CREATE TABLE pp(x TEXT PRIMARY KEY);
        CREATE TABLE cc(y TEXT primary key REFERENCES pp DEFERRABLE INITIALLY DEFERRED);
        INSERT INTO pp VALUES('abc');
        INSERT INTO cc VALUES('abc');
    ]])

test:do_execsql_test(
    1.4,
    [[
        DROP TABLE cc;
        DROP TABLE pp;
    ]])

test:do_execsql_test(
    2.1,
    [[
        CREATE TABLE pp(x INTEGER PRIMARY KEY);
        CREATE TABLE cc(
          y INTEGER PRIMARY KEY REFERENCES pp DEFERRABLE INITIALLY DEFERRED
        );
        INSERT INTO pp VALUES(5);
        INSERT INTO cc VALUES(5);
    ]])

test:do_execsql_test(
    2.2,
    [[
        DROP TABLE cc;
        DROP TABLE pp;
    ]])

test:do_execsql_test(
    2.3,
    [[
        CREATE TABLE pp(x INTEGER PRIMARY KEY);
        CREATE TABLE cc(
          y INTEGER PRIMARY KEY REFERENCES pp DEFERRABLE INITIALLY DEFERRED
        );
        INSERT INTO pp VALUES(5);
        INSERT INTO cc VALUES(5);
    ]])

test:do_execsql_test(
    2.4,
    [[
        DROP TABLE cc;
        DROP TABLE pp;
    ]])

test:do_execsql_test(
    3.1,
    [[
        CREATE TABLE pp1(x  INT PRIMARY KEY);
        CREATE TABLE cc1(y  INT REFERENCES pp1 DEFERRABLE INITIALLY DEFERRED, a  INT primary key);

        CREATE TABLE pp2(x  INT PRIMARY KEY);
        CREATE TABLE cc2(y  INT primary key REFERENCES pp1 DEFERRABLE INITIALLY DEFERRED);

        INSERT INTO pp1 VALUES(2200);
        INSERT INTO cc1 VALUES(NULL, 1);

        INSERT INTO pp2 VALUES(2200);
        INSERT INTO cc2 VALUES(2200);
    ]])

test:do_catchsql_test(
    3.2,
    [[
        -- START TRANSACTION;
          DELETE FROM pp2;
        -- COMMIT;
          DROP TABLE pp1;
          DROP TABLE cc1;
    ]], {
        -- <3.2>
        1, "Can't drop space 'PP1': other objects depend on it"
        -- </3.2>
    })

test:do_catchsql_test(
    3.3,
    [[
        DROP TABLE cc2;
        -- COMMIT;
    ]], {
        -- <3.3>
        0
        -- </3.3>
    })

test:finish_test()

