#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(31)

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
-- This file implements regression tests for sql library.
--
-- This file implements tests for the special processing associated
-- with INTEGER PRIMARY KEY columns.
--
-- $Id: intpkey.test,v 1.24 2007/11/29 17:43:28 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Create a table with a primary key and a datatype other than
-- integer
--
test:do_execsql_test(
    "intpkey-1.0",
    [[
        CREATE TABLE t1(a TEXT PRIMARY KEY, b INT, c INT);
    ]], {
        -- <intpkey-1.0>

        -- </intpkey-1.0>
    })

-- There should be an index associated with the primary key
--
test:do_execsql_test(
    "intpkey-1.1",
    [[
        SELECT "_index"."name" FROM "_index" JOIN "_space" WHERE "_index"."id" = "_space"."id" AND "_space"."name"='T1'
    ]], {
        -- <intpkey-1.1>
        "pk_unnamed_T1_1"
        -- </intpkey-1.1>
    })

-- Now create a table with an integer primary key and verify that
-- there is no associated index.
--
test:do_execsql_test(
    "intpkey-1.2",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b TEXT, c TEXT);
        --SELECT name FROM sql_master
        --  WHERE type='index' AND tbl_name='t1';
    ]], {
        -- <intpkey-1.2>

        -- </intpkey-1.2>
    })

-- Insert some records into the new table.  Specify the primary key
-- and verify that the key is used as the record number.
--
test:do_test(
    "intpkey-1.3",
    function()
        return test:execsql [[
            INSERT INTO t1 VALUES(5,'hello','world');
        ]]
        --db last_insert_rowid
    end, {
        -- <intpkey-1.3>

        -- </intpkey-1.3>
    })

--} {5}
test:do_execsql_test(
    "intpkey-1.4",
    [[
        -- SELECT rowid, * FROM t1;
        SELECT * FROM t1;
    ]], {
        -- <intpkey-1.4>
        5, "hello", "world"
        -- </intpkey-1.4>
    })

test:do_catchsql_test(
    "intpkey-1.6",
    [[
        INSERT INTO t1 VALUES(5,'second','entry');
    ]], {
        -- <intpkey-1.6>
        1, "Duplicate key exists in unique index \"pk_unnamed_T1_1\" in space \"T1\" with old tuple - [5, \"hello\", \"world\"] and new tuple - [5, \"second\", \"entry\"]"
        -- </intpkey-1.6>
    })

test:do_execsql_test(
    "intpkey-1.7",
    [[
        -- SELECT rowid, * FROM t1;
        SELECT * FROM t1;
    ]], {
    -- <intpkey-1.4>
    5, "hello", "world"
    -- </intpkey-1.4>
})

test:do_catchsql_test(
    "intpkey-1.8",
    [[
        INSERT INTO t1 VALUES(6,'second','entry');
    ]], {
        -- <intpkey-1.8>
        0
        -- </intpkey-1.8>
    })

-- do_test intpkey-1.8.1 {
--   db last_insert_rowid
-- } {6}
-- do_test intpkey-1.9 {
--   execsql {
--     SELECT rowid, * FROM t1;
--   }
-- } {5 5 hello world 6 6 second entry}
test:execsql "insert into t1 values(7, 'one', 'two')"
-- A ROWID is automatically generated for new records that do not specify
-- the integer primary key.
--
test:do_execsql_test(
    "intpkey-1.10",
    [[
        --INSERT INTO t1(b,c) VALUES('one','two');
        SELECT b FROM t1 ORDER BY b;
    ]], {
        -- <intpkey-1.10>
        "hello", "one", "second"
        -- </intpkey-1.10>
    })

-- Try to change the ROWID for the new entry.
--
test:do_execsql_test(
    "intpkey-1.11",
    [[
        DELETE FROM t1 WHERE a = 7;
        INSERT INTO t1 VALUES(4,'one','two');
        SELECT * FROM t1;
    ]], {
        -- <intpkey-1.11>
        4, "one", "two", 5, "hello", "world", 6, "second", "entry"
        -- </intpkey-1.11>
    })

