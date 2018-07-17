#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(70)

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
-- This file implements regression tests for SQLite library.  The
-- focus of this file is testing the CREATE INDEX statement.
--
-- $Id: index.test,v 1.43 2008/01/16 18:20:42 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Create a basic index and verify it is added to sqlite_master
--
test:do_test(
    "index-1.1",
    function()
        test:execsql "CREATE TABLE test1(id primary key, f1 int, f2 int, f3 int)"
        test:execsql "CREATE INDEX index1 ON test1(f1)"
        return test:execsql [[SELECT "name" FROM "_space" WHERE "name"='TEST1']]
    end, {
        -- <index-1.1>
        "TEST1"
        -- </index-1.1>
    })

test:do_execsql_test(
    "index-1.1.1",
    [[
        SELECT "name" FROM "_index" WHERE "name"='INDEX1'
    ]], {
        -- <index-1.1.1>
        "INDEX1"
        -- </index-1.1.1>
    })

-- do_test index-1.1b {
--   execsql {SELECT name, sql, tbl_name, type FROM sqlite_master 
--            WHERE name='index1'}
-- } {index1 {CREATE INDEX index1 ON test1(f1)} test1 index}
--do_test index-1.1c {
--  db close
--  sqlite3 db test.db
--  execsql {SELECT name FROM _index WHERE name='index1'}
--} {index1}
-- execsql {SELECT name, sql, tbl_name, type FROM sqlite_master 
--          WHERE name='index1'}
--} {index1 {CREATE INDEX index1 ON test1(f1)} test1 index}
--do_test index-1.1d {
--  db close
--  sqlite3 db test.db
--  execsql {SELECT name FROM _space WHERE name='test1'}
--execsql {SELECT name FROM sqlite_master WHERE type!='meta' ORDER BY name}
--} {test1}
-- Verify that the index dies with the table
--
test:do_test(
    "index-1.2",
    function()
        test:execsql "DROP TABLE test1"
        return test:execsql [[SELECT "name" FROM "_space" WHERE "name"='TEST1']]
        --execsql {SELECT name FROM sqlite_master WHERE type!='meta' ORDER BY name}
    end, {
        -- <index-1.2>
        
        -- </index-1.2>
    })

-- Try adding an index to a table that does not exist
--
test:do_catchsql_test(
    "index-2.1",
    [[
        CREATE INDEX index1 ON test1(f1)
    ]], {
        -- <index-2.1>
        1, "no such table: TEST1"
        -- </index-2.1>
    })

-- Try adding an index on a column of a table where the table
-- exists but the column does not.
--
test:do_test(
    "index-2.1b",
    function()
        test:execsql "CREATE TABLE test1(id primary key, f1 int, f2 int, f3 int)"
        return test:catchsql "CREATE INDEX index1 ON test1(f4)"
    end, {
        -- <index-2.1b>
        1, "no such column: F4"
        -- </index-2.1b>
    })

-- Try an index with some columns that match and others that do now.
--
test:do_test(
    "index-2.2",
    function()
        local msg
        local v , msg= pcall(function()
            test:execsql("CREATE INDEX index1 ON test1(f1, f2, f4, f3)")
            end)
        v = v == true and {0} or {1}
        test:execsql("DROP TABLE test1")
        return table.insert(v,msg) or v
    end, {
        -- <index-2.2>
        1, "no such column: F4"
        -- </index-2.2>
    })

