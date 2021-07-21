#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(59)

--!./tcltestrunner.lua
-- 2001 September 15.
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
-- This file implements tests for miscellanous features that were
-- left out of other test files.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Mimic the sql 2 collation type NUMERIC.

-- Test the creation and use of tables that have a large number
-- of columns.
--
test:do_test(
    "misc1-1.1",
    function()
        local cmd = "CREATE TABLE manycol(id  INT primary key, x0 text"
        for i = 1, 99, 1 do
            cmd = cmd .. ",x"..i.." text"
        end
        cmd = cmd .. ")"
        test:execsql(cmd)
        cmd = "INSERT INTO manycol VALUES(1, '0'"
        for i = 1, 99, 1 do
            cmd = cmd .. ",'"..i.."'"
        end
        cmd = cmd .. ")"
        test:execsql(cmd)
        return test:execsql("SELECT x99 FROM manycol")
    end, {
        -- <misc1-1.1>
        "99"
        -- </misc1-1.1>
    })

test:do_execsql_test(
    "misc1-1.2",
    [[
        SELECT x0, x10, x25, x50, x75 FROM manycol
    ]], {
        -- <misc1-1.2>
        "0", "10", "25", "50", "75"
        -- </misc1-1.2>
    })

test:do_test(
    "misc1-1.3.1",
    function()
        for j = 100, 1000, 100 do
            local cmd = string.format("INSERT INTO manycol VALUES(%s, '%s'", j, j)
            for i = 1, 99, 1 do
                cmd = cmd .. ",'"..(i + j).."'"
            end
            cmd = cmd .. ")"
            test:execsql(cmd)
        end
        return test:execsql([[
            SELECT x50 FROM manycol ORDER BY CAST(x80 AS INTEGER);
        ]])
    end, {
        -- <misc1-1.3.1>
        "50", "150", "250", "350", "450", "550", "650", "750", "850", "950", "1050"
        -- </misc1-1.3.1>
    })

test:do_execsql_test(
    "misc1-1.3.2",
    [[
        SELECT x50 FROM manycol ORDER BY x80
    ]], {
        -- <misc1-1.3.2>
        "1050", "150", "250", "350", "450", "550", "650", "750", "50", "850", "950"
        -- </misc1-1.3.2>
    })

test:do_execsql_test(
    "misc1-1.4",
    [[
        SELECT x75 FROM manycol WHERE x50=350
    ]], {
        -- <misc1-1.4>
        "375"
        -- </misc1-1.4>
    })

test:do_execsql_test(
    "misc1-1.5",
    [[
        SELECT x50 FROM manycol WHERE x99=599
    ]], {
        -- <misc1-1.5>
        "550"
        -- </misc1-1.5>
    })

test:do_test(
    "misc1-1.6",
    function()
        test:execsql("CREATE INDEX manycol_idx1 ON manycol(x99)")
        return test:execsql("SELECT x50 FROM manycol WHERE x99=899")
    end, {
        -- <misc1-1.6>
        "850"
        -- </misc1-1.6>
    })

test:do_execsql_test(
    "misc1-1.7",
    [[
        SELECT count(*) FROM manycol
    ]], {
        -- <misc1-1.7>
        11
        -- </misc1-1.7>
    })

test:do_test(
    "misc1-1.8",
    function()
        test:execsql("DELETE FROM manycol WHERE x98=1234")
        return test:execsql("SELECT count(*) FROM manycol")
    end, {
        -- <misc1-1.8>
        11
        -- </misc1-1.8>
    })

test:do_test(
    "misc1-1.9",
    function()
        test:execsql("DELETE FROM manycol WHERE x98=998")
        return test:execsql("SELECT count(*) FROM manycol")
    end, {
        -- <misc1-1.9>
        10
        -- </misc1-1.9>
    })

test:do_test(
    "misc1-1.10",
    function()
        test:execsql("DELETE FROM manycol WHERE x99=500")
        return test:execsql("SELECT count(*) FROM manycol")
    end, {
        -- <misc1-1.10>
        10
        -- </misc1-1.10>
    })

test:do_test(
    "misc1-1.11",
    function()
        test:execsql("DELETE FROM manycol WHERE x99=599")
        return test:execsql("SELECT count(*) FROM manycol")
    end, {
        -- <misc1-1.11>
        9
        -- </misc1-1.11>
    })