-- Make sure SELECT statements are able to use the primary key column
-- as an index.
--
test:do_execsql_test(
    "intpkey-1.12.1",
    [[
        SELECT * FROM t1 WHERE a==4;
    ]], {
        -- <intpkey-1.12.1>
        4, "one", "two"
        -- </intpkey-1.12.1>
    })

test:do_execsql_test(
    "intpkey-1.12.2",
    [[
        EXPLAIN QUERY PLAN
        SELECT * FROM t1 WHERE a==4;
    ]], {
        -- <intpkey-1.12.2>
        "/SEARCH TABLE T1 /"
        -- </intpkey-1.12.2>
    })

-- Try to insert a non-integer value into the primary key field.  This
-- should result in a data type mismatch.
--
-- MUST_WORK_TEST
if (0 > 0)
 then
    --Tarantoool: issues submitted #2315
    test:do_catchsql_test(
        "intpkey-1.13.1",
        [[
            INSERT INTO t1 VALUES('x','y','z');
        ]], {
            -- <intpkey-1.13.1>
            1, "datatype mismatch"
            -- </intpkey-1.13.1>
        })

    test:do_catchsql_test(
        "intpkey-1.13.2",
        [[
            INSERT INTO t1 VALUES('','y','z');
        ]], {
            -- <intpkey-1.13.2>
            1, "datatype mismatch"
            -- </intpkey-1.13.2>
        })

    test:do_catchsql_test(
        "intpkey-1.14",
        [[
            INSERT INTO t1 VALUES(3.4,'y','z');
        ]], {
            -- <intpkey-1.14>
            1, "datatype mismatch"
            -- </intpkey-1.14>
        })

end
test:do_catchsql_test(
    "intpkey-1.15",
    [[
        INSERT INTO t1 VALUES(-3,'y','z');
    ]], {
        -- <intpkey-1.15>
        0
        -- </intpkey-1.15>
    })

test:do_execsql_test(
    "intpkey-1.16",
    [[
        SELECT * FROM t1
    ]], {
        -- <intpkey-1.16>
        -3, "y", "z", 4, "one", "two", 5, "hello", "world", 6, "second", "entry"
        -- </intpkey-1.16>
    })

-- Direct update of PK is forbidden
--
test:do_catchsql_test(
    "intpkey-1.17",
    [[
        CREATE TABLE test(id INT PRIMARY KEY AUTOINCREMENT);
        INSERT INTO test VALUES (1);
        UPDATE test SET id = 2;
    ]], {
        1, "Attempt to modify a tuple field which is part of index 'pk_unnamed_TEST_1' in space 'TEST'"
    })

--### INDICES
-- Check to make sure indices work correctly with integer primary keys
--
test:do_execsql_test(
    "intpkey-2.1",
    [[
        CREATE INDEX i1 ON t1(b);
        SELECT * FROM t1 WHERE b=='y'
    ]], {
        -- <intpkey-2.1>
        -3, "y", "z"
        -- </intpkey-2.1>
    })

-- do_test intpkey-2.1.1 {
--   execsql {
--     SELECT * FROM t1 WHERE b=='y' AND rowid<0
--   }
-- } {-3 y z}
-- do_test intpkey-2.1.2 {
--   execsql {
--     SELECT * FROM t1 WHERE b=='y' AND rowid<0 AND rowid>=-20
--   }
-- } {-3 y z}
test:do_execsql_test(
    "intpkey-2.1.3",
    [[
        SELECT * FROM t1 WHERE b>='y'
    ]], {
        -- <intpkey-2.1.3>
        -3, "y", "z"
        -- </intpkey-2.1.3>
    })

-- do_test intpkey-2.1.4 {
--   execsql {
--     SELECT * FROM t1 WHERE b>='y' AND rowid<10
--   }
-- } {-3 y z}
test:do_execsql_test(
    "intpkey-2.2",
    [[
        DELETE FROM t1 WHERE b=='y';
        INSERT INTO t1 VALUES(8,'y','z');
        SELECT * FROM t1 WHERE b=='y';
    ]], {
        -- <intpkey-2.2>
        8, "y", "z"
        -- </intpkey-2.2>
    })

