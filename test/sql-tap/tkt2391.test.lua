#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(4)

--!./tcltestrunner.lua
--
-- 2007 May 28
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- $Id: tkt2391.test,v 1.1 2007/05/29 12:11:30 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "tkt2391.1",
    [[
        CREATE TABLE folders(folderid INT , parentid INT , foldername TEXT COLLATE binary primary key);
        INSERT INTO folders VALUES(1, 3, 'FolderA');
        INSERT INTO folders VALUES(1, 3, 'folderB');
        INSERT INTO folders VALUES(4, 0, 'FolderC');
    ]], {
        -- <tkt2391.1>
        
        -- </tkt2391.1>
    })

test:do_execsql_test(
    "tkt2391.2",
    [[
        SELECT count(*) FROM folders WHERE foldername < 'FolderC';
    ]], {
        -- <tkt2391.2>
        1
        -- </tkt2391.2>
    })

test:do_execsql_test(
    "tkt2391.3",
    [[
        SELECT count(*) FROM folders WHERE foldername < 'FolderC' COLLATE "unicode_ci";
    ]], {
        -- <tkt2391.3>
        2
        -- </tkt2391.3>
    })

-- This demonstrates the bug. Creating the index causes SQLite to ignore
-- the "COLLATE nocase" clause and use the default collation sequence 
-- for column "foldername" instead (happens to be BINARY in this case).
--
test:do_execsql_test(
    "tkt2391.4",
    [[
        CREATE INDEX f_i ON folders(foldername);
        SELECT count(*) FROM folders WHERE foldername < 'FolderC' COLLATE "unicode_ci";
    ]], {
        -- <tkt2391.4>
        2
        -- </tkt2391.4>
    })

test:finish_test()