-- Check GROUP BY expressions that name two or more columns.
--
test:do_test(
    "misc1-2.1",
    function()
        test:execsql([[
            CREATE TABLE agger(one int primary key, two text, three text, four text);
            START TRANSACTION;
            INSERT INTO agger VALUES(1, 'one', 'hello', 'yes');
            INSERT INTO agger VALUES(2, 'two', 'howdy', 'no');
            INSERT INTO agger VALUES(3, 'thr', 'howareya', 'yes');
            INSERT INTO agger VALUES(4, 'two', 'lothere', 'yes');
            INSERT INTO agger VALUES(5, 'one', 'atcha', 'yes');
            INSERT INTO agger VALUES(6, 'two', 'hello', 'no');
            COMMIT
        ]])
        return test:execsql("SELECT count(*) FROM agger")
    end, {
        -- <misc1-2.1>
        6
        -- </misc1-2.1>
    })

test:do_execsql_test(
    "misc1-2.2",
    [[SELECT sum(one), two, four FROM agger
           GROUP BY two, four ORDER BY sum(one) desc]], {
        -- <misc1-2.2>
        8, "two", "no", 6, "one", "yes", 4, "two", "yes", 3, "thr", "yes"
        -- </misc1-2.2>
    })

test:do_execsql_test(
    "misc1-2.3",
    [[SELECT sum((one)), (two), (four) FROM agger
           GROUP BY (two), (four) ORDER BY sum(one) desc]], {
        -- <misc1-2.3>
        8, "two", "no", 6, "one", "yes", 4, "two", "yes", 3, "thr", "yes"
        -- </misc1-2.3>
    })
-- Here's a test for a bug found by Joel Lucsy.  The code below
-- was causing an assertion failure.
--
test:do_test(
    "misc1-3.1",
    function()
        local r = test:execsql([[
            CREATE TABLE t1(a TEXT primary KEY);
            INSERT INTO t1 VALUES('hi');
            UPDATE "_session_settings" SET "value" = true WHERE "name" = 'sql_full_column_names';
            --SELECT rowid, * FROM t1;
            SELECT * FROM t1;
        ]])
        return test.lindex(r, 1)
    end, {
        -- <misc1-3.1>

        -- </misc1-3.1>
    })

--} {hi}
-- Here's a test for yet another bug found by Joel Lucsy.  The code
-- below was causing an assertion failure.
--
test:do_execsql_test(
    "misc1-4.1",
    [[
        CREATE TABLE temp(id INT PRIMARY KEY, a TEXT);
        CREATE TABLE t2(a TEXT primary key);

        START TRANSACTION;
        INSERT INTO temp VALUES(0, 'This is a long string to use up a lot of disk -');
        UPDATE temp SET a=a||a||a||a;
        INSERT INTO t2 (a) SELECT a FROM temp;
        INSERT INTO t2 (a) SELECT '1 - ' || a FROM t2;
        INSERT INTO t2 (a) SELECT '2 - ' || a FROM t2;
        INSERT INTO t2 (a) SELECT '3 - ' || a FROM t2;
        INSERT INTO t2 (a) SELECT '4 - ' || a FROM t2;
        INSERT INTO t2 (a) SELECT '5 - ' || a FROM t2;
        INSERT INTO t2 (a) SELECT '6 - ' || a FROM t2;
        COMMIT;
        SELECT count(*) FROM t2;
    ]], {
        -- <misc1-4.1>
        64
        -- </misc1-4.1>
    })

-- Make sure we actually see a semicolon or end-of-file in the SQL input
-- before executing a command.  Thus if "WHERE" is misspelled on an UPDATE,
-- the user won't accidently update every record.
--

test:do_execsql_test(
    "misc1-5.1.1",
    [[
        CREATE TABLE t3(a  INT primary key,b INT );
        INSERT INTO t3 VALUES(1,2);
        INSERT INTO t3 VALUES(3,4);
    ]], {
        -- <misc1-5.1.1>
        -- </misc1-5.1.1>
    })

test:do_catchsql_test(
    "misc1-5.1.2",
    [[
        UPDATE t3 SET a=0 WHEREwww b=2;
    ]], {
        -- <misc1-5.1.2>
        1, [[Syntax error at line 1 near 'WHEREwww']]
        -- </misc1-5.1.2>
    })