-- do_test intpkey-2.3 {
--   execsql {
--     SELECT rowid, * FROM t1;
--   }
-- } {4 4 one two 5 5 hello world 6 6 second entry 8 8 y z}
-- do_test intpkey-2.4 {
--   execsql {
--     SELECT rowid, * FROM t1 WHERE b<'second'
--   }
-- } {5 5 hello world 4 4 one two}
-- do_test intpkey-2.4.1 {
--   execsql {
--     SELECT rowid, * FROM t1 WHERE 'second'>b
--   }
-- } {5 5 hello world 4 4 one two}
-- do_test intpkey-2.4.2 {
--   execsql {
--     SELECT rowid, * FROM t1 WHERE 8>rowid AND 'second'>b
--   }
-- } {4 4 one two 5 5 hello world}
-- do_test intpkey-2.4.3 {
--   execsql {
--     SELECT rowid, * FROM t1 WHERE 8>rowid AND 'second'>b AND 0<rowid
--   }
-- } {4 4 one two 5 5 hello world}
-- do_test intpkey-2.5 {
--   execsql {
--     SELECT rowid, * FROM t1 WHERE b>'a'
--   }
-- } {5 5 hello world 4 4 one two 6 6 second entry 8 8 y z}
test:do_execsql_test(
    "intpkey-2.6",
    [[
        --DELETE FROM t1 WHERE rowid=4;
        DELETE FROM t1 WHERE a = 4;
        SELECT * FROM t1 WHERE b>'a';
    ]], {
        -- <intpkey-2.6>
        5, "hello", "world", 6, "second", "entry", 8, "y", "z"
        -- </intpkey-2.6>
    })

test:do_execsql_test(
    "intpkey-2.7",
    [[
        --UPDATE t1 SET a=-4 WHERE rowid=8;
        DELETE FROM t1 WHERE a==8;
        INSERT INTO t1 VALUES(-4,'y','z');
        SELECT * FROM t1 WHERE b>'a';
    ]], {
        -- <intpkey-2.7>
        5, "hello", "world", 6, "second", "entry", -4, "y", "z"
        -- </intpkey-2.7>
    })

test:do_execsql_test(
    "intpkey-2.7",
    [[
        SELECT * FROM t1
    ]], {
        -- <intpkey-2.7>
        -4, "y", "z", 5, "hello", "world", 6, "second", "entry"
        -- </intpkey-2.7>
    })

-- Do an SQL statement.  Append the search count to the end of the result.
--
local function count(sql)
    local sql_search_count = 0
    local r = test:execsql(sql)
    table.insert(r, sql_search_count)
    return r
end

-- Create indices that include the integer primary key as one of their
-- columns.
--
test:do_execsql_test(
    "intpkey-3.1",
    [[
        CREATE INDEX i2 ON t1(a);
    ]], {
        -- <intpkey-3.1>

        -- </intpkey-3.1>
    })

-- MUST_WORK_TEST
if (0 > 0) then
test:do_test(
    "intpkey-3.2",
    function()
        return count([[
            SELECT * FROM t1 WHERE a=5;
          ]])
    end, {
    -- <intpkey-3.2>
    5, "hello", "world", 1
    -- </intpkey-3.2>
    })

test:do_test(
"intpkey-3.3",
function()
    return count([[
        SELECT * FROM t1 WHERE a>4 AND a<6;
      ]])
    end, {
    -- <intpkey-3.3>
    5, "hello", "world", 2
    -- </intpkey-3.3>
    })
test:do_test(
    "intpkey-3.4",
    function()
        return count([[
            SELECT * FROM t1 WHERE b>='hello' AND b<'hello2';
          ]])
    end, {
    -- <intpkey-3.4>
    5, "hello", "world", 3
    -- </intpkey-3.4>
    })

