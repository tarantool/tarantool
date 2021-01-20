#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(57)

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
-- This file implements tests to verify that ticket #1473 has been
-- fixed.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_execsql_test(
    "tkt1473-1.1",
    [[
        CREATE TABLE t1(a INT primary key,b INT);
        INSERT INTO t1 VALUES(1,2);
        INSERT INTO t1 VALUES(3,4);
        SELECT * FROM t1
    ]], {
        -- <tkt1473-1.1>
        1, 2, 3, 4
        -- </tkt1473-1.1>
    })

test:do_execsql_test(
    "tkt1473-1.2",
    [[
        SELECT 1 FROM t1 WHERE a=1 UNION ALL SELECT 2 FROM t1 WHERE b=0
    ]], {
        -- <tkt1473-1.2>
        1
        -- </tkt1473-1.2>
    })

test:do_execsql_test(
    "tkt1473-1.3",
    [[
        SELECT 1 FROM t1 WHERE a=1 UNION SELECT 2 FROM t1 WHERE b=0
    ]], {
        -- <tkt1473-1.3>
        1
        -- </tkt1473-1.3>
    })

test:do_execsql_test(
    "tkt1473-1.4",
    [[
        SELECT 1 FROM t1 WHERE a=1 UNION ALL SELECT 2 FROM t1 WHERE b=4
    ]], {
        -- <tkt1473-1.4>
        1, 2
        -- </tkt1473-1.4>
    })

test:do_execsql_test(
    "tkt1473-1.5",
    [[
        SELECT 1 FROM t1 WHERE a=1 UNION SELECT 2 FROM t1 WHERE b=4
    ]], {
        -- <tkt1473-1.5>
        1, 2
        -- </tkt1473-1.5>
    })

test:do_execsql_test(
    "tkt1473-1.6",
    [[
        SELECT 1 FROM t1 WHERE a=0 UNION ALL SELECT 2 FROM t1 WHERE b=4
    ]], {
        -- <tkt1473-1.6>
        2
        -- </tkt1473-1.6>
    })

test:do_execsql_test(
    "tkt1473-1.7",
    [[
        SELECT 1 FROM t1 WHERE a=0 UNION SELECT 2 FROM t1 WHERE b=4
    ]], {
        -- <tkt1473-1.7>
        2
        -- </tkt1473-1.7>
    })

test:do_execsql_test(
    "tkt1473-1.8",
    [[
        SELECT 1 FROM t1 WHERE a=0 UNION ALL SELECT 2 FROM t1 WHERE b=0
    ]], {
        -- <tkt1473-1.8>

        -- </tkt1473-1.8>
    })

test:do_execsql_test(
    "tkt1473-1.9",
    [[
        SELECT 1 FROM t1 WHERE a=0 UNION SELECT 2 FROM t1 WHERE b=0
    ]], {
        -- <tkt1473-1.9>

        -- </tkt1473-1.9>
    })

-- Everything from this point on depends on sub-queries. So skip it
-- if sub-queries are not available.


test:do_catchsql_test(
    "tkt1473-2.2",
    [[
        SELECT (SELECT 1 FROM t1 WHERE a=1 UNION ALL SELECT 2 FROM t1 WHERE b=0)
    ]], {
        -- <tkt1473-2.2>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-2.2>
    })

test:do_execsql_test(
    "tkt1473-2.3",
    [[
        SELECT (SELECT 1 FROM t1 WHERE a=1 UNION SELECT 2 FROM t1 WHERE b=0)
    ]], {
        -- <tkt1473-2.3>
        1
        -- </tkt1473-2.3>
    })

test:do_catchsql_test(
    "tkt1473-2.4",
    [[
        SELECT (SELECT 1 FROM t1 WHERE a=1 UNION ALL SELECT 2 FROM t1 WHERE b=4)
    ]], {
        -- <tkt1473-2.4>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-2.4>
    })

test:do_catchsql_test(
    "tkt1473-2.5",
    [[
        SELECT (SELECT 1 FROM t1 WHERE a=1 UNION SELECT 2 FROM t1 WHERE b=4)
    ]], {
        -- <tkt1473-2.5>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-2.5>
    })

test:do_catchsql_test(
    "tkt1473-2.6",
    [[
        SELECT (SELECT 1 FROM t1 WHERE a=0 UNION ALL SELECT 2 FROM t1 WHERE b=4)
    ]], {
        -- <tkt1473-2.6>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-2.6>
    })