test:do_execsql_test(
    "misc1-5.2",
    [[
        SELECT * FROM t3 ORDER BY a;
    ]], {
        -- <misc1-5.2>
        1, 2, 3, 4
        -- </misc1-5.2>
    })

-- Certain keywords (especially non-standard keywords like "REPLACE") can
-- also be used as identifiers.  The way this works in the parser is that
-- the parser first detects a syntax error, the error handling routine
-- sees that the special keyword caused the error, then replaces the keyword
-- with "ID" and tries again.
--
-- Check the operation of this logic.
--
test:do_catchsql_test(
    "misc1-6.1",
    [[
        CREATE TABLE t4(
          abort  INT primary key, "asc" INT, beginn INT , cluster INT , conflict INT , copy INT , delimiters INT , "desc" INT, endd INT ,
          "explain" INT, fail INT , ignore INT , key INT , offset INT , "pragma" INT, "replace" INT, temp INT , "view" INT
        );
    ]], {
        -- <misc1-6.1>
        0
        -- </misc1-6.1>
    })

test:do_catchsql_test(
    "misc1-6.2",
    [[
        INSERT INTO t4
           VALUES(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18);
    ]], {
        -- <misc1-6.2>
        0
        -- </misc1-6.2>
    })

test:do_execsql_test(
    "misc1-6.3",
    [[
        SELECT * FROM t4
    ]], {
        -- <misc1-6.3>
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18
        -- </misc1-6.3>
    })

test:do_execsql_test(
    "misc1-6.4",
    [[
        SELECT abort+"asc",GREATEST(key,"pragma",temp) FROM t4
    ]], {
        -- <misc1-6.4>
        3, 17
        -- </misc1-6.4>
    })

-- Test for multi-column primary keys, and for multiple primary keys.
--
test:do_catchsql_test(
    "misc1-7.1",
    [[
        CREATE TABLE error1(
          a  INT PRIMARY KEY,
          b  INT PRIMARY KEY
        );
    ]], {
        -- <misc1-7.1>
        1, [[Failed to create space 'ERROR1': primary key has been already declared]]
        -- </misc1-7.1>
    })

test:do_catchsql_test(
    "misc1-7.2",
    [[
        CREATE TABLE error1(
          a INTEGER PRIMARY KEY,
          b  INT PRIMARY KEY
        );
    ]], {
        -- <misc1-7.2>
        1, [[Failed to create space 'ERROR1': primary key has been already declared]]
        -- </misc1-7.2>
    })

test:do_execsql_test(
    "misc1-7.3",
    [[
        CREATE TABLE t5(a INT ,b INT ,c INT ,PRIMARY KEY(a,b));
        INSERT INTO t5 VALUES(1,2,3);
        SELECT * FROM t5 ORDER BY a;
    ]], {
        -- <misc1-7.3>
        1, 2, 3
        -- </misc1-7.3>
    })

test:do_catchsql_test(
    "misc1-7.4",
    [[
        INSERT INTO t5 VALUES(1,2,4);
    ]], {
        -- <misc1-7.4>
        1, "Duplicate key exists in unique index \"pk_unnamed_T5_1\" in space \"T5\" with old tuple - [1, 2, 3] and new tuple - [1, 2, 4]"
        -- </misc1-7.4>
    })

test:do_catchsql_test(
    "misc1-7.5",
    [[
        INSERT INTO t5 VALUES(0,2,4);
    ]], {
        -- <misc1-7.5>
        0
        -- </misc1-7.5>
    })

test:do_execsql_test(
    "misc1-7.6",
    [[
        SELECT * FROM t5 ORDER BY a;
    ]], {
        -- <misc1-7.6>
        0, 2, 4, 1, 2, 3
        -- </misc1-7.6>
    })

test:do_catchsql_test(
    "misc1-8.1",
    [[
        SELECT *;
    ]], {
        -- <misc1-8.1>
        1, "Failed to expand '*' in SELECT statement without FROM clause"
        -- </misc1-8.1>
    })

test:do_catchsql_test(
    "misc1-8.2",
    [[
        SELECT t1.*;
    ]], {
        -- <misc1-8.2>
        1, "Space 'T1' does not exist"
        -- </misc1-8.2>
    })

