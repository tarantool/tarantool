#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(4)

--!./tcltestrunner.lua
-- 2005 September 19
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
-- This file implements tests to verify that ticket #1444 has been
-- fixed.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- The use of a VIEW that contained an ORDER BY clause within a UNION ALL
-- was causing problems.  See ticket #1444.
--
test:do_execsql_test(
    "tkt1444-1.1",
    [[
        CREATE TABLE DemoTable (id  INT primary key, x INTEGER, TextKey TEXT, DKey NUMBER);
        CREATE INDEX DemoTableIdx ON DemoTable (TextKey);
        INSERT INTO DemoTable VALUES(1, 9,'8',7);
        INSERT INTO DemoTable VALUES(2, 1,'2',3);
        CREATE VIEW DemoView AS SELECT x, TextKey, DKey FROM DemoTable ORDER BY TextKey;
        SELECT x,TextKey,DKey FROM DemoTable UNION ALL SELECT * FROM DemoView ORDER BY 1;
    ]], {
        -- <tkt1444-1.1>
        1, "2", 3.0, 1, "2", 3.0, 9, "8", 7.0, 9, "8", 7.0
        -- </tkt1444-1.1>
    })

test:do_execsql_test(
    "tkt1444-1.2",
    [[
        SELECT x,TextKey,DKey FROM DemoTable UNION ALL SELECT * FROM DemoView;
    ]], {
        -- <tkt1444-1.2>
        9,"8",7,1,"2",3,1,"2",3,9,"8",7
        -- </tkt1444-1.2>
    })

test:do_execsql_test(
    "tkt1444-1.3",
    [[
        DROP VIEW DemoView;
        CREATE VIEW DemoView AS SELECT x,TextKey,DKey FROM DemoTable;
        SELECT x,TextKey,DKey FROM DemoTable UNION ALL SELECT * FROM DemoView ORDER BY 1;
    ]], {
        -- <tkt1444-1.3>
        1, "2", 3.0, 1, "2", 3.0, 9, "8", 7.0, 9, "8", 7.0
        -- </tkt1444-1.3>
    })

test:do_execsql_test(
    "tkt1444-1.4",
    [[
        SELECT x,TextKey,DKey FROM DemoTable UNION ALL SELECT * FROM DemoView;
    ]], {
        -- <tkt1444-1.4>
        9,"8",7,1,"2",3,9,"8",7,1,"2",3
        -- </tkt1444-1.4>
    })

test:finish_test()