-- MUST_WORK_TEST REINDEX and integrity_check
if (0 > 0)
 then
    -- Try creating a bunch of indices on the same table
    --
    local r = {}
    for i = 1, 99, 1 do
        table.insert(r,string.format("index%02d", i))
    end
    test:do_test(
        "index-3.1",
        function()
            test:execsql("CREATE TABLE test1(f1 int primary key, f2 int, f3 int, f4 int, f5 int)")
            for i = 1, 99, 1 do
                local sql = string.format("CREATE INDEX %s ON test1(f%s)", string.format("index%02d", i), (i%5)+1)
                test:execsql(sql)
            end
            return test:execsql [[SELECT name FROM sqlite_master 
              WHERE type='index' AND tbl_name='test1'
              ORDER BY name]]
        end, {
            -- <index-3.1>
            r
            -- </index-3.1>
        })

    X(104, "X!cmd", [=[["integrity_check","index-3.2.1"]]=])
    test:do_execsql_test(
        "index-3.2.2",
        [[
            REINDEX
        ]], {
            -- <index-3.2.2>
            
            -- </index-3.2.2>
        })



    --X(110, "X!cmd", [=[["integrity_check","index-3.2.3"]]=])
    -- Verify that all the indices go away when we drop the table.
    --
    test:do_test(
        "index-3.3",
        function()
            test:execsql "DROP TABLE test1"
            return test:execsql [[SELECT name FROM sqlite_master 
              WHERE type='index' AND tbl_name='test1'
              ORDER BY name]]
        end, {
            -- <index-3.3>
            
            -- </index-3.3>
        })

    -- Create a table and insert values into that table. Then create
    -- an index on that table. Verify that we can select values
    -- from the table correctly using the index
    -- Note that the index names index9 and indext are chosen because
    -- they both have the same hash.
end
test:do_test(
    "index-4.1",
    function()
        test:execsql "CREATE TABLE test1(id primary key, cnt int, power int)"
        local val = 2
        for i = 1, 19, 1 do
            test:execsql(string.format("INSERT INTO test1 VALUES(%s, %s,%s)", i, i, val))
            val = val * 2
        end
        test:execsql "CREATE INDEX index9 ON test1(cnt)"
        test:execsql "CREATE INDEX indext ON test1(power)"
        return test:execsql [[SELECT "name" FROM "_index" WHERE "name"='INDEX9' OR "name"='INDEXT' union SELECT "name" FROM "_space" WHERE "name"='TEST1';]]
    end, {
        -- <index-4.1>
        "INDEX9", "INDEXT", "TEST1"
        -- </index-4.1>
    })

test:do_execsql_test(
    "index-4.2",
    [[
        SELECT cnt FROM test1 WHERE power=4
    ]], {
        -- <index-4.2>
        2
        -- </index-4.2>
    })

test:do_execsql_test(
    "index-4.3",
    [[
        SELECT cnt FROM test1 WHERE power=1024
    ]], {
        -- <index-4.3>
        10
        -- </index-4.3>
    })

test:do_execsql_test(
    "index-4.4",
    [[
        SELECT power FROM test1 WHERE cnt=6
    ]], {
        -- <index-4.4>
        64
        -- </index-4.4>
    })

test:do_test(
    "index-4.5",
    function()
        test:execsql [[DROP INDEX indext ON test1]]
        return test:execsql "SELECT power FROM test1 WHERE cnt=6"
    end, {
        -- <index-4.5>
        64
        -- </index-4.5>
    })

test:do_execsql_test(
    "index-4.6",
    [[
        SELECT cnt FROM test1 WHERE power=1024
    ]], {
        -- <index-4.6>
        10
        -- </index-4.6>
    })

test:do_test(
    "index-4.7",
    function()
        test:execsql "CREATE INDEX indext ON test1(cnt)"
        return test:execsql "SELECT power FROM test1 WHERE cnt=6"
    end, {
        -- <index-4.7>
        64
        -- </index-4.7>
    })

test:do_execsql_test(
    "index-4.8",
    [[
        SELECT cnt FROM test1 WHERE power=1024
    ]], {
        -- <index-4.8>
        10
        -- </index-4.8>
    })

test:do_test(
    "index-4.9",
    function()
        test:execsql [[DROP INDEX index9 ON test1]]
        return test:execsql "SELECT power FROM test1 WHERE cnt=6"
    end, {
        -- <index-4.9>
        64
        -- </index-4.9>
    })

test:do_execsql_test(
    "index-4.10",
    [[
        SELECT cnt FROM test1 WHERE power=1024
    ]], {
        -- <index-4.10>
        10
        -- </index-4.10>
    })