test:execsql([[
    DROP TABLE t1;
    DROP TABLE t2;
    DROP TABLE t3;
    DROP TABLE t4;
    DROP TABLE temp;
]])
-- 64-bit integers are represented exactly.
--
test:do_catchsql_test(
    "misc1-9.1",
    [[
        CREATE TABLE t1(a  TEXT primary key not null, b  INT unique not null);
        INSERT INTO t1 VALUES('a',1234567890123456789);
        INSERT INTO t1 VALUES('b',1234567891123456789);
        INSERT INTO t1 VALUES('c',1234567892123456789);
        SELECT * FROM t1;
    ]], {
        -- <misc1-9.1>
        0, {"a", 1234567890123456789LL, "b", 1234567891123456789LL, "c", 1234567892123456789LL}
        -- </misc1-9.1>
    })

-- A WHERE clause is not allowed to contain more than 99 terms.  Check to
-- make sure this limit is enforced.
--
-- 2005-07-16: There is no longer a limit on the number of terms in a
-- WHERE clause.  But keep these tests just so that we have some tests
-- that use a large number of terms in the WHERE clause.
--
test:do_execsql_test(
    "misc1-10.0",
    [[
        SELECT count(*) FROM manycol
    ]], {
        -- <misc1-10.0>
        9
        -- </misc1-10.0>
    })
local where = ""
test:do_test(
    "misc1-10.1",
    function()
        where = "WHERE x0>=0"
        for i = 1, 99, 1 do
            where = where .. " AND x"..i.."<>0"
        end
        return test:catchsql("SELECT count(*) FROM manycol "..where.."")
    end, {
        -- <misc1-10.1>
        0, {9}
        -- </misc1-10.1>
    })

-- do_test misc1-10.2 {
--   catchsql "SELECT count(*) FROM manycol $::where AND rowid>0"
-- } {0 9}
test:do_test(
    "misc1-10.3",
    function()
        where = string.gsub(where,"x0>=0", "x0=0")
        return test:catchsql("DELETE FROM manycol "..where.."")
    end, {
        -- <misc1-10.3>
        0
        -- </misc1-10.3>
    })

test:do_execsql_test(
    "misc1-10.4",
    [[
        SELECT count(*) FROM manycol
    ]], {
        -- <misc1-10.4>
        8
        -- </misc1-10.4>
    })

-- do_test misc1-10.5 {
--   catchsql "DELETE FROM manycol $::where AND rowid>0"
-- } {0 {}}
test:do_execsql_test(
    "misc1-10.6",
    [[
        SELECT x1 FROM manycol WHERE x0=100
    ]], {
        -- <misc1-10.6>
        "101"
        -- </misc1-10.6>
    })

local cast = "CAST(CAST(x1 AS INTEGER) + 1 AS STRING)"
test:do_test(
    "misc1-10.7",
    function()
        where = string.gsub(where, "x0=0", "x0=100")
        return test:catchsql("UPDATE manycol SET x1 = "..cast.." "..where..";")
    end, {
        -- <misc1-10.7>
        0
        -- </misc1-10.7>
    })

test:do_execsql_test(
    "misc1-10.8",
    [[
        SELECT x1 FROM manycol WHERE x0=100
    ]], {
        -- <misc1-10.8>
        "102"
        -- </misc1-10.8>
    })

-- do_test misc1-10.9 {
--   catchsql "UPDATE manycol SET x1=x1+1 $::where AND rowid>0"
-- } {0 {}}
test:do_execsql_test(
    "misc1-10.9",
    "UPDATE manycol SET x1 = "..cast.." "..where
        --"UPDATE manycol SET x1=x1+1 $::where AND rowid>0"
    , {})


-- Tarantool: the case has no sense as far as misc1-10.9 is commented.
-- It is essentially same as misc1-10.8. Comment the case.
test:do_execsql_test(
    "misc1-10.10",
    [[
        SELECT x1 FROM manycol WHERE x0=100
    ]], {
        -- <misc1-10.10>
        "103"
        -- </misc1-10.10>
    })