test:do_execsql_test(
    "tkt1473-2.7",
    [[
        SELECT (SELECT 1 FROM t1 WHERE a=0 UNION SELECT 2 FROM t1 WHERE b=4)
    ]], {
        -- <tkt1473-2.7>
        2
        -- </tkt1473-2.7>
    })

test:do_execsql_test(
    "tkt1473-2.8",
    [[
        SELECT (SELECT 1 FROM t1 WHERE a=0 UNION ALL SELECT 2 FROM t1 WHERE b=0)
    ]], {
        -- <tkt1473-2.8>
        ""
        -- </tkt1473-2.8>
    })

test:do_execsql_test(
    "tkt1473-2.9",
    [[
        SELECT (SELECT 1 FROM t1 WHERE a=0 UNION SELECT 2 FROM t1 WHERE b=0)
    ]], {
        -- <tkt1473-2.9>
        ""
        -- </tkt1473-2.9>
    })

test:do_catchsql_test(
    "tkt1473-3.2",
    [[
        SELECT EXISTS
          (SELECT 1 FROM t1 WHERE a=1 UNION ALL SELECT 2 FROM t1 WHERE b=0)
    ]], {
        -- <tkt1473-3.2>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-3.2>
    })

test:do_execsql_test(
    "tkt1473-3.3",
    [[
        SELECT EXISTS
          (SELECT 1 FROM t1 WHERE a=1 UNION SELECT 2 FROM t1 WHERE b=0)
    ]], {
        -- <tkt1473-3.3>
        true
        -- </tkt1473-3.3>
    })

test:do_catchsql_test(
    "tkt1473-3.4",
    [[
        SELECT EXISTS
          (SELECT 1 FROM t1 WHERE a=1 UNION ALL SELECT 2 FROM t1 WHERE b=4)
    ]], {
        -- <tkt1473-3.4>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-3.4>
    })

test:do_catchsql_test(
    "tkt1473-3.5",
    [[
        SELECT EXISTS
          (SELECT 1 FROM t1 WHERE a=1 UNION SELECT 2 FROM t1 WHERE b=4)
    ]], {
        -- <tkt1473-3.5>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-3.5>
    })

test:do_catchsql_test(
    "tkt1473-3.6",
    [[
        SELECT EXISTS
          (SELECT 1 FROM t1 WHERE a=0 UNION ALL SELECT 2 FROM t1 WHERE b=4)
    ]], {
        -- <tkt1473-3.6>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-3.6>
    })

test:do_execsql_test(
    "tkt1473-3.7",
    [[
        SELECT EXISTS
          (SELECT 1 FROM t1 WHERE a=0 UNION SELECT 2 FROM t1 WHERE b=4)
    ]], {
        -- <tkt1473-3.7>
        true
        -- </tkt1473-3.7>
    })

test:do_execsql_test(
    "tkt1473-3.8",
    [[
        SELECT EXISTS
          (SELECT 1 FROM t1 WHERE a=0 UNION ALL SELECT 2 FROM t1 WHERE b=0)
    ]], {
        -- <tkt1473-3.8>
        false
        -- </tkt1473-3.8>
    })

test:do_execsql_test(
    "tkt1473-3.9",
    [[
        SELECT EXISTS
          (SELECT 1 FROM t1 WHERE a=0 UNION SELECT 2 FROM t1 WHERE b=0)
    ]], {
        -- <tkt1473-3.9>
        false
        -- </tkt1473-3.9>
    })

test:do_execsql_test(
    "tkt1473-4.1",
    [[
        CREATE TABLE t2(x INT primary key,y INT);
        INSERT INTO t2 VALUES(1,2);
        INSERT INTO t2 SELECT x+2, y+2 FROM t2;
        INSERT INTO t2 SELECT x+4, y+4 FROM t2;
        INSERT INTO t2 SELECT x+8, y+8 FROM t2;
        INSERT INTO t2 SELECT x+16, y+16 FROM t2;
        INSERT INTO t2 SELECT x+32, y+32 FROM t2;
        INSERT INTO t2 SELECT x+64, y+64 FROM t2;
        SELECT count(*), sum(x), sum(y) FROM t2;
    ]], {
        -- <tkt1473-4.1>
        64, 4096, 4160
        -- </tkt1473-4.1>
    })