test:do_execsql_test(
    "intpkey-3.5",
    [[
        CREATE INDEX i3 ON t1(c,a);
    ]], {
        -- <intpkey-3.5>

        -- </intpkey-3.5>
    })

test:do_test(
    "intpkey-3.6",
    function()
        return count([[
        SELECT * FROM t1 WHERE c=='world';
      ]])
    end, {
    -- <intpkey-3.6>
    5, "hello", "world", 1
    -- </intpkey-3.6>
    })

test:do_test(
    "intpkey-3.7",
    function()
        test:execsql("INSERT INTO t1 VALUES(11,'hello','world')")
        return count([[
            SELECT * FROM t1 WHERE c=='world';
          ]])
    end, {
    -- <intpkey-3.7>
    5, "hello", "world", 11, "hello", "world", 2
    -- </intpkey-3.7>
    })

test:do_test(
    "intpkey-3.8",
    function()
        return count([[
            SELECT * FROM t1 WHERE c=='world' AND a>7;
          ]])
    end, {
    -- <intpkey-3.8>
    11, "hello", "world", 4
    -- </intpkey-3.8>
    })

test:do_test(
    "intpkey-3.9",
    function()
        return count([[
            SELECT * FROM t1 WHERE 7<a;
        ]])
    end, {
    -- <intpkey-3.9>
    11, "hello", "world", 1
    -- </intpkey-3.9>
    })
end

-- # Test inequality constraints on integer primary keys and rowids
-- #
-- do_test intpkey-4.1 {
--   count {
--     SELECT * FROM t1 WHERE 11=rowid
--   }
-- } {11 hello world 0}
-- do_test intpkey-4.2 {
--   count {
--     SELECT * FROM t1 WHERE 11=rowid AND b=='hello'
--   }
-- } {11 hello world 0}
-- do_test intpkey-4.3 {
--   count {
--     SELECT * FROM t1 WHERE 11=rowid AND b=='hello' AND c IS NOT NULL;
--   }
-- } {11 hello world 0}
-- do_test intpkey-4.4 {
--   count {
--     SELECT * FROM t1 WHERE rowid==11
--   }
-- } {11 hello world 0}
-- do_test intpkey-4.5 {
--   count {
--     SELECT * FROM t1 WHERE oid==11 AND b=='hello'
--   }
-- } {11 hello world 0}
-- MUST_WORK_TEST
if (0 > 0) then
test:do_test(
    "intpkey-4.6",
    function()
        return count([[
            SELECT * FROM t1 WHERE a==11 AND b=='hello' AND c IS NOT NULL;
          ]])
    end, {
    -- <intpkey-4.6>
    11, "hello", "world", 0
    -- </intpkey-4.6>
    })
end

-- do_test intpkey-4.7 {
--   count {
--     SELECT * FROM t1 WHERE 8<rowid;
--   }
-- } {11 hello world 1}
-- do_test intpkey-4.8 {
--   count {
--     SELECT * FROM t1 WHERE 8<rowid AND 11>=oid;
--   }
-- } {11 hello world 1}
-- do_test intpkey-4.9 {
--   count {
--     SELECT * FROM t1 WHERE 11<=_rowid_ AND 12>=a;
--   }
-- } {11 hello world 1}
-- do_test intpkey-4.10 {
--   count {
--     SELECT * FROM t1 WHERE 0>=_rowid_;
--   }
-- } {-4 y z 1}
-- MUST_WORK_TEST
if (0 > 0) then
test:do_test(
    "intpkey-4.11",
    function()
        return count([[
    SELECT * FROM t1 WHERE a<0;
  ]])
    end, {
        -- <intpkey-4.11>
        -4, "y", "z", 1
        -- </intpkey-4.11>
    })

test:do_test(
    "intpkey-4.12",
    function()
        return count([[
    SELECT * FROM t1 WHERE a<0 AND a>10;
  ]])
    end, {
        -- <intpkey-4.12>
        1
        -- </intpkey-4.12>
    })