-- MUST_WORK_TEST
if (0 > 0) then
    -- Make sure the initialization works even if a database is opened while
    -- another process has the database locked.
    --
    -- Update for v3: The BEGIN doesn't lock the database so the schema is read
    -- and the SELECT returns successfully.
    test:do_test(
        "misc1-11.1",
        function()
            -- Legacy from the original code. Must be replaced with analogue
            -- functions from box.
            local sql = nil
            local X = nil
            local msg = nil
            test:execsql("START TRANSACTION")
            test:execsql("UPDATE t1 SET a=0 WHERE 0")
            sql("db2", "test.db")
            local rc = X(371, "X!cmd", [=[["catch","db2 eval {SELECT count(*) FROM t1}","msg"]]=])
            return table.insert(rc,msg) or rc
            -- v2 result: {1 {database is locked}}
        end, {
            -- <misc1-11.1>
            0, 3
            -- </misc1-11.1>
        })

    test:do_test(
        "misc1-11.2",
        function()
            -- Legacy from the original code. Must be replaced with analogue
            -- functions from box.
            local X = nil
            local msg = nil
            test:execsql("COMMIT")
            local rc = X(377, "X!cmd", [=[["catch","db2 eval {SELECT count(*) FROM t1}","msg"]]=])
            return table.insert(rc,msg) or rc
        end, {
            -- <misc1-11.2>
            0, 3
            -- </misc1-11.2>
        })

    -- Make sure string comparisons really do compare strings in format4+.
    -- Similar tests in the format3.test file show that for format3 and earlier
    -- all comparisions where numeric if either operand looked like a number.
end
test:do_execsql_test(
    "misc1-12.1",
    [[
        SELECT '0'=='0.0'
    ]], {
        -- <misc1-12.1>
        false
        -- </misc1-12.1>
    })

test:do_execsql_test(
    "misc1-12.2",
    [[
        SELECT '0'==0.0
    ]], {
        -- <misc1-12.2>
        true
        -- </misc1-12.2>
    })

test:do_execsql_test(
    "misc1-12.3",
    [[
        SELECT '12345678901234567890'=='12345678901234567891'
    ]], {
        -- <misc1-12.3>
        false
        -- </misc1-12.3>
    })

test:do_execsql_test(
    "misc1-12.4",
    [[
        CREATE TABLE t6(a TEXT UNIQUE, b TEXT primary key);
        INSERT INTO t6 VALUES('0','0.0');
        SELECT * FROM t6;
    ]], {
        -- <misc1-12.4>
    "0","0.0"
        -- </misc1-12.4>
    })

test:do_execsql_test(
    "misc1-12.5",
    [[
        INSERT OR IGNORE INTO t6 VALUES('0','x');
        SELECT * FROM t6;
    ]], {
        -- <misc1-12.5>
        "0", "0.0"
        -- </misc1-12.5>
    })

test:do_execsql_test(
    "misc1-12.6",
    [[
        INSERT OR IGNORE INTO t6 VALUES('y','0');
        SELECT * FROM t6;
    ]], {
        -- <misc1-12.6>
    "y","0","0","0.0"
        -- </misc1-12.6>
    })



test:do_execsql_test(
    "misc1-12.7",
    [[
        CREATE TABLE t7(x INTEGER, y TEXT, z  INT primary key);
        INSERT INTO t7 VALUES(0,'0',1);
        INSERT INTO t7 VALUES(0.0,'0',2);
        INSERT INTO t7 VALUES(0,'0.0',3);
        INSERT INTO t7 VALUES(0.0,'0.0',4);
        SELECT DISTINCT x, y FROM t7 ORDER BY z;
    ]], {
        -- <misc1-12.7>
    0,"0",0,"0.0"
        -- </misc1-12.7>
    })

test:do_execsql_test(
    "misc1-12.8",
    [[
        SELECT min(z), max(z), count(z) FROM t7 GROUP BY x ORDER BY 1;
    ]], {
        -- <misc1-12.8>
        1, 4, 4
        -- </misc1-12.8>
    })

test:do_execsql_test(
    "misc1-12.9",
    [[
        SELECT min(z), max(z), count(z) FROM t7 GROUP BY y ORDER BY 1;
    ]], {
        -- <misc1-12.9>
        1, 2, 2, 3, 4, 2
        -- </misc1-12.9>
    })

-- This used to be an error.  But we changed the code so that arbitrary
-- identifiers can be used as a collating sequence.  Collation is by text
-- if the identifier contains "text", "blob", or "clob" and is numeric
-- otherwise.
--
-- Update: In v3, it is an error again.
--
--do_test misc1-12.10 {
--  catchsql {
--    SELECT * FROM t6 ORDER BY a COLLATE unknown;
--  }
--} {0 {0 0 y 0}}