test:do_execsql_test(
    "tkt1473-4.2",
    [[
        SELECT 1 FROM t2 WHERE x=0
        UNION ALL
        SELECT 2 FROM t2 WHERE x=1
        UNION ALL
        SELECT 3 FROM t2 WHERE x=2
        UNION ALL
        SELECT 4 FROM t2 WHERE x=3
        UNION ALL
        SELECT 5 FROM t2 WHERE x=4
        UNION ALL
        SELECT 6 FROM t2 WHERE y=0
        UNION ALL
        SELECT 7 FROM t2 WHERE y=1
        UNION ALL
        SELECT 8 FROM t2 WHERE y=2
        UNION ALL
        SELECT 9 FROM t2 WHERE y=3
        UNION ALL
        SELECT 10 FROM t2 WHERE y=4
    ]], {
        -- <tkt1473-4.2>
        2, 4, 8, 10
        -- </tkt1473-4.2>
    })

test:do_catchsql_test(
    "tkt1473-4.3",
    [[
        SELECT (
          SELECT 1 FROM t2 WHERE x=0
          UNION ALL
          SELECT 2 FROM t2 WHERE x=1
          UNION ALL
          SELECT 3 FROM t2 WHERE x=2
          UNION ALL
          SELECT 4 FROM t2 WHERE x=3
          UNION ALL
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION ALL
          SELECT 7 FROM t2 WHERE y=1
          UNION ALL
          SELECT 8 FROM t2 WHERE y=2
          UNION ALL
          SELECT 9 FROM t2 WHERE y=3
          UNION ALL
          SELECT 10 FROM t2 WHERE y=4
        )
    ]], {
        -- <tkt1473-4.3>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-4.3>
    })

test:do_catchsql_test(
    "tkt1473-4.4",
    [[
        SELECT (
          SELECT 1 FROM t2 WHERE x=0
          UNION ALL
          SELECT 2 FROM t2 WHERE x=-1
          UNION ALL
          SELECT 3 FROM t2 WHERE x=2
          UNION ALL
          SELECT 4 FROM t2 WHERE x=3
          UNION ALL
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION ALL
          SELECT 7 FROM t2 WHERE y=1
          UNION ALL
          SELECT 8 FROM t2 WHERE y=2
          UNION ALL
          SELECT 9 FROM t2 WHERE y=3
          UNION ALL
          SELECT 10 FROM t2 WHERE y=4
        )
    ]], {
        -- <tkt1473-4.4>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-4.4>
    })

test:do_catchsql_test(
    "tkt1473-4.5",
    [[
        SELECT (
          SELECT 1 FROM t2 WHERE x=0
          UNION ALL
          SELECT 2 FROM t2 WHERE x=-1
          UNION ALL
          SELECT 3 FROM t2 WHERE x=2
          UNION ALL
          SELECT 4 FROM t2 WHERE x=-1
          UNION ALL
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION ALL
          SELECT 7 FROM t2 WHERE y=1
          UNION ALL
          SELECT 8 FROM t2 WHERE y=2
          UNION ALL
          SELECT 9 FROM t2 WHERE y=3
          UNION ALL
          SELECT 10 FROM t2 WHERE y=-4
        )
    ]], {
        -- <tkt1473-4.5>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-4.5>
    })

test:do_catchsql_test(
    "tkt1473-4.6",
    [[
        SELECT (
          SELECT 1 FROM t2 WHERE x=0
          UNION ALL
          SELECT 2 FROM t2 WHERE x=-1
          UNION ALL
          SELECT 3 FROM t2 WHERE x=2
          UNION ALL
          SELECT 4 FROM t2 WHERE x=-2
          UNION ALL
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION ALL
          SELECT 7 FROM t2 WHERE y=1
          UNION ALL
          SELECT 8 FROM t2 WHERE y=-3
          UNION ALL
          SELECT 9 FROM t2 WHERE y=3
          UNION ALL
          SELECT 10 FROM t2 WHERE y=4
        )
    ]], {
        -- <tkt1473-4.6>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-4.6>
    })

test:do_execsql_test(
    "tkt1473-4.7",
    [[
        SELECT (
          SELECT 1 FROM t2 WHERE x=0
          UNION ALL
          SELECT 2 FROM t2 WHERE x=-1
          UNION ALL
          SELECT 3 FROM t2 WHERE x=2
          UNION ALL
          SELECT 4 FROM t2 WHERE x=-2
          UNION ALL
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION ALL
          SELECT 7 FROM t2 WHERE y=1
          UNION ALL
          SELECT 8 FROM t2 WHERE y=-3
          UNION ALL
          SELECT 9 FROM t2 WHERE y=3
          UNION ALL
          SELECT 10 FROM t2 WHERE y=-4
        )
    ]], {
        -- <tkt1473-4.7>
        ""
        -- </tkt1473-4.7>
    })

