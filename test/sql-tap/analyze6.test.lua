#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(13)

--!./tcltestrunner.lua
-- 2011 March 3
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
-- This file implements tests for sql library.  The focus of the tests
-- in this file a corner-case query planner optimization involving the
-- join order of two tables of different sizes.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

test:do_test(
    "analyze6-1.0",
    function()
        return test:execsql([[
            CREATE TABLE cat(id INTEGER PRIMARY KEY AUTOINCREMENT, x INT, yz TEXT);
            CREATE UNIQUE INDEX catx ON cat(x);
            /* Give cat 16 unique integers */
            INSERT INTO cat(x) VALUES(1);
            INSERT INTO cat(x) VALUES(2);
            INSERT INTO cat(x) SELECT x+2 FROM cat;
            INSERT INTO cat(x) SELECT x+4 FROM cat;
            INSERT INTO cat(x) SELECT x+8 FROM cat;

            CREATE TABLE ev(id INTEGER PRIMARY KEY AUTOINCREMENT, y INT);
            CREATE INDEX evy ON ev(y);
            /* ev will hold 32 copies of 16 integers found in cat */
            INSERT INTO ev(y) SELECT x FROM cat;
            INSERT INTO ev(y) SELECT x FROM cat;
            INSERT INTO ev(y) SELECT y FROM ev;
            INSERT INTO ev(y) SELECT y FROM ev;
            INSERT INTO ev(y) SELECT y FROM ev;
            INSERT INTO ev(y) SELECT y FROM ev;
            ANALYZE;
            SELECT count(*) FROM cat;
        ]])
    end, {
        -- <analyze6-1.0>
        16
        -- </analyze6-1.0>
})

test:do_test(
    "analyze6-1.0.1",
    function()
        return test:execsql([[
            SELECT count(*) FROM ev;
        ]])
    end, {
        -- <analyze6-1.0>
        512
        -- </analyze6-1.0>
})

-- The lowest cost plan is to scan CAT and for each integer there, do a single
-- lookup of the first corresponding entry in EV then read off the equal values
-- in EV.  (Prior to the 2011-03-04 enhancement to where.c, this query would
-- have used EV for the outer loop instead of CAT - which was about 3x slower.)
--
test:do_eqp_test(
    "analyze6-1.1",
    [[SELECT count(*) FROM ev, cat WHERE x=y;]],
    {
        -- <analyze6-1.1>
        {0, 0, 1, "SCAN TABLE CAT"}, {0, 1, 0, "SEARCH TABLE EV USING COVERING INDEX EVY (Y=?)"}
        -- </analyze6-1.1>
})

-- The same plan is chosen regardless of the order of the tables in the
-- FROM clause.
--
test:do_eqp_test(
    "analyze6-1.2",
    [[SELECT count(*) FROM cat, ev WHERE x=y]],
    {
        -- <analyze6-1.2>
        {0, 0, 0, "SCAN TABLE CAT"}, {0, 1, 1, "SEARCH TABLE EV USING COVERING INDEX EVY (Y=?)"}
        -- </analyze6-1.2>
})

-- Ticket [83ea97620bd3101645138b7b0e71c12c5498fe3d] 2011-03-30
-- If ANALYZE is run on an empty table, make sure indices are used
-- on the table.
--
test:do_test(
    "analyze6-2.1",
    function()
        test:execsql([[
            CREATE TABLE t201(x INTEGER PRIMARY KEY, y  INT UNIQUE, z INT );
            CREATE INDEX t201z ON t201(z);
            ANALYZE;
        ]])
        return test:execsql("EXPLAIN QUERY PLAN ".."SELECT * FROM t201 WHERE z=5")
    end, {
        -- <analyze6-2.1>
        0, 0, 0, "SEARCH TABLE T201 USING COVERING INDEX T201Z (Z=?)"
        -- </analyze6-2.1>
})

test:do_eqp_test(
    "analyze6-2.2",
    [[SELECT * FROM t201 WHERE y=5]],
    {
        -- <analyze6-2.2>
        {0, 0, 0, "SEARCH TABLE T201 USING COVERING INDEX unique_unnamed_T201_2 (Y=?)"}
        -- </analyze6-2.2>
})

test:do_eqp_test(
    "analyze6-2.3",
    [[SELECT * FROM t201 WHERE x=5]],
    {
        -- <analyze6-2.3>
        {0, 0, 0, "SEARCH TABLE T201 USING PRIMARY KEY (X=?)"}
        -- </analyze6-2.3>
})

test:do_test(
    "analyze6-2.4",
    function()
        test:execsql([[
            INSERT INTO t201 VALUES(1,2,3),(2,3,4),(3,4,5);
            ANALYZE t201;
        ]])
        return test:execsql("EXPLAIN QUERY PLAN ".."SELECT * FROM t201 WHERE z=5")
    end, {
        -- <analyze6-2.4>
        0, 0, 0, "SEARCH TABLE T201 USING COVERING INDEX T201Z (Z=?)"
        -- </analyze6-2.4>
})

test:do_eqp_test(
    "analyze6-2.5",
    [[SELECT * FROM t201 WHERE y=5]],
    {
        -- <analyze6-2.5>
        {0, 0, 0, "SEARCH TABLE T201 USING COVERING INDEX unique_unnamed_T201_2 (Y=?)"}
        -- </analyze6-2.5>
})

test:do_eqp_test(
    "analyze6-2.6",
    [[SELECT * FROM t201 WHERE x=5]],
    {
        -- <analyze6-2.6>
        {0, 0, 0, "SEARCH TABLE T201 USING PRIMARY KEY (X=?)"}
        -- </analyze6-2.6>
})

test:do_test(
    "analyze6-2.7",
    function()
        test:execsql([[
            INSERT INTO t201 VALUES(4,5,7);
            INSERT INTO t201 SELECT x+100, y+100, z+100 FROM t201;
            INSERT INTO t201 SELECT x+200, y+200, z+200 FROM t201;
            INSERT INTO t201 SELECT x+400, y+400, z+400 FROM t201;
            ANALYZE t201;
        ]])
        return test:execsql("EXPLAIN QUERY PLAN ".."SELECT * FROM t201 WHERE z=5")
    end, {
        -- <analyze6-2.7>
        0, 0, 0, "SEARCH TABLE T201 USING COVERING INDEX T201Z (Z=?)"
        -- </analyze6-2.7>
})

test:do_eqp_test(
    "analyze6-2.8",
    [[SELECT * FROM t201 WHERE y=5]],
    {
        -- <analyze6-2.8>
        {0, 0, 0, "SEARCH TABLE T201 USING COVERING INDEX unique_unnamed_T201_2 (Y=?)"}
        -- </analyze6-2.8>
})

test:do_eqp_test(
    "analyze6-2.9",
    [[SELECT * FROM t201 WHERE x=5]],
    {
        -- <analyze6-2.9>
        {0, 0, 0, "SEARCH TABLE T201 USING PRIMARY KEY (X=?)"}
        -- </analyze6-2.9>
})

test:finish_test()