-- MUST_WORK_TEST collate
if 0>0 then
    -- Legacy from the original code. Must be replaced with analogue
    -- functions from box.
    local db = nil
    local X = nil
    db("collate", "numeric", "numeric_collate")
    local function numeric_collate(lhs, rhs) -- luacheck: no unused
        if (lhs == rhs)
        then
            return 0
        end
        return X(0, "X!expr", [=[["?:",[">",["lhs"],["rhs"]],3,["-",1]]]=])
    end

    -- Mimic the sql 2 collation type TEXT.
    db("collate", "text", "text_collate")
    local function numeric_collate(lhs, rhs) -- luacheck: no unused
        return X(34, "X!cmd", [=[["string","compare",["lhs"],["rhs"]]]=])
    end

test:do_execsql_test(
    "misc1-12.11",
    [[
        CREATE TABLE t8(x TEXT COLLATE numeric, y INTEGER COLLATE text, z  INT primary key);
        INSERT INTO t8 VALUES(0,0,1);
        INSERT INTO t8 VALUES(0.0,0,2);
        INSERT INTO t8 VALUES(0,0.0,3);
        INSERT INTO t8 VALUES(0.0,0.0,4);
        SELECT DISTINCT x, y FROM t8 ORDER BY z;
    ]], {
        -- <misc1-12.11>
        0, 0, 0.0, 0
        -- </misc1-12.11>
    })

test:do_execsql_test(
    "misc1-12.12",
    [[
        SELECT min(z), max(z), count(z) FROM t8 GROUP BY x ORDER BY 1;
    ]], {
        -- <misc1-12.12>
        1, 3, 2, 2, 4, 2
        -- </misc1-12.12>
    })

test:do_execsql_test(
    "misc1-12.13",
    [[
        SELECT min(z), max(z), count(z) FROM t8 GROUP BY y ORDER BY 1;
    ]], {
        -- <misc1-12.13>
        1, 4, 4
        -- </misc1-12.13>
    })
end

-- There was a problem with realloc() in the OP_MemStore operation of
-- the VDBE.  A buffer was being reallocated but some pointers into
-- the old copy of the buffer were not being moved over to the new copy.
-- The following code tests for the problem.
--
test:do_execsql_test(
    "misc1-13.1",
    [[
        CREATE TABLE t9(x TEXT,y  INT primary key);
        INSERT INTO t9 VALUES('one',1);
        INSERT INTO t9 VALUES('two',2);
        INSERT INTO t9 VALUES('three',3);
        INSERT INTO t9 VALUES('four',4);
        INSERT INTO t9 VALUES('five',5);
        INSERT INTO t9 VALUES('six',6);
        INSERT INTO t9 VALUES('seven',7);
        INSERT INTO t9 VALUES('eight',8);
        INSERT INTO t9 VALUES('nine',9);
        INSERT INTO t9 VALUES('ten',10);
        INSERT INTO t9 VALUES('eleven',11);
        SELECT y FROM t9
        WHERE x=(SELECT x FROM t9 WHERE y=1)
           OR x=(SELECT x FROM t9 WHERE y=2)
           OR x=(SELECT x FROM t9 WHERE y=3)
           OR x=(SELECT x FROM t9 WHERE y=4)
           OR x=(SELECT x FROM t9 WHERE y=5)
           OR x=(SELECT x FROM t9 WHERE y=6)
           OR x=(SELECT x FROM t9 WHERE y=7)
           OR x=(SELECT x FROM t9 WHERE y=8)
           OR x=(SELECT x FROM t9 WHERE y=9)
           OR x=(SELECT x FROM t9 WHERE y=10)
           OR x=(SELECT x FROM t9 WHERE y=11)
           OR x=(SELECT x FROM t9 WHERE y=12)
           OR x=(SELECT x FROM t9 WHERE y=13)
           OR x=(SELECT x FROM t9 WHERE y=14)
        ;
    ]], {
        -- <misc1-13.1>
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        -- </misc1-13.1>
    })



