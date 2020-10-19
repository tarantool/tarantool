#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(7)

--!./tcltestrunner.lua
-- 2011 July 9
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
-- focus of this file is testing the CREATE INDEX statement.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "index4"
test:do_execsql_test(
    1.1,
    [[
        CREATE TABLE t1(x SCALAR primary key);
        START TRANSACTION;
          INSERT INTO t1 VALUES(randomblob(102));
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --     2
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --     4
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --     8
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --    16
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --    32
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --    64
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --   128
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --   256
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --   512
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --  1024
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --  2048
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --  4096
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     --  8192
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     -- 16384
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     -- 32768
          INSERT INTO t1 SELECT randomblob(102) FROM t1;     -- 65536
        COMMIT;
    ]])

test:do_execsql_test(
    1.2,
    [[
        CREATE INDEX i1 ON t1(x);
    ]])

-- do_execsql_test 1.3 {
--   PRAGMA integrity_check 
-- } {ok}
-- The same test again - this time with limited memory.
--

-- MUST_WORK_TEST limit memory
--soft_limit = sql_soft_heap_limit(50000)
--db("close")
--sql("db", "test.db")
--test:do_execsql_test(
--    1.4,
--    [[
--        PRAGMA cache_size = 10;
--        CREATE INDEX i2 ON t1(x);
--    ]])
--
---- do_execsql_test 1.5 {
----   PRAGMA integrity_check
---- } {ok}
--sql_soft_heap_limit(soft_limit)


-- Tarantool: x is PK now, so NULL is not allowed.
test:do_execsql_test(
    1.6,
    [[
          DROP TABLE t1;
          CREATE TABLE t1(x SCALAR primary key);
        START TRANSACTION;
          INSERT INTO t1 VALUES('a');
          INSERT INTO t1 VALUES('b');
          INSERT INTO t1 VALUES('c');
          INSERT INTO t1 VALUES('d');
          INSERT INTO t1 VALUES('e');
          INSERT INTO t1 VALUES('f');
          INSERT INTO t1 VALUES('g');
          -- INSERT INTO t1 VALUES(NULL);
          INSERT INTO t1 SELECT randomblob(1202) FROM t1;     --    16
          INSERT INTO t1 SELECT randomblob(2202) FROM t1;     --    32
          INSERT INTO t1 SELECT randomblob(3202) FROM t1;     --    64
          INSERT INTO t1 SELECT randomblob(4202) FROM t1;     --   128
          INSERT INTO t1 SELECT randomblob(5202) FROM t1;     --   256
        COMMIT;
        CREATE INDEX i1 ON t1(x); 
        --PRAGMA integrity_check
    ]], {
        -- <1.6>
        
        -- </1.6>
    })

test:do_execsql_test(
    1.7,
    [[
        --START TRANSACTION;
          DROP TABLE t1;
          CREATE TABLE t1(x TEXT primary key);
          INSERT INTO t1 VALUES('a');
        --COMMIT;
        CREATE INDEX i1 ON t1(x); 
        --PRAGMA integrity_check
    ]], {
        -- <1.7>
        
        -- </1.7>
    })

-- MUST_WORK_TEST
if (1 > 0)
 then
    test:do_execsql_test(
        1.8,
        [[
            --START TRANSACTION;
              DROP TABLE t1;
              CREATE TABLE t1(x INT primary key);
            --COMMIT;
            CREATE INDEX i1 ON t1(x); 
            --PRAGMA integrity_check
        ]], {
            -- <1.8>
            
            -- </1.8>
        })

end
test:do_execsql_test(
    2.1,
    [[
          CREATE TABLE t2(id INT primary key, x INT);
        START TRANSACTION;
          INSERT INTO t2 VALUES(1, 14);
          INSERT INTO t2 VALUES(2, 35);
          INSERT INTO t2 VALUES(3, 15);
          INSERT INTO t2 VALUES(4, 35);
          INSERT INTO t2 VALUES(5, 16);
        COMMIT;
    ]])

test:do_catchsql_test(
    2.2,
    [[
        CREATE UNIQUE INDEX i3 ON t2(x);
    ]], {
        -- <2.2>
        1, "Duplicate key exists in unique index 'I3' in space 'T2'"
        -- </2.2>
    })


test:finish_test()