test:do_test(
    "index-4.11",
    function()
        test:execsql [[DROP INDEX indext ON test1]]
        return test:execsql "SELECT power FROM test1 WHERE cnt=6"
    end, {
        -- <index-4.11>
        64
        -- </index-4.11>
    })

test:do_execsql_test(
    "index-4.12",
    [[
        SELECT cnt FROM test1 WHERE power=1024
    ]], {
        -- <index-4.12>
        10
        -- </index-4.12>
    })

test:do_test(
    "index-4.13",
    function()
        test:execsql "DROP TABLE test1"
        return test:execsql [[SELECT "name" FROM "_space" WHERE "name"='TEST1']]
        --execsql {SELECT name FROM sqlite_master WHERE type!='meta' ORDER BY name}
    end, {
        -- <index-4.13>
        
        -- </index-4.13>
    })

-- integrity_check index-4.14
-- # Do not allow indices to be added to sqlite_master
-- #
-- do_test index-5.1 {
--   set v [catch {execsql {CREATE INDEX index1 ON sqlite_master(name)}} msg]
--   lappend v $msg
-- } {1 {table sqlite_master may not be indexed}}
-- do_test index-5.2 {
--   execsql {SELECT name FROM sqlite_master WHERE type!='meta'}
-- } {}
-- Do not allow indices with duplicate names to be added
--
test:do_test(
    "index-6.1",
    function()
        test:execsql "CREATE TABLE test1(id primary key, f1 int, f2 int)"
        test:execsql "CREATE TABLE test2(id primary key, g1 real, g2 real)"
        return test:execsql "CREATE INDEX index1 ON test1(f1)"
    end, {
        -- <index-6.1>
        
        -- </index-6.1>
    })

--set v [catch {execsql {CREATE INDEX index1 ON test2(g1)}} msg]
--lappend v $msg
--} {1 {index index1 already exists}}
-- do_test index-6.1.1 {
--   catchsql {CREATE INDEX [index1] ON test2(g1)}
-- } {1 {index index1 already exists}}
test:do_execsql_test(
    "index-6.1b",
    [[
        SELECT "name" FROM "_index" WHERE "name"='INDEX1' union SELECT "name" FROM "_space" WHERE "name"='TEST1' OR "name"='TEST2'
    ]], {
        -- <index-6.1b>
        "INDEX1", "TEST1", "TEST2"
        -- </index-6.1b>
    })

test:do_catchsql_test(
    "index-6.1c",
    [[
        CREATE INDEX IF NOT EXISTS index1 ON test1(f1)
    ]], {
        -- <index-6.1c>
        0
        -- </index-6.1c>
    })

test:do_execsql_test(
    "index-6.2",
    [[
        SELECT "name" FROM "_index" WHERE "name"='INDEX1' union SELECT "name" FROM "_space" WHERE "name"='TEST1' OR "name"='TEST2'
    ]], {
        -- <index-6.2>
        "INDEX1", "TEST1", "TEST2"
        -- </index-6.2>
    })

test:do_test(
    "index-6.3",
    function()
        test:execsql "DROP TABLE test1"
        test:execsql "DROP TABLE test2"
        return test:execsql [[SELECT "name" FROM "_space" WHERE "name"='TEST1' OR "name"='TEST2']]
        --execsql {SELECT name FROM sqlite_master WHERE type!='meta' ORDER BY name}
    end, {
        -- <index-6.3>
        
        -- </index-6.3>
    })

test:do_execsql_test(
    "index-6.4",
    [[
        CREATE TABLE test1(id primary key, a,b);
        CREATE INDEX index1 ON test1(a);
        CREATE INDEX index2 ON test1(b);
        CREATE INDEX index3 ON test1(a,b);
        DROP TABLE test1;
        SELECT "name" FROM "_space" WHERE "name"='TEST1';
    ]], {
        -- <index-6.4>
        
        -- </index-6.4>
    })