-- Make sure it is OK to insert a rowid of 0
--
test:do_test(
    "intpkey-5.1",
    function()
        test:execsql [[
            INSERT INTO t1 VALUES(0,'zero','entry');
        ]]
        return count([[
    SELECT * FROM t1 WHERE a=0;
  ]])
    end, {
        -- <intpkey-5.1>
        0, "zero", "entry", 1
        -- </intpkey-5.1>
    })

test:do_execsql_test(
    "intpkey-6.2",
    [[
        SELECT * FROM t1 WHERE b=='hello'
    ]], {
    -- <intpkey-6.2>
    5, "hello", "world", 11, "hello", "world"
    -- </intpkey-6.2>
    })
end
-- do_test intpkey-5.2 {
--   execsql {
--     SELECT rowid, a FROM t1 ORDER BY rowid
--   }
-- } {-4 -4 0 0 5 5 6 6 11 11}
-- Test the ability of the COPY command to put data into a
-- table that contains an integer primary key.
--
-- COPY command has been removed.  But we retain these tests so
-- that the tables will contain the right data for tests that follow.
--
test:do_execsql_test(
    "intpkey-6.1",
    [[
        START TRANSACTION;
        INSERT INTO t1 VALUES(20,'b-20','c-20');
        INSERT INTO t1 VALUES(21,'b-21','c-21');
        INSERT INTO t1 VALUES(22,'b-22','c-22');
        COMMIT;
        SELECT * FROM t1 WHERE a>=20;
    ]], {
        -- <intpkey-6.1>
        20, "b-20", "c-20", 21, "b-21", "c-21", 22, "b-22", "c-22"
        -- </intpkey-6.1>
    })

test:do_execsql_test(
    "intpkey-6.3",
    [[
        DELETE FROM t1 WHERE b='b-21';
        SELECT * FROM t1 WHERE b=='b-21';
    ]], {
        -- <intpkey-6.3>

        -- </intpkey-6.3>
    })

test:do_execsql_test(
    "intpkey-6.4",
    [[
        SELECT * FROM t1 WHERE a>=20
    ]], {
        -- <intpkey-6.4>
        20, "b-20", "c-20", 22, "b-22", "c-22"
        -- </intpkey-6.4>
    })

-- Do an insert of values with the columns specified out of order.
--
test:do_execsql_test(
    "intpkey-7.1",
    [[
        INSERT INTO t1(c,b,a) VALUES('row','new',30);
        --SELECT * FROM t1 WHERE rowid>=30;
    ]], {
        -- <intpkey-7.1>

        -- </intpkey-7.1>
    })

--} {30 new row}
-- do_test intpkey-7.2 {
--   execsql {
--     SELECT * FROM t1 WHERE rowid>20;
--   }
-- } {22 b-22 c-22 30 new row}
-- Do an insert from a select statement.
--
-- MUST_WORK_TEST depends on disabled tests sbove
if (0 > 0) then
test:do_execsql_test(
    "intpkey-8.1",
    [[
        CREATE TABLE t2(x INTEGER PRIMARY KEY, y INT, z INT);
        INSERT INTO t2 SELECT * FROM t1;
        --SELECT rowid FROM t2;
        SELECT x FROM t2;
    ]], {
        -- <intpkey-8.1>
        -4, 0, 5, 6, 11, 20, 22, 30
        -- </intpkey-8.1>
    })

test:do_execsql_test(
    "intpkey-8.2",
    [[
        SELECT x FROM t2;
    ]], {
        -- <intpkey-8.2>
        -4, 0, 5, 6, 11, 20, 22, 30
        -- </intpkey-8.2>
    })
test:do_execsql_test(
    "intpkey-9.1",
    [[
        UPDATE t1 SET c='www' WHERE c='world';
        --SELECT rowid, a, c FROM t1 WHERE c=='www';
        SELECT a, a, c FROM t1 WHERE c = 'www';
    ]], {
    -- <intpkey-9.1>
        5, 5, "www", 11, 11, "www"
    -- </intpkey-9.1>
    })
end

