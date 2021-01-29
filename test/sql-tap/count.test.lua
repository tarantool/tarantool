#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(30)

--!./tcltestrunner.lua
-- 2009-02-24
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.  The
-- focus of this file is testing "SELECT count(*)" statements.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Test plan:
--
--  count-0.*: Make sure count(*) works on an empty database.  (Ticket #3774)
--
--  count-1.*: Test that the OP_Count instruction appears to work on both
--             tables and indexes. Test both when they contain 0 entries,
--             when all entries are on the root page, and when the b-tree
--             forms a structure 2 and 3 levels deep.
--
--
-- do_test count-0.1 {
--   db eval {
--      SELECT count(*) FROM sql_master;
--   }
-- } {0}
local iTest = 0
local queries = {
    "/* no-op */",
    "CREATE INDEX i1 ON t1(a);"
}
for _, zIndex in ipairs(queries) do
    iTest = iTest + 1
    test:do_test(
        "count-1."..iTest..".1",
        function()
            test:execsql [[
                DROP TABLE IF EXISTS t1;
                CREATE TABLE t1(a INT , b INT , PRIMARY KEY(a,b));
            ]]
            test:execsql(zIndex)
            return test:execsql(" SELECT count(*) FROM t1 ")
        end, {
            0
        })

    test:do_execsql_test(
        "count-1."..iTest..".2",
        [[
            INSERT INTO t1 VALUES(1, 2);
            INSERT INTO t1 VALUES(2, 4);
            SELECT count(*) FROM t1;
        ]], {
            2
        })

    test:do_execsql_test(
        "count-1."..iTest..".3",
        [[
            INSERT INTO t1 SELECT a+2, b FROM t1;          --   4
            INSERT INTO t1 SELECT a+4, b FROM t1;          --   8
            INSERT INTO t1 SELECT a+8, b FROM t1;          --  16
            INSERT INTO t1 SELECT a+16, b FROM t1;          --  32
            INSERT INTO t1 SELECT a+32, b FROM t1;          --  64
            INSERT INTO t1 SELECT a+64, b FROM t1;          -- 128
            INSERT INTO t1 SELECT a+128, b FROM t1;          -- 256
            SELECT count(*) FROM t1;
        ]], {
            256
        })

    test:do_execsql_test(
        "count-1."..iTest..".4",
        [[
            INSERT INTO t1 SELECT a+256, b FROM t1;          --  512
            INSERT INTO t1 SELECT a+512, b FROM t1;          -- 1024
            INSERT INTO t1 SELECT a+1024, b FROM t1;          -- 2048
            INSERT INTO t1 SELECT a+2048, b FROM t1;          -- 4096
            SELECT count(*) FROM t1;
        ]], {
            4096
        })

    test:do_execsql_test(
        "count-1."..iTest..".5",
        [[
            START TRANSACTION;
            INSERT INTO t1 SELECT a+4096, b FROM t1;          --  8192
            INSERT INTO t1 SELECT a+8192, b FROM t1;          -- 16384
            INSERT INTO t1 SELECT a+16384, b FROM t1;          -- 32768
            INSERT INTO t1 SELECT a+32768, b FROM t1;          -- 65536
            COMMIT;
            SELECT count(*) FROM t1;
        ]], {
            65536
        })

end
local function uses_op_count(sql)
    if test:lsearch(test:execsql("EXPLAIN "..sql), "Count")>0 then
        return 1
    end
        return 0
end

test:do_test(
    "count-2.1",
    function()
        test:execsql [[
            CREATE TABLE t2(a INT , b INT , PRIMARY KEY(a,b));
        ]]
        return uses_op_count("SELECT count(*) FROM t2")
    end,1)

test:do_catchsql_test(
    "count-2.2",
    [[
        SELECT count(DISTINCT *) FROM t2
    ]], {
        -- <count-2.2>
        1, [[Syntax error at line 1 near '*']]
        -- </count-2.2>
    })

test:do_test(
    "count-2.3",
    function()
        return uses_op_count("SELECT count(DISTINCT a) FROM t2")
    end,0)

test:do_test(
    "count-2.4",
    function()
        return uses_op_count("SELECT count(a) FROM t2")
    end, 0)

test:do_test(
    "count-2.5",
    function()
        return uses_op_count("SELECT count() FROM t2")
    end, 1)

test:do_catchsql_test(
    "count-2.6",
    [[
        SELECT count(DISTINCT) FROM t2
    ]], {
        -- <count-2.6>
        1, "DISTINCT aggregates must have exactly one argument"
        -- </count-2.6>
    })

test:do_test(
    "count-2.7",
    function()
        return uses_op_count("SELECT count(*)+1 FROM t2")
    end, 0)

test:do_test(
    "count-2.8",
    function()
        return uses_op_count("SELECT count(*) FROM t2 WHERE a IS NOT NULL")
    end, 0)

test:do_execsql_test(
    "count-2.9",
    [[
        SELECT count(*) FROM t2 HAVING count(*)>1
    ]],
        -- <count-2.9>
        {}
        -- </count-2.9>
    )

test:do_test(
    "count-2.10",
    function()
        return uses_op_count("SELECT count(*) FROM (SELECT 1)")
    end, 0)

test:do_test(
    "count-2.11",
    function()
        test:execsql " CREATE VIEW v1 AS SELECT 1 AS a "
        return uses_op_count("SELECT count(*) FROM v1")
    end, 0)

test:do_test(
    "count-2.12",
    function()
        return uses_op_count("SELECT count(*), max(a) FROM t2")
    end, 0)

test:do_test(
    "count-2.13",
    function()
        return uses_op_count("SELECT count(*) FROM t1, t2")
    end, 0)

-- ifcapable vtab {
--   register_echo_module [sql_connection_pointer db]
--   do_test count-2.14 {
--     execsql { CREATE VIRTUAL TABLE techo USING echo(t1); }
--     uses_op_count {SELECT count(*) FROM techo}
--   } {0}
-- }
test:do_execsql_test(
    "count-3.1",
    [[
        CREATE TABLE t3(a INT , b INT , PRIMARY KEY(a,b));
        SELECT a FROM (SELECT count(*) AS a FROM t3) WHERE a==0;
    ]], {
        -- <count-3.1>
        0
        -- </count-3.1>
    })

test:do_execsql_test(
    "count-3.2",
    [[
        SELECT a FROM (SELECT count(*) AS a FROM t3) WHERE a==1;
    ]], {
        -- <count-3.2>

        -- </count-3.2>
    })

test:do_execsql_test(
    "count-4.1",
    [[
        CREATE TABLE t4(a TEXT PRIMARY KEY, b TEXT );
        INSERT INTO t4 VALUES('a', 'b');
        CREATE INDEX t4i1 ON t4(b, a);
        SELECT count(*) FROM t4;
    ]], {
        -- <count-4.1>
        1
        -- </count-4.1>
    })

test:do_execsql_test(
    "count-4.2",
    [[
        CREATE INDEX t4i2 ON t4(b);
        SELECT count(*) FROM t4;
    ]], {
        -- <count-4.2>
        1
        -- </count-4.2>
    })

test:do_execsql_test(
    "count-4.3",
    [[
        DROP INDEX t4i1 ON t4;
        CREATE INDEX t4i1 ON t4(b, a);
        SELECT count(*) FROM t4;
    ]], {
        -- <count-4.3>
        1
        -- </count-4.3>
    })

test:do_execsql_test(
    "count-5.1",
    [[
        CREATE TABLE t5(a TEXT PRIMARY KEY, b VARCHAR(50));
        INSERT INTO t5 VALUES('bison','jazz');
        SELECT count(*) FROM t5;
    ]], {
        -- <count-5.1>
        1
        -- </count-5.1>
    })

test:do_catchsql_test(
    "count-6.1",
    [[
        CREATE TABLE t6(x  INT PRIMARY KEY);
        SELECT count(DISTINCT) FROM t6 GROUP BY x;
    ]], {
        -- <count-6.1>
        1, "DISTINCT aggregates must have exactly one argument"
        -- </count-6.1>
    })



test:finish_test()