-- integrity_check index-6.5
-- Create a primary key
--
test:do_test(
    "index-7.1",
    function()
        test:execsql "CREATE TABLE test1(f1 int, f2 int primary key)"
        local val = 2
        for i = 1, 19, 1 do
            test:execsql(string.format("INSERT INTO test1 VALUES(%s,%s)", i, val))
            val = val * 2
        end
        return test:execsql "SELECT count(*) FROM test1"
    end, {
        -- <index-7.1>
        19
        -- </index-7.1>
    })

test:do_execsql_test(
    "index-7.2",
    [[
        SELECT f1 FROM test1 WHERE f2=65536
    ]], {
        -- <index-7.2>
        16
        -- </index-7.2>
    })

test:do_execsql_test(
    "index-7.3",
    [[
        SELECT "name" FROM "_index" WHERE "name"='pk_unnamed_TEST1_1'
    ]], {
        -- <index-7.3>
        "pk_unnamed_TEST1_1"
        -- </index-7.3>
    })

test:do_test(
    "index-7.4",
    function()
        test:execsql "DROP table test1"
        return test:execsql [[SELECT "name" FROM "_space" WHERE "name"='TEST1']]
    end, {
        -- <index-7.4>
        
        -- </index-7.4>
    })

-- integrity_check index-7.5
-- Make sure we cannot drop a non-existant index.
--
test:do_catchsql_test(
    "index-8.1",
    [[
    	CREATE TABLE test1(a INT PRIMARY KEY);
        DROP INDEX index1 ON test1
    ]], {
        -- <index-8.1>
        1, "no such index: TEST1.INDEX1"
        -- </index-8.1>
    })

test:execsql("DROP TABLE IF EXISTS test1")

-- Make sure we don't actually create an index when the EXPLAIN keyword
-- is used.
--
test:do_test(
    "index-9.1",
    function()
        test:execsql "CREATE TABLE tab1(id primary key, a int)"
        test:execsql "EXPLAIN CREATE INDEX idx1 ON tab1(a)"


        return test:execsql [[SELECT "name" FROM "_space" WHERE "name"='TAB1']]
    end, {
        -- <index-9.1>
        "TAB1"
        -- </index-9.1>
    })

test:do_test(
    "index-9.2",
    function()
        test:execsql "CREATE INDEX idx1 ON tab1(a)"
        return test:execsql [[SELECT "name" FROM "_index" WHERE "name"='IDX1' union SELECT "name" FROM "_space" WHERE "name"='TAB1']]
    end, {
        -- <index-9.2>
        "IDX1", "TAB1"
        -- </index-9.2>
    })

-- integrity_check index-9.3
-- Allow more than one entry with the same key.
--
test:do_execsql_test(
    "index-10.0",
    [[
        CREATE TABLE t1(id primary key, a int, b int);
        CREATE INDEX i1 ON t1(a);
        INSERT INTO t1 VALUES(1, 1,2);
        INSERT INTO t1 VALUES(2, 2,4);
        INSERT INTO t1 VALUES(3, 3,8);
        INSERT INTO t1 VALUES(4, 1,12);
        SELECT b FROM t1 WHERE a=1 ORDER BY b;
    ]], {
        -- <index-10.0>
        2, 12
        -- </index-10.0>
    })

test:do_execsql_test(
    "index-10.1",
    [[
        SELECT b FROM t1 WHERE a=2 ORDER BY b;
    ]], {
        -- <index-10.1>
        4
        -- </index-10.1>
    })

test:do_execsql_test(
    "index-10.2",
    [[
        DELETE FROM t1 WHERE b=12;
        SELECT b FROM t1 WHERE a=1 ORDER BY b;
    ]], {
        -- <index-10.2>
        2
        -- </index-10.2>
    })

test:do_execsql_test(
    "index-10.3",
    [[
        DELETE FROM t1 WHERE b=2;
        SELECT b FROM t1 WHERE a=1 ORDER BY b;
    ]], {
        -- <index-10.3>
        
        -- </index-10.3>
    })