-- MUST_WORK_TEST
if (0 > 0)
 then
    -- Check insert of NULL for primary key
    --
    test:do_execsql_test(
        "intpkey-10.1",
        [[
            DROP TABLE t2;
            CREATE TABLE t2(x INTEGER PRIMARY KEY, y INT, z INT);
            INSERT INTO t2 VALUES(NULL, 1, 2);
            SELECT * from t2;
        ]], {
            -- <intpkey-10.1>
            1, 1, 2
            -- </intpkey-10.1>
        })

    test:do_execsql_test(
        "intpkey-10.2",
        [[
            INSERT INTO t2 VALUES(NULL, 2, 3);
            SELECT * from t2 WHERE x=2;
        ]], {
            -- <intpkey-10.2>
            2, 2, 3
            -- </intpkey-10.2>
        })

    test:do_execsql_test(
        "intpkey-10.3",
        [[
            INSERT INTO t2 SELECT NULL, z, y FROM t2;
            SELECT * FROM t2;
        ]], {
            -- <intpkey-10.3>
            1, 1, 2, 2, 2, 3, 3, 2, 1, 4, 3, 2
            -- </intpkey-10.3>
        })

    -- This tests checks to see if a floating point number can be used
    -- to reference an integer primary key.
end
test:do_execsql_test(
    "intpkey-11.1",
    [[
        SELECT b FROM t1 WHERE a=2.0+3.0;
    ]], {
        -- <intpkey-11.1>
        "hello"
        -- </intpkey-11.1>
    })

test:do_execsql_test(
    "intpkey-11.1",
    [[
        SELECT b FROM t1 WHERE a=2.0+3.5;
    ]], {
        -- <intpkey-11.1>

        -- </intpkey-11.1>
    })

-- Check for proper handling of primary keys greater than 2^31.
-- Ticket #1188
--
test:do_execsql_test(
    "intpkey-15.1",
    [[
        INSERT INTO t1 VALUES(2147483647, 'big-1', '123');
        SELECT * FROM t1 WHERE a>2147483648;
    ]], {
        -- <intpkey-15.1>

        -- </intpkey-15.1>
    })

-- MUST_WORK_TEST
if (0 > 0) then
    --Tarantool: a is PK, so NULL is prohibited.
    test:do_execsql_test(
        "intpkey-15.2",
        [[
            INSERT INTO t1 VALUES(NULL, 'big-2', 234);
            SELECT b FROM t1 WHERE a>=2147483648;
        ]], {
            -- <intpkey-15.2>
            "big-2"
            -- </intpkey-15.2>
        })

end
test:do_execsql_test(
    "intpkey-15.3",
    [[
        SELECT b FROM t1 WHERE a>2147483648;
    ]], {
        -- <intpkey-15.3>

        -- </intpkey-15.3>
    })

-- MUST_WORK_TEST
if (0 > 0) then
test:do_execsql_test(
    "intpkey-15.4",
    [[
        SELECT b FROM t1 WHERE a>=2147483647;
    ]], {
        -- <intpkey-15.4>
        "big-1", "big-2"
        -- </intpkey-15.4>
    })

test:do_execsql_test(
    "intpkey-15.5",
    [[
        SELECT b FROM t1 WHERE a<2147483648;
    ]], {
        -- <intpkey-15.5>
        "y", "zero", 2, "hello", "second", "hello", "b-20", "b-22", "new", 3, "big-1"
        -- </intpkey-15.5>
    })

test:do_execsql_test(
    "intpkey-15.6",
    [[
        SELECT b FROM t1 WHERE a<12345678901;
    ]], {
        -- <intpkey-15.6>
        "y", "zero", 2, "hello", "second", "hello", "b-20", "b-22", "new", 3, "big-1", "big-2"
        -- </intpkey-15.6>
    })
end

test:do_execsql_test(
    "intpkey-15.7",
    [[
        SELECT b FROM t1 WHERE a>12345678901;
    ]], {
        -- <intpkey-15.7>
        -- </intpkey-15.7>
    })



test:finish_test()