test:do_catchsql_test(
    "tkt1473-5.3",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION ALL
          SELECT 2 FROM t2 WHERE x=1
          UNION ALL
          SELECT 3 FROM t2 WHERE x=2
          UNION ALL
          SELECT 4 FROM t2 WHERE x=3
          UNION ALL
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION ALL
          SELECT 7 FROM t2 WHERE y=1
          UNION ALL
          SELECT 8 FROM t2 WHERE y=2
          UNION ALL
          SELECT 9 FROM t2 WHERE y=3
          UNION ALL
          SELECT 10 FROM t2 WHERE y=4
        )
    ]], {
        -- <tkt1473-5.3>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-5.3>
    })

test:do_catchsql_test(
    "tkt1473-5.4",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION ALL
          SELECT 2 FROM t2 WHERE x=-1
          UNION ALL
          SELECT 3 FROM t2 WHERE x=2
          UNION ALL
          SELECT 4 FROM t2 WHERE x=3
          UNION ALL
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION ALL
          SELECT 7 FROM t2 WHERE y=1
          UNION ALL
          SELECT 8 FROM t2 WHERE y=2
          UNION ALL
          SELECT 9 FROM t2 WHERE y=3
          UNION ALL
          SELECT 10 FROM t2 WHERE y=4
        )
    ]], {
        -- <tkt1473-5.4>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-5.4>
    })

test:do_catchsql_test(
    "tkt1473-5.5",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION ALL
          SELECT 2 FROM t2 WHERE x=-1
          UNION ALL
          SELECT 3 FROM t2 WHERE x=2
          UNION ALL
          SELECT 4 FROM t2 WHERE x=-1
          UNION ALL
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION ALL
          SELECT 7 FROM t2 WHERE y=1
          UNION ALL
          SELECT 8 FROM t2 WHERE y=2
          UNION ALL
          SELECT 9 FROM t2 WHERE y=3
          UNION ALL
          SELECT 10 FROM t2 WHERE y=-4
        )
    ]], {
        -- <tkt1473-5.5>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-5.5>
    })

test:do_catchsql_test(
    "tkt1473-5.6",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION ALL
          SELECT 2 FROM t2 WHERE x=-1
          UNION ALL
          SELECT 3 FROM t2 WHERE x=2
          UNION ALL
          SELECT 4 FROM t2 WHERE x=-2
          UNION ALL
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION ALL
          SELECT 7 FROM t2 WHERE y=1
          UNION ALL
          SELECT 8 FROM t2 WHERE y=-3
          UNION ALL
          SELECT 9 FROM t2 WHERE y=3
          UNION ALL
          SELECT 10 FROM t2 WHERE y=4
        )
    ]], {
        -- <tkt1473-5.6>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-5.6>
    })

test:do_execsql_test(
    "tkt1473-5.7",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION ALL
          SELECT 2 FROM t2 WHERE x=-1
          UNION ALL
          SELECT 3 FROM t2 WHERE x=2
          UNION ALL
          SELECT 4 FROM t2 WHERE x=-2
          UNION ALL
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION ALL
          SELECT 7 FROM t2 WHERE y=1
          UNION ALL
          SELECT 8 FROM t2 WHERE y=-3
          UNION ALL
          SELECT 9 FROM t2 WHERE y=3
          UNION ALL
          SELECT 10 FROM t2 WHERE y=-4
        )
    ]], {
        -- <tkt1473-5.7>
        false
        -- </tkt1473-5.7>
    })

test:do_catchsql_test(
    "tkt1473-6.3",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION
          SELECT 2 FROM t2 WHERE x=1
          UNION
          SELECT 3 FROM t2 WHERE x=2
          UNION
          SELECT 4 FROM t2 WHERE x=3
          UNION
          SELECT 5 FROM t2 WHERE x=4
          UNION
          SELECT 6 FROM t2 WHERE y=0
          UNION
          SELECT 7 FROM t2 WHERE y=1
          UNION
          SELECT 8 FROM t2 WHERE y=2
          UNION
          SELECT 9 FROM t2 WHERE y=3
          UNION
          SELECT 10 FROM t2 WHERE y=4
        )
    ]], {
        -- <tkt1473-6.3>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-6.3>
    })