test:do_execsql_test(
    "index-10.4",
    [[
        DELETE FROM t1;
        INSERT INTO t1 VALUES (1, 1,1);
        INSERT INTO t1 VALUES (2, 1,2);
        INSERT INTO t1 VALUES (3, 1,3);
        INSERT INTO t1 VALUES (4, 1,4);
        INSERT INTO t1 VALUES (5, 1,5);
        INSERT INTO t1 VALUES (6, 1,6);
        INSERT INTO t1 VALUES (7, 1,7);
        INSERT INTO t1 VALUES (8, 1,8);
        INSERT INTO t1 VALUES (9, 1,9);
        INSERT INTO t1 VALUES (10, 2,0);
        SELECT b FROM t1 WHERE a=1 ORDER BY b;
    ]], {
        -- <index-10.4>
        1, 2, 3, 4, 5, 6, 7, 8, 9
        -- </index-10.4>
    })

test:do_test(
    "index-10.5",
    function()
        test:execsql " DELETE FROM t1 WHERE b IN (2, 4, 6, 8); "


        return test:execsql [[
            SELECT b FROM t1 WHERE a=1 ORDER BY b;
        ]]
    end, {
        -- <index-10.5>
        1, 3, 5, 7, 9
        -- </index-10.5>
    })

test:do_execsql_test(
    "index-10.6",
    [[
        DELETE FROM t1 WHERE b>2;
        SELECT b FROM t1 WHERE a=1 ORDER BY b;
    ]], {
        -- <index-10.6>
        1
        -- </index-10.6>
    })

test:do_execsql_test(
    "index-10.7",
    [[
        DELETE FROM t1 WHERE b=1;
        SELECT b FROM t1 WHERE a=1 ORDER BY b;
    ]], {
        -- <index-10.7>
        
        -- </index-10.7>
    })

test:do_execsql_test(
    "index-10.8",
    [[
        SELECT b FROM t1 ORDER BY b;
    ]], {
        -- <index-10.8>
        0
        -- </index-10.8>
    })

-- integrity_check index-10.9
-- Automatically create an index when we specify a primary key.
--
-- Tarantool: WITHOUT ROWID is by default, so search count is less
-- by one. Expected result changed {0.1 2} -> {0.1 1}
-- MUST_WORK_TEST uses internal variables (sqlite_search_count)
if 0>0 then
test:do_test(
    "index-11.1",
    function()
        test:execsql [[
            CREATE TABLE t3(
              a text,
              b int,
              c float,
              PRIMARY KEY(b)
            );
        ]]
        for i = 1, 50, 1 do
            test:execsql(string.format("INSERT INTO t3 VALUES('x%sx',%s,0.%s)", i, i, i))
        end
        sqlite_search_count = 0
        return X(381, "X!cmd", [=[["concat",[["execsql","SELECT c FROM t3 WHERE b==10"]],["sqlite_search_count"]]]=])
    end, {
        -- <index-11.1>
        0.1, 1
        -- </index-11.1>
    })
end
-- integrity_check index-11.2
-- Numeric strings should compare as if they were numbers.  So even if the
-- strings are not character-by-character the same, if they represent the
-- same number they should compare equal to one another.  Verify that this
-- is true in indices.
--
-- Updated for sqlite3 v3: SQLite will now store these values as numbers
-- (because the affinity of column a is NUMERIC) so the quirky
-- representations are not retained. i.e. '+1.0' becomes '1'.
test:do_execsql_test(
    "index-12.1",
    [[
        CREATE TABLE t4(id primary key, a NUM,b);
        INSERT INTO t4 VALUES(1, '0.0',1);
        INSERT INTO t4 VALUES(2, '0.00',2);
        INSERT INTO t4 VALUES(3, 'abc',3);
        INSERT INTO t4 VALUES(4, '-1.0',4);
        INSERT INTO t4 VALUES(5, '+1.0',5);
        INSERT INTO t4 VALUES(6, '0',6);
        INSERT INTO t4 VALUES(7, '00000',7);
        SELECT a FROM t4 ORDER BY b;
    ]], {
        -- <index-12.1>
        0, 0, "abc", -1, 1, 0, 0
        -- </index-12.1>
    })

