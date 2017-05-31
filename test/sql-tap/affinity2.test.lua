#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(10)

--!./tcltestrunner.lua
-- 2015-06-02
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.  The
-- focus of this file is type affinity in comparison operations.
--
-- EVERYWHERE HERE WAS 'ROWID' INSTEAD OF 'ID'
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "affinity2-100",
    [[
        CREATE TABLE t1(
          id integer primary key,
          xi INTEGER,
          xr REAL,
          xb BLOB,
          xn NUMERIC,
          xt TEXT
        );
        INSERT INTO t1(id,xi,xr,xb,xn,xt) VALUES(1,1,1,1,1,1);
        INSERT INTO t1(id,xi,xr,xb,xn,xt) VALUES(2,'2','2','2','2','2');
        INSERT INTO t1(id,xi,xr,xb,xn,xt) VALUES(3,'03','03','03','03','03');

    ]], {
        -- <affinity2-100>
        
        -- </affinity2-100>
    })

test:do_execsql_test(
    "affinity2-110",
    [[
        SELECT xi, typeof(xi) FROM t1 ORDER BY id;
    ]], {
        -- <affinity2-110>
        1, "integer", 2, "integer", 3, "integer"
        -- </affinity2-110>
    })

test:do_execsql_test(
    "affinity2-120",
    [[
        SELECT xr, typeof(xr) FROM t1 ORDER BY id;
    ]], {
        -- <affinity2-120>
        1.0, "real", 2.0, "real", 3.0, "real"
        -- </affinity2-120>
    })

test:do_execsql_test(
    "affinity2-130",
    [[
        SELECT xb, typeof(xb) FROM t1 ORDER BY id;
    ]], {
        -- <affinity2-130>
        1, "integer", "2", "text", "03", "text"
        -- </affinity2-130>
    })

test:do_execsql_test(
    "affinity2-140",
    [[
        SELECT xn, typeof(xn) FROM t1 ORDER BY id;
    ]], {
        -- <affinity2-140>
        1, "integer", 2, "integer", 3, "integer"
        -- </affinity2-140>
    })

test:do_execsql_test(
    "affinity2-150",
    [[
        SELECT xt, typeof(xt) FROM t1 ORDER BY id;
    ]], {
        -- <affinity2-150>
        "1", "text", "2", "text", "03", "text"
        -- </affinity2-150>
    })

test:do_execsql_test(
    "affinity2-200",
    [[
        SELECT id, xi==xt, xi==xb, xi==+xt FROM t1 ORDER BY id;
    ]], {
        -- <affinity2-200>
        1, 1, 1, 1, 2, 1, 1, 1, 3, 1, 1, 1
        -- </affinity2-200>
    })

test:do_execsql_test(
    "affinity2-210",
    [[
        SELECT id, xr==xt, xr==xb, xr==+xt FROM t1 ORDER BY id;
    ]], {
        -- <affinity2-210>
        1, 1, 1, 1, 2, 1, 1, 1, 3, 1, 1, 1
        -- </affinity2-210>
    })

test:do_execsql_test(
    "affinity2-220",
    [[
        SELECT id, xn==xt, xn==xb, xn==+xt FROM t1 ORDER BY id;
    ]], {
        -- <affinity2-220>
        1, 1, 1, 1, 2, 1, 1, 1, 3, 1, 1, 1
        -- </affinity2-220>
    })

test:do_execsql_test(
    "affinity2-300",
    [[
        SELECT id, xt==+xi, xt==xi, xt==xb FROM t1 ORDER BY id;
    ]], {
        -- <affinity2-300>
        1, 1, 1, 0, 2, 1, 1, 1, 3, 0, 1, 1
        -- </affinity2-300>
    })



test:finish_test()