-- #
-- # The following tests can only work if the current sql VFS has the concept
-- # of a current directory.
-- #
-- ifcapable curdir {
-- # Make sure a database connection still works after changing the
-- # working directory.
-- #
-- do_test misc1-14.1 {
--   file mkdir tempdir
--   cd tempdir
--   execsql {BEGIN}
--   file exists ./test.db-journal
-- } {0}
-- do_test misc1-14.2a {
--   execsql {UPDATE t1 SET a=a||'x' WHERE 0}
--   file exists ../test.db-journal
-- } {0}
-- do_test misc1-14.2b {
--   execsql {UPDATE t1 SET a=a||'y' WHERE 1}
--   file exists ../test.db-journal
-- } {1}
-- do_test misc1-14.3 {
--   cd ..
--   forcedelete tempdir
--   execsql {COMMIT}
--   file exists ./test.db-journal
-- } {0}
-- }
-- # A failed create table should not leave the table in the internal
-- # data structures.  Ticket #238.
-- #
-- do_test misc1-15.1.1 {
--   catchsql {
--     CREATE TABLE t10 AS SELECT c1;
--   }
-- } {1 {no such column: c1}}
-- do_test misc1-15.1.2 {
--   catchsql {
--     CREATE TABLE t10 AS SELECT t9.c1;
--   }
-- } {1 {no such column: t9.c1}}
-- do_test misc1-15.1.3 {
--   catchsql {
--     CREATE TABLE t10 AS SELECT main.t9.c1;
--   }
-- } {1 {no such column: main.t9.c1}}
-- do_test misc1-15.2 {
--   catchsql {
--     CREATE TABLE t10 AS SELECT 1;
--   }
--   # The bug in ticket #238 causes the statement above to fail with
--   # the error "table t10 alread exists"
-- } {0 {}}
-- Test for memory leaks when a CREATE TABLE containing a primary key
-- fails.  Ticket #249.
--
test:do_test(
    "misc1-16.1",
    function()
        --catchsql {SELECT name FROM sql_master LIMIT 1}
        return test:catchsql([[
            CREATE TABLE test(a integer, primary key(a));
        ]])
    end, {
        -- <misc1-16.1>
        0
        -- </misc1-16.1>
    })

test:do_catchsql_test(
    "misc1-16.2",
    [[
        CREATE TABLE test(a integer, primary key(a));
    ]], {
        -- <misc1-16.2>
        1, "Space 'TEST' already exists"
        -- </misc1-16.2>
    })

test:do_catchsql_test(
    "misc1-16.3",
    [[
        CREATE TABLE test2(a text primary key, b text, primary key(a,b));
    ]], {
        -- <misc1-16.3>
        1, [[Failed to create space 'TEST2': primary key has been already declared]]
        -- </misc1-16.3>
    })

test:do_execsql_test(
    "misc1-16.4",
    [[
        INSERT INTO test VALUES(1);
        SELECT a, a FROM test;
    ]], {
        -- <misc1-16.4>
        1, 1
        -- </misc1-16.4>
    })

test:do_execsql_test(
    "misc1-16.5",
    [[
        INSERT INTO test VALUES(5);
        SELECT a, a FROM test;
    ]], {
        -- <misc1-16.5>
        1, 1, 5, 5
        -- </misc1-16.5>
    })

-- MUST_WORK_TEST NULL value for PK is prohibited
if (0 > 0) then
    -- Tarantool: NULL value for PK is prohibited
    -- when table is no-rowid. Comment the case
    test:do_execsql_test(
        "misc1-16.6",
        [[
            INSERT INTO test VALUES(NULL);
            SELECT a, a FROM test;
        ]], {
            -- <misc1-16.6>
            1, 1, 5, 5, 6, 6
            -- </misc1-16.6>
        })

    -- MUST_WORK_TEST
    -- Ticket #333: Temp triggers that modify persistent tables.
    --
    test:do_execsql_test(
        "misc1-17.1",
        [[
            START TRANSACTION;
            CREATE TABLE RealTable(TestID INTEGER PRIMARY KEY, TestString TEXT);
            CREATE TABLE TempTable(TestID INTEGER PRIMARY KEY, TestString TEXT);
            CREATE TRIGGER trigTest_1 AFTER UPDATE ON TempTable BEGIN
              INSERT INTO RealTable(TestString)
                 SELECT new.TestString FROM TempTable LIMIT 1;
            END;
            INSERT INTO TempTable(TestString) VALUES ('1');
            INSERT INTO TempTable(TestString) VALUES ('2');
            UPDATE TempTable SET TestString = TestString + 1 WHERE TestID=1 OR TestId=2;
            COMMIT;
            SELECT TestString FROM RealTable ORDER BY 1;
        ]], {
            -- <misc1-17.1>
            2, 3
            -- </misc1-17.1>
        })
end

-- Do not need sql_sleep
--test:do_test(
--    "misc1-18.1",
--    function()
--        n = sql_sleep(100)
--        return (n >= 100)
--    end, {
--        -- <misc1-18.1>
--        1
--        -- </misc1-18.1>
--    })