test:do_catchsql_test(
    "tkt1473-6.4",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION
          SELECT 2 FROM t2 WHERE x=-1
          UNION
          SELECT 3 FROM t2 WHERE x=2
          UNION
          SELECT 4 FROM t2 WHERE x=3
          UNION
          SELECT 5 FROM t2 WHERE x=4
          UNION
          SELECT 6 FROM t2 WHERE y=0
          UNION
          SELECT 7 FROM t2 WHERE y=1
          UNION
          SELECT 8 FROM t2 WHERE y=2
          UNION
          SELECT 9 FROM t2 WHERE y=3
          UNION
          SELECT 10 FROM t2 WHERE y=4
        )
    ]], {
        -- <tkt1473-6.4>
        1, "Failed to execute SQL statement: Expression subquery returned more than 1 row"
        -- </tkt1473-6.4>
    })

test:do_execsql_test(
    "tkt1473-6.5",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION
          SELECT 2 FROM t2 WHERE x=-1
          UNION
          SELECT 3 FROM t2 WHERE x=2
          UNION
          SELECT 4 FROM t2 WHERE x=-1
          UNION
          SELECT 5 FROM t2 WHERE x=4
          UNION
          SELECT 6 FROM t2 WHERE y=0
          UNION
          SELECT 7 FROM t2 WHERE y=1
          UNION
          SELECT 8 FROM t2 WHERE y=2
          UNION
          SELECT 9 FROM t2 WHERE y=3
          UNION
          SELECT 10 FROM t2 WHERE y=-4
        )
    ]], {
        -- <tkt1473-6.5>
        true
        -- </tkt1473-6.5>
    })

test:do_execsql_test(
    "tkt1473-6.6",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION
          SELECT 2 FROM t2 WHERE x=-1
          UNION
          SELECT 3 FROM t2 WHERE x=2
          UNION
          SELECT 4 FROM t2 WHERE x=-2
          UNION
          SELECT 5 FROM t2 WHERE x=4
          UNION
          SELECT 6 FROM t2 WHERE y=0
          UNION
          SELECT 7 FROM t2 WHERE y=1
          UNION
          SELECT 8 FROM t2 WHERE y=-3
          UNION
          SELECT 9 FROM t2 WHERE y=3
          UNION
          SELECT 10 FROM t2 WHERE y=4
        )
    ]], {
        -- <tkt1473-6.6>
        true
        -- </tkt1473-6.6>
    })

test:do_execsql_test(
    "tkt1473-6.7",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION
          SELECT 2 FROM t2 WHERE x=-1
          UNION
          SELECT 3 FROM t2 WHERE x=2
          UNION
          SELECT 4 FROM t2 WHERE x=-2
          UNION
          SELECT 5 FROM t2 WHERE x=4
          UNION
          SELECT 6 FROM t2 WHERE y=0
          UNION
          SELECT 7 FROM t2 WHERE y=1
          UNION
          SELECT 8 FROM t2 WHERE y=-3
          UNION
          SELECT 9 FROM t2 WHERE y=3
          UNION
          SELECT 10 FROM t2 WHERE y=-4
        )
    ]], {
        -- <tkt1473-6.7>
        false
        -- </tkt1473-6.7>
    })

test:do_execsql_test(
    "tkt1473-6.8",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION
          SELECT 2 FROM t2 WHERE x=-1
          UNION
          SELECT 3 FROM t2 WHERE x=2
          UNION
          SELECT 4 FROM t2 WHERE x=-2
          UNION
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION
          SELECT 7 FROM t2 WHERE y=1
          UNION
          SELECT 8 FROM t2 WHERE y=-3
          UNION
          SELECT 9 FROM t2 WHERE y=3
          UNION
          SELECT 10 FROM t2 WHERE y=4
        )
    ]], {
        -- <tkt1473-6.8>
        true
        -- </tkt1473-6.8>
    })