test:do_execsql_test(
    "index-12.2",
    [[
        SELECT a FROM t4 WHERE a==0 ORDER BY b
    ]], {
        -- <index-12.2>
        0, 0, 0, 0
        -- </index-12.2>
    })

test:do_execsql_test(
    "index-12.3",
    [[
        SELECT a FROM t4 WHERE a<0.5 ORDER BY b
    ]], {
        -- <index-12.3>
        0, 0, -1, 0, 0
        -- </index-12.3>
    })

test:do_execsql_test(
    "index-12.4",
    [[
        SELECT a FROM t4 WHERE a>-0.5 ORDER BY b
    ]], {
        -- <index-12.4>
        0, 0, "abc", 1, 0, 0
        -- </index-12.4>
    })

test:do_execsql_test(
    "index-12.5",
    [[
        CREATE INDEX t4i1 ON t4(a);
        SELECT a FROM t4 WHERE a==0 ORDER BY b
    ]], {
        -- <index-12.5>
        0, 0, 0, 0
        -- </index-12.5>
    })

test:do_execsql_test(
    "index-12.6",
    [[
        SELECT a FROM t4 WHERE a<0.5 ORDER BY b
    ]], {
        -- <index-12.6>
        0, 0, -1, 0, 0
        -- </index-12.6>
    })

test:do_execsql_test(
    "index-12.7",
    [[
        SELECT a FROM t4 WHERE a>-0.5 ORDER BY b
    ]], {
        -- <index-12.7>
        0, 0, "abc", 1, 0, 0
        -- </index-12.7>
    })

-- integrity_check index-12.8
-- Make sure we cannot drop an automatically created index.
--
test:do_execsql_test(
    "index-13.1",
    [[
        CREATE TABLE t5(
           a int UNIQUE,
           b float PRIMARY KEY,
           c varchar(10),
           UNIQUE(a,c)
        );
        INSERT INTO t5 VALUES(1,2,3);
        SELECT * FROM t5;
    ]], {
        -- <index-13.1>
        1, 2.0, "3"
        -- </index-13.1>
    })

-- do_test index-13.2 {
--   set ::idxlist [execsql {
--     SELECT name FROM sqlite_master WHERE type="index" AND tbl_name="t5";
--   }]
--   llength $::idxlist
-- } {3}
-- for {set i 0} {$i<[llength $::idxlist]} {incr i} {
--   do_test index-13.3.$i {
--     catchsql "
--       DROP INDEX '[lindex $::idxlist $i]';
--     "
--   } {1 {index associated with UNIQUE or PRIMARY KEY constraint cannot be dropped}}
-- }
-- do_test index-13.4 {
--   execsql {
--     INSERT INTO t5 VALUES('a','b','c');
--     SELECT * FROM t5;
--   }
-- } {1 2.0 3 a b c}
-- integrity_check index-13.5
-- Check the sort order of data in an index.
--
test:do_execsql_test(
    "index-14.1",
    [[
        CREATE TABLE t6(id primary key, a,b,c);
        CREATE INDEX t6i1 ON t6(a,b);
        INSERT INTO t6 VALUES(1, '','',1);
        INSERT INTO t6 VALUES(2, '',NULL,2);
        INSERT INTO t6 VALUES(3, NULL,'',3);
        INSERT INTO t6 VALUES(4, 'abc',123,4);
        INSERT INTO t6 VALUES(5, 123,'abc',5);
        SELECT c FROM t6 ORDER BY a,b;
    ]], {
        -- <index-14.1>
        3, 5, 2, 1, 4
        -- </index-14.1>
    })

test:do_execsql_test(
    "index-14.2",
    [[
        SELECT c FROM t6 WHERE a='';
    ]], {
        -- <index-14.2>
        2, 1
        -- </index-14.2>
    })

test:do_execsql_test(
    "index-14.3",
    [[
        SELECT c FROM t6 WHERE b='';
    ]], {
        -- <index-14.3>
        1, 3
        -- </index-14.3>
    })