-- # 2014-01-10:  In a CREATE TABLE AS, if one or more of the column names
-- # are an empty string, that is still OK.
-- #
-- do_execsql_test misc1-19.1 {
--   CREATE TABLE t19 AS SELECT 1, 2 AS '', 3;
--   SELECT * FROM t19;
-- } {1 2 3}
-- do_execsql_test misc1-19.2 {
--   CREATE TABLE t19b AS SELECT 4 AS '', 5 AS '',  6 AS '';
--   SELECT * FROM t19b;
-- } {4 5 6}
-- # 2015-05-20:  CREATE TABLE AS should not store value is a TEXT
-- # column.
-- #
-- do_execsql_test misc1-19.3 {
--   CREATE TABLE t19c(x TEXT);
--   CREATE TABLE t19d AS SELECT * FROM t19c UNION ALL SELECT 1234;
--   SELECT x, typeof(x) FROM t19d;
-- } {1234 text}
-- # 2014-05-16:  Tests for the sql_TESTCTRL_FAULT_INSTALL feature.
-- #
-- unset -nocomplain fault_callbacks
-- set fault_callbacks {}
-- proc fault_callback {n} {
--   lappend ::fault_callbacks $n
--   return 0
-- }
-- do_test misc1-19.1 {
--   sql_test_control_fault_install fault_callback
--   set fault_callbacks
-- } {0}
-- do_test misc1-19.2 {
--   sql_test_control_fault_install
--   set fault_callbacks
-- } {0}
-- MUST_WORK_TEST
if (0 > 0) then
    -- 2015-01-26:  Valgrind-detected over-read.
    -- Reported on sql-users@sql.org by Michal Zalewski.  Found by afl-fuzz
    -- presumably.
    --
    test:do_execsql_test(
        "misc1-20.1",
        [[
            CREATE TABLE t0(x INTEGER DEFAULT(0==0) NOT NULL);
            REPLACE INTO t0(x) VALUES('');
            SELECT rowid, quote(x) FROM t0;
        ]], {
            -- <misc1-20.1>
            1, "''"
            -- </misc1-20.1>
        })

    -- 2015-03-22: NULL pointer dereference after a syntax error
end
test:do_catchsql_test(
    "misc1-21.1",
    [[
        select''like''like''like#0;
    ]], {
        -- <misc1-21.1>
        1, [[Syntax error at line 1 near '#0']]
        -- </misc1-21.1>
    })

test:do_catchsql_test(
    "misc1-21.2",
    [[
        VALUES(0,0x0MATCH#0;
    ]], {
        -- <misc1-21.2>
        1, [[Syntax error at line 1 near '#0']]
        -- </misc1-21.2>
    })

-- # 2015-04-19: NULL pointer dereference on a corrupt schema
-- #
-- do_execsql_test misc1-23.1 {
--   CREATE TABLE t1(x INT );
--   UPDATE sql_master SET sql='CREATE table t(d CHECK(T(#0)';
--   BEGIN;
--   CREATE TABLE t2(y INT );
--   ROLLBACK;
--   DROP TABLE IF EXISTS t3;
-- } {}
-- # 2015-04-19:  Faulty assert() statement
-- #
-- database_may_be_corrupt
-- do_catchsql_test misc1-23.2 {
--   CREATE TABLE t1(x  INT UNIQUE);
--   UPDATE sql_master SET sql='CREATE TABLE IF not EXISTS t(c)';
--   BEGIN;
--   CREATE TABLE t2(x INT );
--   ROLLBACK;
--   DROP TABLE F;
-- } {1 {no such table: F}}
-- do_catchsql_test misc1-23.3 {
--   CREATE TABLE t1(x  INT UNIQUE);
--   UPDATE sql_master SET sql='CREATE table y(a TEXT, a TEXT)';
--   BEGIN;
--   CREATE TABLE t2(y INT );
--   ROLLBACK;
--   DROP TABLE IF EXISTS t;
-- } {0 {}}
-- # At one point, running this would read one byte passed the end of a
-- # buffer, upsetting valgrind.
-- #
-- do_test misc1-24.0 {
--   list [catch { sql_prepare_v2 db ! -1 dummy } msg] $msg
-- } {1 {(1) unrecognized token: "!}}


test:finish_test()