test:do_execsql_test(
    "tkt1473-6.9",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0
          UNION
          SELECT 2 FROM t2 WHERE x=-1
          UNION
          SELECT 3 FROM t2 WHERE x=2
          UNION
          SELECT 4 FROM t2 WHERE x=-2
          UNION
          SELECT 5 FROM t2 WHERE x=4
          UNION ALL
          SELECT 6 FROM t2 WHERE y=0
          UNION
          SELECT 7 FROM t2 WHERE y=1
          UNION
          SELECT 8 FROM t2 WHERE y=-3
          UNION
          SELECT 9 FROM t2 WHERE y=3
          UNION
          SELECT 10 FROM t2 WHERE y=-4
        )
    ]], {
        -- <tkt1473-6.9>
        false
        -- </tkt1473-6.9>
    })

test:do_execsql_test(
    "tkt1473-7.1",
    [[
        SELECT 1 FROM t2 WHERE x=1 EXCEPT SELECT 2 FROM t2 WHERE y=2
    ]], {
        -- <tkt1473-7.1>
        1
        -- </tkt1473-7.1>
    })

test:do_execsql_test(
    "tkt1473-7.2",
    [[
        SELECT (
          SELECT 1 FROM t2 WHERE x=1 EXCEPT SELECT 2 FROM t2 WHERE y=2
        )
    ]], {
        -- <tkt1473-7.2>
        1
        -- </tkt1473-7.2>
    })

test:do_execsql_test(
    "tkt1473-7.3",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=1 EXCEPT SELECT 2 FROM t2 WHERE y=2
        )
    ]], {
        -- <tkt1473-7.3>
        true
        -- </tkt1473-7.3>
    })

test:do_execsql_test(
    "tkt1473-7.4",
    [[
        SELECT (
          SELECT 1 FROM t2 WHERE x=0 EXCEPT SELECT 2 FROM t2 WHERE y=2
        )
    ]], {
        -- <tkt1473-7.4>
        ""
        -- </tkt1473-7.4>
    })

test:do_execsql_test(
    "tkt1473-7.5",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=0 EXCEPT SELECT 2 FROM t2 WHERE y=2
        )
    ]], {
        -- <tkt1473-7.5>
        false
        -- </tkt1473-7.5>
    })

test:do_execsql_test(
    "tkt1473-8.1",
    [[
        SELECT 1 FROM t2 WHERE x=1 INTERSECT SELECT 2 FROM t2 WHERE y=2
    ]], {
        -- <tkt1473-8.1>

        -- </tkt1473-8.1>
    })

test:do_execsql_test(
    "tkt1473-8.1",
    [[
        SELECT 1 FROM t2 WHERE x=1 INTERSECT SELECT 1 FROM t2 WHERE y=2
    ]], {
        -- <tkt1473-8.1>
        1
        -- </tkt1473-8.1>
    })

test:do_execsql_test(
    "tkt1473-8.3",
    [[
        SELECT (
          SELECT 1 FROM t2 WHERE x=1 INTERSECT SELECT 2 FROM t2 WHERE y=2
        )
    ]], {
        -- <tkt1473-8.3>
        ""
        -- </tkt1473-8.3>
    })

test:do_execsql_test(
    "tkt1473-8.4",
    [[
        SELECT (
          SELECT 1 FROM t2 WHERE x=1 INTERSECT SELECT 1 FROM t2 WHERE y=2
        )
    ]], {
        -- <tkt1473-8.4>
        1
        -- </tkt1473-8.4>
    })

test:do_execsql_test(
    "tkt1473-8.5",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=1 INTERSECT SELECT 2 FROM t2 WHERE y=2
        )
    ]], {
        -- <tkt1473-8.5>
        false
        -- </tkt1473-8.5>
    })

test:do_execsql_test(
    "tkt1473-8.6",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=1 INTERSECT SELECT 1 FROM t2 WHERE y=2
        )
    ]], {
        -- <tkt1473-8.6>
        true
        -- </tkt1473-8.6>
    })

test:do_execsql_test(
    "tkt1473-8.7",
    [[
        SELECT (
          SELECT 1 FROM t2 WHERE x=0 INTERSECT SELECT 1 FROM t2 WHERE y=2
        )
    ]], {
        -- <tkt1473-8.7>
        ""
        -- </tkt1473-8.7>
    })

test:do_execsql_test(
    "tkt1473-8.8",
    [[
        SELECT EXISTS (
          SELECT 1 FROM t2 WHERE x=1 INTERSECT SELECT 1 FROM t2 WHERE y=0
        )
    ]], {
        -- <tkt1473-8.8>
        false
        -- </tkt1473-8.8>
    })

test:finish_test()