test:do_execsql_test(
    "index-14.4",
    [[
        SELECT c FROM t6 WHERE a>'';
    ]], {
        -- <index-14.4>
        4
        -- </index-14.4>
    })

test:do_execsql_test(
    "index-14.5",
    [[
        SELECT c FROM t6 WHERE a>='';
    ]], {
        -- <index-14.5>
        2, 1, 4
        -- </index-14.5>
    })

test:do_execsql_test(
    "index-14.6",
    [[
        SELECT c FROM t6 WHERE a>123;
    ]], {
        -- <index-14.6>
        2, 1, 4
        -- </index-14.6>
    })

test:do_execsql_test(
    "index-14.7",
    [[
        SELECT c FROM t6 WHERE a>=123;
    ]], {
        -- <index-14.7>
        5, 2, 1, 4
        -- </index-14.7>
    })

test:do_execsql_test(
    "index-14.8",
    [[
        SELECT c FROM t6 WHERE a<'abc';
    ]], {
        -- <index-14.8>
        5, 2, 1
        -- </index-14.8>
    })

test:do_execsql_test(
    "index-14.9",
    [[
        SELECT c FROM t6 WHERE a<='abc';
    ]], {
        -- <index-14.9>
        5, 2, 1, 4
        -- </index-14.9>
    })

test:do_execsql_test(
    "index-14.10",
    [[
        SELECT c FROM t6 WHERE a<='';
    ]], {
        -- <index-14.10>
        5, 2, 1
        -- </index-14.10>
    })

test:do_execsql_test(
    "index-14.11",
    [[
        SELECT c FROM t6 WHERE a<'';
    ]], {
        -- <index-14.11>
        5
        -- </index-14.11>
    })

-- integrity_check index-14.12
test:do_execsql_test(
    "index-15.1",
    [[
        DELETE FROM t1;
        SELECT * FROM t1;
    ]], {
        -- <index-15.1>
        
        -- </index-15.1>
    })

-- do_test index-15.2 {
--   execsql {
--     INSERT INTO t1 VALUES(1, '1.234e5',1);
--     INSERT INTO t1 VALUES(2, '12.33e04',2);
--     INSERT INTO t1 VALUES(3, '12.35E4',3);
--     INSERT INTO t1 VALUES(4, '12.34e',4);
--     INSERT INTO t1 VALUES(5, '12.32e+4',5);
--     INSERT INTO t1 VALUES(6, '12.36E+04',6);
--     INSERT INTO t1 VALUES(7, '12.36E+',7);
--     INSERT INTO t1 VALUES(8, '+123.10000E+0003',8);
--     INSERT INTO t1 VALUES(9, '+',9);
--     INSERT INTO t1 VALUES(10, '+12347.E+02',10);
--     INSERT INTO t1 VALUES(11, '+12347E+02',11);
--     INSERT INTO t1 VALUES(12, '+.125E+04',12);
--     INSERT INTO t1 VALUES(13, '-.125E+04',13);
--     INSERT INTO t1 VALUES(14, '.125E+0',14);
--     INSERT INTO t1 VALUES(15, '.125',15);
--     SELECT b FROM t1 ORDER BY a, b;
--   }
-- } {13 14 15 12 8 5 2 1 3 6 10 11 9 4 7}
-- # do_test index-15.3 {
--   execsql {
--     SELECT b FROM t1 WHERE typeof(a) IN ('integer','real') ORDER BY b;
--   }
-- } {1 2 3 5 6 8 10 11 12 13 14 15}
-- integrity_check index-15.4
-- The following tests - index-16.* - test that when a table definition
-- includes qualifications that specify the same constraint twice only a
-- single index is generated to enforce the constraint.
--
-- For example: "CREATE TABLE abc( x PRIMARY KEY, UNIQUE(x) );"
--
test:do_execsql_test(
    "index-16.1",
    [[
        CREATE TABLE t7(c UNIQUE PRIMARY KEY);
        SELECT count(*) FROM "_index" JOIN "_space" WHERE "_index"."id" = "_space"."id" AND "_space"."name"='T7';
    ]], {
        -- <index-16.1>
        1
        -- </index-16.1>
    })

