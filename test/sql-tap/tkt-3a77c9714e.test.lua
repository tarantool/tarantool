#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(4)

--!./tcltestrunner.lua
-- 2011 December 06
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
-- This file implements tests to verify that ticket [3a77c9714e] has been
-- fixed.  
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


local testprefix = "tkt-3a77c9714e"
test:do_execsql_test(
    1.1,
    [[
        CREATE TABLE t1(t1_id INTEGER PRIMARY KEY, t1_title TEXT);
        CREATE TABLE t2(t2_id INTEGER PRIMARY KEY, t2_title TEXT);
        CREATE TABLE t3(t3_id INTEGER PRIMARY KEY, t3_title TEXT);

        INSERT INTO t1 (t1_id, t1_title) VALUES (888, 'ABCDEF');
        INSERT INTO t2 (t2_id, t2_title) VALUES (999, 'ABCDEF');
        INSERT INTO t3 (t3_id, t3_title) VALUES (999, 'ABCDEF');
    ]])

test:do_execsql_test(
    1.2,
    [[
        SELECT t1_title, t2_title
        FROM t1 LEFT JOIN t2
        WHERE
          t2_id = (SELECT t3_id FROM
           ( SELECT t3_id FROM t3 WHERE t3_title=t1_title LIMIT 500 )
        )
    ]], {
        -- <1.2>
        "ABCDEF", "ABCDEF"
        -- </1.2>
    })

test:do_execsql_test(
    2.1,
    -- primary key autoincrement was removed from id
    -- because it does not related to the ticket
    [=[
        CREATE TABLE Beginnings (
          Id INTEGER PRIMARY KEY,Title TEXT, EndingId INTEGER
        );
        CREATE TABLE Endings (Id INT PRIMARY KEY,Title TEXT,EndingId INT);
        INSERT INTO Beginnings (Id, Title, EndingId) VALUES (1, 'FACTOR', 18);
        INSERT INTO Beginnings (Id, Title, EndingId) VALUES (2, 'SWIMM', 18);
        INSERT INTO Endings (Id, Title, EndingId) VALUES (1, 'ING', 18);
    ]=])

test:do_execsql_test(
    2.2,
    [[
        SELECT
          SrcWord, Beginnings.Title
          FROM 
            (SELECT 'FACTORING' AS SrcWord UNION SELECT 'SWIMMING' AS SrcWord )
          LEFT JOIN 
            Beginnings
          WHERE Beginnings.Id= (
            SELECT BeginningId FROM (
              SELECT SrcWord, B.Id as BeginningId, B.Title || E.Title As Connected
              FROM Beginnings B LEFT JOIN Endings E ON B.EndingId=E.EndingId
              WHERE Connected=SrcWord LIMIT 1
            )
          )
    ]], {
        -- <2.2>
        "FACTORING", "FACTOR", "SWIMMING", "SWIMM"
        -- </2.2>
    })

test:finish_test()

