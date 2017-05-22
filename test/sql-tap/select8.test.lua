#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(3)

--!./tcltestrunner.lua
-- 2001 September 15
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library. 
--
-- The focus of this file is testing that LIMIT and OFFSET work for
-- unusual combinations SELECT statements.
--
-- $Id: select8.test,v 1.1 2008/01/12 12:48:09 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:execsql [[
    DROP TABLE IF EXISTS songs;
    CREATE TABLE songs(songid primary key, artist, timesplayed);
    INSERT INTO songs VALUES(1,'one',1);
    INSERT INTO songs VALUES(2,'one',2);
    INSERT INTO songs VALUES(3,'two',3);
    INSERT INTO songs VALUES(4,'three',5);
    INSERT INTO songs VALUES(5,'one',7);
    INSERT INTO songs VALUES(6,'two',11);
]]
local result = test:execsql [[
    SELECT DISTINCT artist,sum(timesplayed) AS total      
    FROM songs      
    GROUP BY LOWER(artist)      
]]

function subrange(t, first, last)
    local sub = {}
    for i=first,last do
        sub[#sub + 1] = t[i]
    end
    return sub
end

test:do_execsql_test(
    "select8-1.1",
    [[
        SELECT DISTINCT artist,sum(timesplayed) AS total      
        FROM songs      
        GROUP BY LOWER(artist)      
        LIMIT 1 OFFSET 1
    ]], subrange(result, 3, 4))

test:do_execsql_test(
    "select8-1.2",
    [[
        SELECT DISTINCT artist,sum(timesplayed) AS total      
        FROM songs      
        GROUP BY LOWER(artist)      
        LIMIT 2 OFFSET 1
    ]], subrange(result, 3, 6))

test:do_execsql_test(
    "select8-1.3",
    [[
        SELECT DISTINCT artist,sum(timesplayed) AS total      
        FROM songs      
        GROUP BY LOWER(artist)      
        LIMIT -1 OFFSET 2
    ]], subrange(result, 5, #result))

test:finish_test()