test:do_execsql_test(
    "index-16.2",
    [[
        DROP TABLE t7;
        CREATE TABLE t7(c UNIQUE PRIMARY KEY);
        SELECT count(*) FROM "_index" JOIN "_space" WHERE "_index"."id" = "_space"."id" AND "_space"."name"='T7';
    ]], {
        -- <index-16.2>
        1
        -- </index-16.2>
    })

test:do_execsql_test(
    "index-16.3",
    [[
        DROP TABLE t7;
        CREATE TABLE t7(c PRIMARY KEY, UNIQUE(c) );
        SELECT count(*) FROM "_index" JOIN "_space" WHERE "_index"."id" = "_space"."id" AND "_space"."name"='T7';
    ]], {
        -- <index-16.3>
        1
        -- </index-16.3>
    })

test:do_execsql_test(
    "index-16.4",
    [[
        DROP TABLE t7;
        CREATE TABLE t7(c, d , UNIQUE(c, d), PRIMARY KEY(c, d) );
        SELECT count(*) FROM "_index" JOIN "_space" WHERE "_index"."id" = "_space"."id" AND "_space"."name"='T7';
    ]], {
        -- <index-16.4>
        1
        -- </index-16.4>
    })

test:do_execsql_test(
    "index-16.5",
    [[
        DROP TABLE t7;
        CREATE TABLE t7(c, d , UNIQUE(c), PRIMARY KEY(c, d) );
        SELECT count(*) FROM "_index" JOIN "_space" WHERE "_index"."id" = "_space"."id" AND "_space"."name"='T7';
    ]], {
        -- <index-16.5>
        2
        -- </index-16.5>
    })

-- Test that automatically create indices are named correctly. The current
-- convention is: "sql_autoindex_<table name>_<integer>"
--
-- Then check that it is an error to try to drop any automtically created
-- indices.
test:do_execsql_test(
    "index-17.1",
    [[
        DROP TABLE t7;
        CREATE TABLE t7(c, d UNIQUE, UNIQUE(c), PRIMARY KEY(c, d) );
        SELECT "_index"."name" FROM "_index" JOIN "_space" WHERE "_index"."id" = "_space"."id" AND "_space"."name"='T7';
    ]], {
        -- <index-17.1>
        "pk_unnamed_T7_3", "unique_unnamed_T7_2", "unique_unnamed_T7_1"
        -- </index-17.1>
    })

-- do_test index-17.2 {
--   catchsql {
--     DROP INDEX sql_autoindex_t7_1;
--   }
-- } {1 {index associated with UNIQUE or PRIMARY KEY constraint cannot be dropped}}
-- do_test index-17.3 {
--   catchsql {
--     DROP INDEX IF EXISTS sql_autoindex_t7_1;
--   }
-- } {1 {index associated with UNIQUE or PRIMARY KEY constraint cannot be dropped}}
test:do_catchsql_test(
    "index-17.4",
    [[
        DROP INDEX IF EXISTS no_such_index ON t7;
    ]], {
        -- <index-17.4>
        0
        -- </index-17.4>
    })

test:do_execsql_test(
    "index-17.5",
    [[
        DROP TABLE t7;
    ]], {
    })

-- Try to create a TEMP index on a non-TEMP table. */
--
test:do_catchsql_test(
    "index-21.1",
    [[
        CREATE INDEX temp.i21 ON t6(c);
    ]], {
        -- <index-21.1>
        1, "near \".\": syntax error"
        -- </index-21.1>
    })

-- MUST_WORK_TEST different schemas not supported
if (0 > 0)
 then
    test:do_catchsql_test(
        "index-21.2",
        [[
            CREATE TABLE t6(x primary key);
            INSERT INTO temp.t6 values(1),(5),(9);
            CREATE INDEX temp.i21 ON t6(x);
            SELECT x FROM t6 ORDER BY x DESC;
        ]], {
            -- <index-21.2>
            0, {9, 5, 1}
            -- </index-21.2>
        })

end


test:finish_test()
