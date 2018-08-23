#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(121)

testprefix = "analyze9"

--!./tcltestrunner.lua
-- 2013 August 3
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
-- This file contains automated tests used to verify that the sqlite_stat4
-- functionality is working.
--

-- SQL Analyze is working correctly only with memtx now.
test:do_execsql_test(
    1.0,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a TEXT PRIMARY KEY, b TEXT); 
        INSERT INTO t1 VALUES('(0)', '(0)');
        INSERT INTO t1 VALUES('(1)', '(1)');
        INSERT INTO t1 VALUES('(2)', '(2)');
        INSERT INTO t1 VALUES('(3)', '(3)');
        INSERT INTO t1 VALUES('(4)', '(4)');
        CREATE INDEX i1 ON t1(a, b);
    ]], {
        -- <1.0>
        -- </1.0>
    })

test:do_execsql_test(
    1.1,
    [[
        ANALYZE;
    ]], {
        -- <1.1>
        -- </1.1>
    })

msgpack_decode_sample = function(txt)
    msgpack = require('msgpack')
    local i = 1
    local decoded_str = ''
    while msgpack.decode(txt)[i] ~= nil do
        if i == 1 then
            decoded_str = msgpack.decode(txt)[i]
        else 
            decoded_str = decoded_str.." "..msgpack.decode(txt)[i]
        end
        i = i+1
    end
    return decoded_str
end

box.internal.sql_create_function("msgpack_decode_sample", msgpack_decode_sample)

test:do_execsql_test(
    1.2,
    [[
        SELECT "tbl","idx","neq","nlt","ndlt",msgpack_decode_sample("sample") FROM "_sql_stat4" where "idx" = 'I1';
    ]], {
        -- <1.2>
        "T1", "I1", "1 1", "0 0", "0 0", "(0) (0)", "T1", "I1", "1 1", "1 1", "1 1", "(1) (1)", 
        "T1", "I1", "1 1", "2 2", "2 2", "(2) (2)", "T1", "I1", "1 1", "3 3", "3 3", "(3) (3)", 
        "T1", "I1", "1 1", "4 4", "4 4", "(4) (4)"
        -- </1.2>
    })

test:do_execsql_test(
    1.3,
    [[
        SELECT "tbl","idx","neq","nlt","ndlt",msgpack_decode_sample("sample") FROM "_sql_stat4" where "idx" = 'T1';

    ]], {
        -- <1.3>
        'T1', 'T1', '1', '0', '0', '(0)', 'T1', 'T1', '1', '1', '1', '(1)', 
        'T1', 'T1', '1', '2', '2', '(2)', 'T1', 'T1', '1', '3', '3', '(3)', 
        'T1', 'T1', '1', '4', '4', '(4)'
        -- </1.3>
    })


---------------------------------------------------------------------------
-- This is really just to test SQL user function "msgpack_decode_sample".
--
test:do_execsql_test(
    2.1,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a PRIMARY KEY, b);
        INSERT INTO t1 VALUES('some text', 14);
        INSERT INTO t1 VALUES(22.0, 'some text');
        CREATE INDEX i1 ON t1(a, b);
        ANALYZE;
        SELECT msgpack_decode_sample("sample") FROM "_sql_stat4";
    ]], {
        -- <2.1>
        "some text 14", "22 some text", "some text", 22
        -- </2.1>
    })

---------------------------------------------------------------------------
test:do_execsql_test(
    3.1,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t2(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b);
        CREATE INDEX i2 ON t2(a, b);
    ]])

test:do_test(
    3.2,
    function()
        for i = 0, 999 do
            local a = math.floor(i / 10)
            local b = math.random(0, 15)
            test:execsql(string.format("INSERT INTO t2 VALUES(null, %s, %s)", a, b))
        end
    end, {
        -- <3.2>
        -- </3.2>
    })

-- Analogue of function from tcl
lindex = function(str, pos)
    return string.sub(str, pos+1, pos+1)
end

box.internal.sql_create_function("lindex", lindex)

-- Analogue of function from tcl
lrange = function(str, first, last)
    local res_tokens = ""
    local i = 1
    for token in string.gmatch(str, "[^%s]+") do
        if i >= first and i <= last then
            if i == first then
                res_tokens = token
            else 
                res_tokens = res_tokens.." "..token
            end
        end
        i = i + 1
    end
    return res_tokens
end

box.internal.sql_create_function("lrange", lrange)

generate_tens = function(n)
    tens = {}
    for i = 1, n do
        tens[i] = 10
    end
    return tens
end

generate_tens_str = function(n)
    tens = {}
    for i = 1, n do
        tens[i] = "10"
    end
    return tens
end

-- Each value of "a" occurs exactly 10 times in the table.
--
test:do_execsql_test(
    "3.3.1",
    [[
        SELECT count(*) FROM t2 GROUP BY a;
    ]], generate_tens(100))

-- The first element in the "nEq" list of all samples should therefore be 10.
--      
test:do_execsql_test(
    "3.3.2",
    [[
        ANALYZE;
        SELECT lrange("neq", 1, 1) FROM "_sql_stat4" WHERE "idx" = 'I2';
    ]], generate_tens_str(24))

---------------------------------------------------------------------------
-- 
test:do_execsql_test(
    3.4,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b, c);
        INSERT INTO t1 VALUES(1, 1, 'one-a');
        INSERT INTO t1 VALUES(11, 1, 'one-b');
        INSERT INTO t1 VALUES(21, 1, 'one-c');
        INSERT INTO t1 VALUES(31, 1, 'one-d');
        INSERT INTO t1 VALUES(41, 1, 'one-e');
        INSERT INTO t1 VALUES(51, 1, 'one-f');
        INSERT INTO t1 VALUES(61, 1, 'one-g');
        INSERT INTO t1 VALUES(71, 1, 'one-h');
        INSERT INTO t1 VALUES(81, 1, 'one-i');
        INSERT INTO t1 VALUES(91, 1, 'one-j');
        INSERT INTO t1 SELECT a+1,2,'two' || substr(c,4) FROM t1;
        INSERT INTO t1 SELECT a+2,3,'three'||substr(c,4) FROM t1 WHERE c GLOB 'one-*';
        INSERT INTO t1 SELECT a+3,4,'four'||substr(c,4) FROM t1 WHERE c GLOB 'one-*';
        INSERT INTO t1 SELECT a+4,5,'five'||substr(c,4) FROM t1 WHERE c GLOB 'one-*';
        INSERT INTO t1 SELECT a+5,6,'six'||substr(c,4) FROM t1 WHERE c GLOB 'one-*';	
        CREATE INDEX t1b ON t1(b);
        ANALYZE;
        SELECT c FROM t1 WHERE b=3 AND a BETWEEN 30 AND 60;
    ]], {
        -- <3.4>
        "three-d", "three-e", "three-f"
        -- </3.4>
    })

---------------------------------------------------------------------------
-- These tests verify that the sample selection for stat4 appears to be 
-- working as designed.
--
test:do_execsql_test(
    4.0,
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b, c);
        CREATE INDEX i1 ON t1(c, b, a);
    ]])

insert_filler_rows_n = function(iStart, nCopy, nVal)
    for i = 0, nVal-1 do
        local iVal = iStart+i
        for j = 0, nCopy-1 do
            box.sql.execute(string.format("INSERT INTO t1 VALUES (null, %s, %s, %s)", iVal, iVal, iVal))
        end
    end
end

box.internal.sql_create_function("insert_filler_rows_n", insert_filler_rows_n)

test:do_test(
    4.1,
    function()
        insert_filler_rows_n(0, 10, 19)
        insert_filler_rows_n(20, 1, 100)
        return test:execsql([[
            INSERT INTO t1(id, c, b, a) VALUES(null, 200, 1, 'a');
            INSERT INTO t1(id, c, b, a) VALUES(null, 200, 1, 'b');
            INSERT INTO t1(id, c, b, a) VALUES(null, 200, 1, 'c');

            INSERT INTO t1(id, c, b, a) VALUES(null, 200, 2, 'e');
            INSERT INTO t1(id, c, b, a) VALUES(null, 200, 2, 'f');

            INSERT INTO t1(id, c, b, a) VALUES(null, 201, 3, 'g');
            INSERT INTO t1(id, c, b, a) VALUES(null, 201, 4, 'h');

            ANALYZE;
            SELECT count(*) FROM "_sql_stat4";

        ]])
        end, {
            -- <4.1>
            48
            -- </4.1>
        })

test:do_execsql_test(
    4.2,
    [[
        SELECT count(*) FROM t1;
    ]], {
        -- <4.2>
        297
        -- </4.2>
    })

test:do_execsql_test(
    4.3,
    [[
        SELECT "neq", lrange("nlt", 1, 3), lrange("ndlt", 1, 3), lrange(msgpack_decode_sample("sample"), 1, 3) 
            FROM "_sql_stat4" WHERE "idx" = 'I1' ORDER BY "sample" LIMIT 16;
    ]], {
        -- <4.3>
        "10 10 10","0 0 0","0 0 0","0 0 0","10 10 10","10 10 10","1 1 1","1 1 1","10 10 10","20 20 20",
        "2 2 2","2 2 2","10 10 10","30 30 30","3 3 3","3 3 3","10 10 10","40 40 40","4 4 4","4 4 4",
        "10 10 10","50 50 50","5 5 5","5 5 5","10 10 10","60 60 60","6 6 6","6 6 6","10 10 10","70 70 70",
        "7 7 7","7 7 7","10 10 10","80 80 80","8 8 8","8 8 8","10 10 10","90 90 90","9 9 9","9 9 9",
        "10 10 10","100 100 100","10 10 10","10 10 10","10 10 10","110 110 110","11 11 11","11 11 11",
        "10 10 10","120 120 120","12 12 12","12 12 12","10 10 10","130 130 130","13 13 13","13 13 13",
        "10 10 10","140 140 140","14 14 14","14 14 14","10 10 10","150 150 150","15 15 15","15 15 15"
        -- </4.3>
    })

test:do_execsql_test(
    4.4,
    [[
        SELECT "neq", lrange("nlt", 1, 3), lrange("ndlt", 1, 3), lrange(msgpack_decode_sample("sample"), 1, 3) 
        FROM "_sql_stat4" WHERE "idx" = 'I1' ORDER BY "sample" DESC LIMIT 2;
    ]], {
        -- <4.4>
        "2 1 1","295 296 296","120 122 125","201 4 h","5 3 1","290 290 291","119 119 120","200 1 b"
        -- </4.4>
    })

test:do_execsql_test(
    4.5,
    [[
        SELECT count(DISTINCT c) FROM t1 WHERE c<201 
    ]], {
        -- <4.5>
        120
        -- </4.5>
    })

test:do_execsql_test(
    4.6,
    [[
        SELECT count(DISTINCT c) FROM t1 WHERE c<200 
    ]], {
        -- <4.6>
        119
        -- </4.6>
    })

-- Check that the perioidic samples are present.
test:do_execsql_test(
    4.7,
    [[
        SELECT count(*) FROM "_sql_stat4" WHERE msgpack_decode_sample("sample") IN (34, 68, 102, 136, 170, 204, 238, 272);
    ]], {
        -- <4.7>
        8
        -- </4.7>
    })

-- reset_db()
test:do_test(
    4.8,
    function()
        test:execsql([[
            DROP TABLE IF EXISTS t1;
            CREATE TABLE t1(o,t INTEGER PRIMARY KEY);
            CREATE INDEX i1 ON t1(o);
        ]])
        for i = 0, 9999, 10 do
            test:execsql(" INSERT INTO t1 VALUES('x', "..i..") ")
        end
        return test:execsql([[
            ANALYZE;
            SELECT count(*) FROM "_sql_stat4";
        ]])
        end, {
            -- <4.8>
            25
            -- </4.8>
        })

test:do_execsql_test(
    4.9,
    [[
        SELECT msgpack_decode_sample("sample") FROM "_sql_stat4";
    ]], {
        -- <4.9>
        "x", 1110, 2230, 2750, 3350, 4090, 4470, 4980, 5240, 5280, 5290, 5590, 5920, 
        5930, 6220, 6710, 7000, 7710, 7830, 7970, 8890, 8950, 9240, 9250, 9680
        -- </4.9>
    })

---------------------------------------------------------------------------
-- This was also crashing (corrupt sqlite_stat4 table).

test:do_execsql_test(
    6.1,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b);
        CREATE INDEX i1 ON t1(a);
        CREATE INDEX i2 ON t1(b);
        INSERT INTO t1 VALUES(null, 1, 1);
        INSERT INTO t1 VALUES(null, 2, 2);
        INSERT INTO t1 VALUES(null, 3, 3);
        INSERT INTO t1 VALUES(null, 4, 4);
        INSERT INTO t1 VALUES(null, 5, 5);
        ANALYZE;
        CREATE TABLE x1(tbl, idx, neq, nlt, ndlt, sample, PRIMARY KEY(tbl, idx, sample)); 
        INSERT INTO x1 SELECT * FROM "_sql_stat4";
        DELETE FROM "_sql_stat4";
        INSERT INTO "_sql_stat4" SELECT * FROM x1;
        ANALYZE;
    ]])

test:do_execsql_test(
    6.2,
    [[
        SELECT * FROM t1 WHERE a = 'abc';
    ]])

---------------------------------------------------------------------------
-- The following tests experiment with adding corrupted records to the
-- 'sample' column of the _sql_stat4 table.
--
test:do_execsql_test(
    7.1,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b);
        CREATE INDEX i1 ON t1(a, b);
        INSERT INTO t1 VALUES(null, 1, 1);
        INSERT INTO t1 VALUES(null, 2, 2);
        INSERT INTO t1 VALUES(null, 3, 3);
        INSERT INTO t1 VALUES(null, 4, 4);
        INSERT INTO t1 VALUES(null, 5, 5);
        ANALYZE;
        UPDATE "_sql_stat4" SET "sample" = '' WHERE "sample" =
            (SELECT "sample" FROM "_sql_stat4" WHERE "tbl" = 't1' AND "idx" = 'i1' LIMIT 1);
        ANALYZE;
    ]])

-- Doesn't work due to the fact that in Tarantool rowid has been removed,
-- and tbl, idx and sample have been united into primary key.
-- test:do_execsql_test(
--    7.2,
--    [[
--        UPDATE _sql_stat4 SET sample = X'FFFF';
--        ANALYZE;
--        SELECT * FROM t1 WHERE a = 1;
--    ]], {
--        -- <7.2>
--        1, 1
--        -- </7.2>
--    })

test:do_execsql_test(
    7.3,
    [[
        UPDATE "_sql_stat4" SET "neq" = '0 0 0';
        ANALYZE;
        SELECT * FROM t1 WHERE a = 1;
    ]], {
        -- <7.3>
        1, 1, 1
        -- </7.3>
    })

test:do_execsql_test(
    7.4,
    [[
        ANALYZE;
        UPDATE "_sql_stat4" SET "ndlt" = '0 0 0';
        ANALYZE;
        SELECT * FROM t1 WHERE a = 3;
    ]], {
        -- <7.4>
        3, 3, 3
        -- </7.4>
    })

test:do_execsql_test(
    7.5,
    [[
        ANALYZE;
        UPDATE "_sql_stat4" SET "nlt" = '0 0 0';
        ANALYZE;
        SELECT * FROM t1 WHERE a = 5;
    ]], {
        -- <7.5>
        5, 5, 5
        -- </7.5>
    })

---------------------------------------------------------------------------
--
test:do_execsql_test(
    8.1,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(id PRIMARY KEY, x TEXT);
        CREATE INDEX i1 ON t1(x);
        INSERT INTO t1 VALUES(1, '1');
        INSERT INTO t1 VALUES(2, '2');
        INSERT INTO t1 VALUES(3, '3');
        INSERT INTO t1 VALUES(4, '4');
        ANALYZE;
    ]])

test:do_execsql_test(
    8.2,
    [[
        SELECT * FROM t1 WHERE x = 3;
    ]], {
        -- <8.2>
        3, '3'
        -- </8.2>
    })

---------------------------------------------------------------------------
-- Check that the bug fixed by [91733bc485] really is fixed.
--
-- Commented due to assertion(#2847)
-- test:do_execsql_test(
--     9.1,
--     [[
--         DROP TABLE IF EXISTS t1;
--         CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b, c, d, e);
--         CREATE INDEX i1 ON t1(a, b, c, d);
--         CREATE INDEX i2 ON t1(e);
--     ]])

-- test:do_test(
--     9.2,
--     function()
--         for i = 0, 100 do
--             test:execsql(string.format("INSERT INTO t1 VALUES(null, 'x', 'y', 'z', %s, %s);", i, math.floor(i / 2)))
--         end
--         for i = 0, 20 do
--             test:execsql("INSERT INTO t1 VALUES(null, 'x', 'y', 'z', 101, "..i..");")
--         end
--         for i = 102, 200 do
--             test:execsql(string.format("INSERT INTO t1 VALUES(null, 'x', 'y', 'z', %s, %s);", i, math.floor(i / 2)))
--         end
--         return test:execsql("ANALYZE")
--     end, {
--         -- <9.2>
--         -- </9.2>
--     })

-- test:do_eqp_test(
--     "9.3.1",
--     [[
--         SELECT * FROM t1 WHERE a='x' AND b='y' AND c='z' AND d=101 AND e=5;
--     ]], {
--         -- <9.3.1>
--         "/t1 USING INDEX i2/"
--         -- </9.3.1>
--     })

-- test:do_eqp_test(
--     "9.3.2",
--     [[
--         SELECT * FROM t1 WHERE a='x' AND b='y' AND c='z' AND d=99 AND e=5;
--     ]], {
--         -- <9.3.2>
--         "/t1 USING INDEX i1/"
--         -- </9.3.2>
--     })

-- test:do_eqp_test(
--     "9.4.1",
--     [[
--         SELECT * FROM t1 WHERE a='x' AND b='y' AND c='z' AND d=101 AND e=5
--     ]], {
--         -- <9.4.1>
--         "/t1 USING INDEX i2/"
--         -- </9.4.1>
--     })

-- test:do_eqp_test(
--     "9.4.2",
--     [[
--         SELECT * FROM t1 WHERE a='x' AND b='y' AND c='z' AND d=99 AND e=5
--     ]], {
--         -- <9.4.2>
--         "/t1 USING INDEX i1/"
--         -- </9.4.2>
--     })

---------------------------------------------------------------------------
-- Check that the planner takes stat4 data into account when considering
-- "IS NULL" and "IS NOT NULL" constraints.
--
test:do_execsql_test(
    "10.1.1",
    [[
        DROP TABLE IF EXISTS t3;
        CREATE TABLE t3(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b);
        CREATE INDEX t3a ON t3(a);
        CREATE INDEX t3b ON t3(b);
    ]])

test:do_test(
    "10.1.2",
    function()
        local a = 0
        for i = 1, 100 do
            if i > 90 then
                a = i
            else
                a = "NULL"
        end
        local b = i % 5
        test:execsql(string.format("INSERT INTO t3 VALUES(null, %s, %s)", a, b))
    end
        return test:execsql("ANALYZE")
    end, {
        -- <10.1.2>      
        -- </10.1.2>
    })

test:do_execsql_test(
    "10.1.3",
    [[
       EXPLAIN QUERY PLAN SELECT * FROM t3 WHERE a IS NULL AND b = 2;
    ]], {
        -- <10.1.3>
        0, 0, 0, "SEARCH TABLE T3 USING COVERING INDEX T3B (B=?)"
        -- </10.1.3>
    })

test:do_execsql_test(
    "10.1.4",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t3 WHERE a IS NOT NULL AND b = 2;
    ]], {
        -- <10.1.4>
        0, 0, 0, "SEARCH TABLE T3 USING COVERING INDEX T3A (A>?)"
        -- </10.1.4>
    })

test:do_execsql_test(
    "10.2.1",
    [[
        DROP TABLE IF EXISTS t3;
        CREATE TABLE t3(id INTEGER PRIMARY KEY AUTOINCREMENT, x, a, b);
        CREATE INDEX t3a ON t3(x, a);
        CREATE INDEX t3b ON t3(x, b);
    ]])

test:do_test(
    "10.2.2",
    function()
        local a = 0
        for i = 1, 100 do
            if i > 90 then
                a = i
            else
                a = "NULL"
        end
        local b = i % 5
        test:execsql(string.format("INSERT INTO t3 VALUES(null, 'xyz', %s, %s);", a, b))
    end
        return test:execsql("ANALYZE")
    end, {
        -- <10.2.2>    
        -- </10.2.2>
    })

test:do_execsql_test(
    "10.2.3",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t3 WHERE x = 'xyz' AND a IS NULL AND b = 2;
    ]], {
        -- <10.2.3>
        0, 0, 0, "SEARCH TABLE T3 USING COVERING INDEX T3B (X=? AND B=?)"
        -- </10.2.3>
    })

test:do_execsql_test(
    "10.2.4",
    [[
       EXPLAIN QUERY PLAN SELECT * FROM t3 WHERE x = 'xyz' AND a IS NOT NULL AND b = 2;
    ]], {
        -- <10.2.4>
        0, 0, 0, "SEARCH TABLE T3 USING COVERING INDEX T3A (X=? AND A>?)"
        -- </10.2.4>
    })

---------------------------------------------------------------------------
-- Check that stat4 data is used correctly with non-default collation
-- sequences.
--
test:do_execsql_test(
    "11.0",
    [[
        CREATE TABLE t4(id INTEGER PRIMARY KEY AUTOINCREMENT, a COLLATE "unicode_ci", b);
        CREATE INDEX t4b ON t4(b);
        CREATE INDEX t4a ON t4(a);
    ]], {
        -- <11.0>
        -- </11.0>
    })

test:do_test(
    11.1,
    function()
        local a = 0
        for i = 0, 100 do
            if i % 10 == 0 then 
                a = "\"ABC\""
            else
                a = "\"DEF\""
            end
            b = i % 5
            test:execsql(string.format("INSERT INTO t4 VALUES(null, '%s', '%s')", a, b))
        test:execsql("ANALYZE")
        end
    end, {
        -- <11.1>
        -- </11.1>
    })

test:do_execsql_test(
    "11.2", 
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE a = 'def' AND b = 3;
    ]], {
        -- <11.2>
        0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX T4B (B=?)"
        -- </11.2>
    })

test:do_execsql_test(
    "11.3", 
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE a = 'abc' AND b = 3;
    ]], {
        -- <11.3>
        0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX T4B (B=?)"
        -- </11.3>
    })

test:do_execsql_test(
    "11.4",
    [[
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t4(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b);
        CREATE INDEX t4b ON t4(b);
        CREATE INDEX t4a ON t4(a COLLATE "unicode_ci");
    ]], {
        -- <11.4>
        -- </11.4>
    })

test:do_test(
    11.5,
    function()
        local a = 0
        for i = 0, 100 do
            if i % 10 == 0 then 
                a = "\"ABC\""
            else
                a = "\"DEF\""
            end
            b = i % 5
            test:execsql(string.format("INSERT INTO t4 VALUES(null, '%s', '%s')", a, b))
        test:execsql("ANALYZE")
        end
    end, {
        -- <11.5>
        -- </11.5>
    })

test:do_execsql_test(
    "11.6", 
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE a = 'def' AND b = 3;
    ]], {
        -- <11.6>
        0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX T4B (B=?)"
        -- </11.6>
    })

test:do_execsql_test(
    "11.7", 
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE a = 'abc' COLLATE "unicode_ci" AND b = 3;
    ]], {
        -- <11.7>
        0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX T4B (B=?)"
        -- </11.7>
    })

test:do_execsql_test(
    "11.8", 
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE a COLLATE "unicode_ci" = 'abc' AND b = 3;
    ]], {
        -- <11.8>
        0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX T4B (B=?)"
        -- </11.8>
    })

test:do_execsql_test(
    "12.0",
    [[
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t4(id INTEGER PRIMARY KEY AUTOINCREMENT, x, a COLLATE "unicode_ci", b);
        CREATE INDEX t4b ON t4(x, b);
        CREATE INDEX t4a ON t4(x, a);
    ]], {
        -- <12.0>
        -- </12.0>
    })

test:do_test(
    12.1,
    function()
        local a = 0
        for i = 0, 100 do
            if i % 10 == 0 then 
                a = "\"ABC\""
            else
                a = "\"DEF\""
            end
            b = i % 5
            test:execsql(string.format("INSERT INTO t4 VALUES(null, 'abcdef', '%s', '%s')", a, b))
        test:execsql("ANALYZE")
        end
    end, {
        -- <12.1>
        -- </12.1>
    })

test:do_execsql_test(
    "12.2", 
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE x = 'abcdef' AND a = 'def' AND b = 3;
    ]], {
        -- <12.2>
        0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX T4B (X=? AND B=?)"
        -- </12.2>
    })

test:do_execsql_test(
    "12.3", 
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE x = 'abcdef' AND a = 'abc' AND b = 3;
    ]], {
        -- <12.3>
        0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX T4B (X=? AND B=?)"
        -- </12.3>
    })

test:do_execsql_test(
    "12.4",
    [[
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t4(id INTEGER PRIMARY KEY AUTOINCREMENT, x, a, b);
        CREATE INDEX t4b ON t4(x, b);
        CREATE INDEX t4a ON t4(x, a COLLATE "unicode_ci");
    ]], {
        -- <12.4>
        -- </12.4>
    })

test:do_test(
    12.5,
    function()
        local a = 0
        for i = 0, 100 do
            if i % 10 == 0 then 
                a = "\"ABC\""
            else
                a = "\"DEF\""
            end
            b = i % 5
            test:execsql(string.format("INSERT INTO t4 VALUES(null, 'abcdef', '%s', '%s')", a, b))
        test:execsql("ANALYZE")
        end
    end, {
        -- <12.5>
        -- </12.5>
    })

test:do_execsql_test(
    "12.6", 
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE x = 'abcdef' AND a = 'def' AND b = 3;
    ]], {
        -- <12.6>
        0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX T4B (X=? AND B=?)"
        -- </12.6>
    })

test:do_execsql_test(
    "12.7", 
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE x= 'abcdef' AND a = 'abc' COLLATE "unicode_ci" AND b = 3;
    ]], {
        -- <12.7>
        0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX T4B (X=? AND B=?)"
        -- </12.7>
    })

test:do_execsql_test(
    "12.8", 
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE x = 'abcdef' AND a COLLATE "unicode_ci" = 'abc' AND b = 3;
    ]], {
        -- <12.8>
        0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX T4B (X=? AND B=?)"
        -- </12.8>
    })

---------------------------------------------------------------------------
-- Check that affinities are taken into account when using stat4 data to
-- estimate the number of rows scanned by an id constraint.

test:do_test(
    13.1,
    function()
        test:execsql("DROP TABLE IF EXISTS t1;")
        test:execsql("CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b, c, d);")
        test:execsql("CREATE INDEX i1 ON t1(a);")
        test:execsql("CREATE INDEX i2 ON t1(b, c);")
        local a = 0
        for i = 0, 100 do
            if i % 2 == 1 then
                a = "\"abc\""
            else
                a = "\"def\""
            end
            test:execsql(string.format("INSERT INTO t1(id, a, b, c) VALUES(null, '%s', %s, %s)", a, i, i))
        test:execsql("ANALYZE;")
        end
    end, {
        -- <13.1>
        -- </13.1>
    })

test:do_execsql_test(
    "13.2.1",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a='abc' AND id<15 AND b<12;
    ]], {
        -- <13.2.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"
        -- </13.2.1>
    })

test:do_execsql_test(
    "13.2.2",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a='abc' AND id<'15' AND b<12;
    ]], {
        -- <13.2.2>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"
        -- </13.2.2>
    })

test:do_execsql_test(
    "13.3.1",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a='abc' AND id<100 AND b<12;
    ]], {
        -- <13.3.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"
        -- </13.3.1>
    })

test:do_execsql_test(
    "13.3.2",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a='abc' AND id<'100' AND b<12;
    ]], {
        -- <13.3.2>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=?)"
        -- </13.3.2>
    })

---------------------------------------------------------------------------
-- Check also that affinities are taken into account when using stat4 data 
-- to estimate the number of rows scanned by any other constraint on a 
-- column other than the leftmost.
--
test:do_test(
    14.1,
    function()
        test:execsql("DROP TABLE IF EXISTS t1")
        test:execsql("CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b INTEGER, c)")
        for i = 0, 100 do
            local c = i % 3
            test:execsql(string.format(" INSERT INTO t1 VALUES(null, 'ott', %s, %s) ", i, c))
        end
        return test:execsql([[
                CREATE INDEX i1 ON t1(a, b);
                CREATE INDEX i2 ON t1(c);
                ANALYZE;
            ]])
        end, {
        -- <14.1>    
        -- </14.1>
        })

test:do_execsql_test(
    "14.2.1",
    [[
       EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a='ott' AND b<10 AND c=1;
    ]], {
        -- <13.2.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=? AND B<?)"
        -- </13.2.1>
    })

test:do_execsql_test(
    "14.2.2",
    [[
       EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a='ott' AND b<'10' AND c=1;
    ]], {
        -- <13.2.2>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=? AND B<?)"
        -- </13.2.2>
    })

---------------------------------------------------------------------------
-- Test that nothing untoward happens if the stat4 table contains entries
-- for indexes that do not exist.
-- Or NULL values in any of the other columns except for PK.
--
test:do_execsql_test(
    15.1,
    [[
        DROP TABLE IF EXISTS x1;
        CREATE TABLE x1(a PRIMARY KEY, b, UNIQUE(a, b));
        INSERT INTO x1 VALUES(1, 2);
        INSERT INTO x1 VALUES(3, 4);
        INSERT INTO x1 VALUES(5, 6);
        ANALYZE;
        INSERT INTO "_sql_stat4" VALUES('x1', 'abc', '', '', '', '');
    ]])

test:do_execsql_test(
    15.2,
    [[
        SELECT * FROM x1; 
    ]], {
        -- <15.2>
        1, 2, 3, 4, 5, 6
        -- </15.2>
    })

test:do_execsql_test(
    15.3,
    [[
        INSERT INTO "_sql_stat4" VALUES('42', '42', '42', '42', '42', 42);
    ]])

test:do_execsql_test(
    15.4,
    [[
        SELECT * FROM x1;
    ]], {
        -- <15.4>
        1, 2, 3, 4, 5, 6
        -- </15.4>
    })

test:do_execsql_test(
    15.7,
    [[
        ANALYZE;
        UPDATE "_sql_stat1" SET "tbl" = 'no such tbl';
    ]])

test:do_execsql_test(
    15.8,
    [[
        SELECT * FROM x1 ;
    ]], {
        -- <15.8>
        1, 2, 3, 4, 5, 6
        -- </15.8>
    })

-- Tarantool: this test seems to be useless. There's no reason
-- for these fields to be nullable.
-- test:do_execsql_test(
--    15.9,
--    [[
--        ANALYZE;
--        UPDATE "_sql_stat4" SET "neq" = NULL, "nlt" = NULL, "ndlt" = NULL;
--    ]])

test:do_execsql_test(
    15.10,
    [[
        SELECT * FROM x1;
    ]], {
        -- <15.10>
        1, 2, 3, 4, 5, 6
        -- </15.10>
    })

-- This is just for coverage....
test:do_execsql_test(
    15.11,
    [[
        ANALYZE;
        UPDATE "_sql_stat1" SET "stat" = "stat" || ' unordered';
    ]])

test:do_execsql_test(
    15.12,
    [[
        SELECT * FROM x1;
    ]], {
        -- <15.12>
        1, 2, 3, 4, 5, 6
        -- </15.12>
    })
---------------------------------------------------------------------------
-- Test that stat4 data may be used with partial indexes.
--
test:do_test(
    17.1,
    function()
        test:execsql([[
            DROP TABLE IF EXISTS t1;
            CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b, c, d);
            CREATE INDEX i1 ON t1(a, b);
            INSERT INTO t1 VALUES(null, -1, -1, -1, NULL);
            INSERT INTO t1 SELECT null, 2*a,2*b,2*c,d FROM t1;
            INSERT INTO t1 SELECT null, 2*a,2*b,2*c,d FROM t1;
            INSERT INTO t1 SELECT null, 2*a,2*b,2*c,d FROM t1;
            INSERT INTO t1 SELECT null, 2*a,2*b,2*c,d FROM t1;
            INSERT INTO t1 SELECT null, 2*a,2*b,2*c,d FROM t1;
            INSERT INTO t1 SELECT null, 2*a,2*b,2*c,d FROM t1;
        ]])
        local b = 0
        for i = 0, 31 do
            if (i < 8) then
                b = 0
            else
                b = i
        end
        test:execsql(string.format(" INSERT INTO t1 VALUES(null, %s%%2, %s, %s/2, 'abc') ", i, b, i))
    end
    return test:execsql("ANALYZE")
    end, {
        -- <17.1>
        -- </17.1>
    })

test:do_execsql_test(
    17.2,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE d IS NOT NULL AND a=0 AND b=10 AND c=10;
    ]], {
        -- <17.2>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX I1 (A=? AND B=?)'
        -- </17.2>
    })

test:do_execsql_test(
    17.3,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE d IS NOT NULL AND a=0 AND b=0 AND c=10;
    ]], {
        -- <17.3>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=? AND B=?)"
        -- </17.3>
    })

test:do_execsql_test(
    17.4,
    [[
        CREATE INDEX i2 ON t1(c, d);
        ANALYZE;
    ]])

test:do_execsql_test(
    17.5,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE d IS NOT NULL AND a=0 AND b=10 AND c=10;
    ]], {
        -- <17.5>
	0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (A=? AND B=?)"
        -- </17.5>
    })

test:do_execsql_test(
    17.6,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE d IS NOT NULL AND a=0 AND b=0 AND c=10;
    ]], {
        -- <17.6>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I2 (C=? AND D>?)"
        -- </17.6>
    })

---------------------------------------------------------------------------

test:do_test(
    18.1,
    function()
        test:execsql([[
            DROP TABLE IF EXISTS t1;
            CREATE TABLE t1(a PRIMARY KEY, b);
            CREATE INDEX i1 ON t1(a, b);
        ]])
        for i = 0, 8 do
            test:execsql(string.format("INSERT INTO t1 VALUES(%s, 0);", i))
        end
        test:execsql("ANALYZE")
        return test:execsql([[ SELECT count(*) FROM "_sql_stat4" WHERE "idx" = 'I1'; ]])
    end, {
        -- <18.1>
        9
        -- </18.1>
    })

---------------------------------------------------------------------------

r = function()
    return math.random(1, 15)
end

box.internal.sql_create_function("r", r)

test:do_test(
    20.1,
    function()
        test:execsql([[
            DROP TABLE IF EXISTS t1;
            DROP TABLE IF EXISTS x1;
            DROP TABLE IF EXISTS t3;
            CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a,b,c,d);
            CREATE INDEX i1 ON t1(a,b,c,d);
        ]])
        for i = 0, 23 do
            test:execsql(string.format("INSERT INTO t1 VALUES(null, %s, %s, r(), r());", i, i))
        end
    end, {
        -- <20.1>
        -- </20.1>
    })

test:do_execsql_test(
    20.2,
    [[
        ANALYZE;
    ]], {
        -- <20.2>
        -- </20.2>
    })

for i = 0, 15 do
    test:do_test(
        "20.3."..i,
        function()
            return test:execsql(string.format(
                [[SELECT count(*) FROM "_sql_stat4" WHERE "idx" = 'I1' AND lrange(msgpack_decode_sample("sample"), 1, 1) = '%s']], i))
        end, {
            1
        })
end

---------------------------------------------------------------------------
--
test:do_execsql_test(
    21.0,
    [[
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t2(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b);
        CREATE INDEX i2 ON t2(a);
    ]])

test:do_test(
    21.1,
    function()
        for i = 1, 100 do
            test:execsql(string.format([[
                INSERT INTO t2 VALUES(null, CASE WHEN %s < 80 THEN 'one' ELSE 'two' END, %s) 
                ]], i, i))
        end
        return test:execsql("ANALYZE")
    end, {
        -- <21.1>
        -- </21.1>
    })

test:do_execsql_test(
    21.2,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t2 WHERE a='one' AND id < 10;
    ]], {
        -- <21.2>
        0, 0, 0, "SEARCH TABLE T2 USING PRIMARY KEY (ID<?)"
        -- </21.2>
    })

test:do_execsql_test(
    21.3,
    [[
       EXPLAIN QUERY PLAN SELECT * FROM t2 WHERE a='one' AND id < 50
    ]], {
        -- <21.3>
        0, 0, 0, "SEARCH TABLE T2 USING PRIMARY KEY (ID<?)"
        -- </21.3>
    })

---------------------------------------------------------------------------
--
test:do_execsql_test(
    22.0,
    [[
        DROP TABLE IF EXISTS t3;
        CREATE TABLE t3(a, b, c, d, PRIMARY KEY(a, b));
    ]])

test:do_execsql_test(
    22.1,
    [[
        WITH r(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM r WHERE x<=100) 
        INSERT INTO t3 SELECT CASE WHEN (x>45 AND x<96) THEN 'B' ELSE 'A' END,
            x, CASE WHEN (x<51) THEN 'one' ELSE 'two' END, x FROM r;

        CREATE INDEX i3 ON t3(c);
        CREATE INDEX i4 ON t3(d);
        ANALYZE;
    ]])

test:do_execsql_test(
    22.2,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t3 WHERE c = 'one' AND a = 'B' AND d < 20;
    ]], {
        -- <22.2>
        0, 0, 0, "SEARCH TABLE T3 USING COVERING INDEX I4 (D<?)"
        -- </22.2>
    })

test:do_execsql_test(
    22.3,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t3 WHERE c = 'one' AND a = 'A' AND d < 20;
    ]], {
        -- <22.2>
        0, 0, 0, "SEARCH TABLE T3 USING COVERING INDEX I4 (D<?)"
        -- </22.2>
    })


int_to_char = function(i)
    local ret = ""
    local char = "abcdefghij"
    local divs = {1000, 100, 10, 1}
    for _, div in ipairs(divs) do
        ret = ret .. lindex(char, math.floor(i/div) % 10)
    end
    return ret
end

box.internal.sql_create_function("int_to_char", int_to_char)

-- These tests are commented until query planer will be stable.
--test:do_execsql_test(
--   23.0,
--   [[
--       DROP TABLE IF EXISTS t4;
--       CREATE TABLE t4(a COLLATE "unicode_ci", b, c, d, e, f, PRIMARY KEY(c, b, a));
--       CREATE INDEX i41 ON t4(e);
--       CREATE INDEX i42 ON t4(f);
--
--       WITH data(a, b, c, d, e, f) AS (SELECT int_to_char(0), 'xyz', 'zyx', '*', 0, 0 UNION ALL
--           SELECT int_to_char(f+1), b, c, d, (e+1) % 2, f+1 FROM data WHERE f<1024)
--               INSERT INTO t4 SELECT a, b, c, d, e, f FROM data;
--       ANALYZE;
--   ]], {
--       -- <23.0>
--       -- </23.0>
--   })
--
--test:do_execsql_test(
--   23.1,
--   [[
--       EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE (e=1 AND b='xyz' AND c='zyx' AND a<'AEA') AND f<300;
--   ]], {
--       -- <23.1>
--       0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX I42 (F<?)"
--       -- </23.1>
--   })
--
--test:do_execsql_test(
--   23.2,
--   [[
--       EXPLAIN QUERY PLAN SELECT * FROM t4 WHERE (e=1 AND b='xyz' AND c='zyx' AND a<'JJJ') AND f<300;
--   ]], {
--       -- <23.2>
--       0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX I42 (F<?)"
--       -- </23.2>
--   })
--
test:do_execsql_test(
    24.0,
    [[
        CREATE TABLE t5(c, d, b, e, a, PRIMARY KEY(a, b, c));
        WITH data(a, b, c, d, e) AS (SELECT 'z', 'y', 0, 0, 0 UNION ALL 
            SELECT a, CASE WHEN b='y' THEN 'n' ELSE 'y' END, c+1, e/250, e+1 FROM data WHERE e<1000) 
                INSERT INTO t5(a, b, c, d, e) SELECT * FROM data;
        CREATE INDEX t5d ON t5(d);
        CREATE INDEX t5e ON t5(e);
        ANALYZE;
    ]])


test:do_execsql_test(
    24.1,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t5 WHERE d=0 AND a='z' AND b='n' AND e<200;
    ]], {
        0, 0, 0, "SEARCH TABLE T5 USING COVERING INDEX T5E (E<?)"
    })

test:do_execsql_test(
    24.2,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t5 WHERE d=0 AND a='z' AND b='n' AND e<100;
    ]], {
        0, 0, 0, "SEARCH TABLE T5 USING COVERING INDEX T5E (E<?)"
    })

test:do_execsql_test(
    24.3,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t5 WHERE d=0 AND e<300;
    ]], {
        0, 0, 0, "SEARCH TABLE T5 USING COVERING INDEX T5D (D=?)"
    })

test:do_execsql_test(
    24.4,
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t5 WHERE d=0 AND e<200;
    ]], {
        0, 0, 0, "SEARCH TABLE T5 USING COVERING INDEX T5E (E<?)"
    })


---------------------------------------------------------------------------
-- Test that if stat4 data is available but cannot be used because the
-- rhs of a range constraint is a complex expression, the default estimates
-- are used instead.
--
test:do_execsql_test(
    25.1,
    [[
        DROP TABLE IF EXISTS t6;
        DROP TABLE IF EXISTS ints;
        CREATE TABLE t6(id INTEGER PRIMARY KEY AUTOINCREMENT, a, b);
        WITH ints(i,j) AS (SELECT 1,1 UNION ALL SELECT i+1,j+1 FROM ints WHERE i<100) 
            INSERT INTO t6 SELECT null,* FROM ints;
        CREATE INDEX aa ON t6(a);
        CREATE INDEX bb ON t6(b);
        ANALYZE;
    ]])

-- Term (b<?) is estimated at 25%. Better than (a<30) but not as
-- good as (a<20).
test:do_execsql_test(
    "25.2.1",
    "EXPLAIN QUERY PLAN SELECT * FROM t6 WHERE a<30 AND b<?;", {
        -- <25.2.1>
        0, 0, 0, "SEARCH TABLE T6 USING COVERING INDEX BB (B<?)"
        -- </25.2.1>
    })

test:do_execsql_test(
    "25.2.2",
    "EXPLAIN QUERY PLAN SELECT * FROM t6 WHERE a<20 AND b<?;", {
        -- <25.2.2>
        0, 0, 0, "SEARCH TABLE T6 USING COVERING INDEX AA (A<?)"
        -- </25.2.2>
    })

-- Term (b BETWEEN ? AND ?) is estimated at 1/64.
test:do_execsql_test(
    "25.3.1",
    [[
       EXPLAIN QUERY PLAN SELECT * FROM t6 WHERE a BETWEEN 5 AND 10 AND b BETWEEN ? AND ?;
    ]], {
        -- <25.3.1>
        0, 0, 0, "SEARCH TABLE T6 USING COVERING INDEX BB (B>? AND B<?)"
        -- </25.3.1>
    })

-- Term (b BETWEEN ? AND 60) is estimated to return roughly 15 rows -
-- 60 from (b<=60) multiplied by 0.25 for the b>=? term. Better than
-- (a<20) but not as good as (a<10).
test:do_execsql_test(
    "25.4.1",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t6 WHERE a < 10 AND (b BETWEEN ? AND 60);
    ]], {
        -- <25.4.1>
        0, 0, 0, "SEARCH TABLE T6 USING COVERING INDEX AA (A<?)"
        -- </25.4.1>
    })

test:do_execsql_test(
    "25.4.2",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t6 WHERE a < 20 AND (b BETWEEN ? AND 60);
    ]], {
        -- <25.4.2>
        0, 0, 0, "SEARCH TABLE T6 USING COVERING INDEX BB (B>? AND B<?)"
        -- </25.4.2>
    })



---------------------------------------------------------------------------
-- Check that a problem in they way stat4 data is used has been 
-- resolved (see below).
--
-- Commented due to assertion(#2834)
test:do_test(
    "26.1.1",
    function()
        test:execsql([[
            DROP TABLE IF EXISTS t1;
            CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, x, y, z);
            CREATE INDEX t1xy ON t1(x, y);
            CREATE INDEX t1z ON t1(z);
        ]])
        for i = 0, 10000 do
            test:execsql(string.format("INSERT INTO t1(id, x, y) VALUES(null, %s, %s)", i, i))
        end
        for i = 0, 10 do
            test:execsql(string.format(
                "WITH cnt(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x<100) INSERT INTO t1(id, x, y) SELECT null, %s, x FROM cnt;", i+10000))
            test:execsql(string.format("INSERT INTO t1(id, x, y) SELECT null, %s, 100;", i+10000))    
        end
        test:execsql([[
                UPDATE t1 SET z = id / 20;
                ANALYZE;
        ]]) end, {
            -- <26.1.1>
            -- </26.1.1>
        })

test:do_execsql_test(
    "26.1.2",
    [[
        SELECT count(*) FROM t1 WHERE x = 10000 AND y < 50;
    ]], {
        -- <26.1.2>
        49
        -- </26.1.2>
    })

test:do_execsql_test(
    "26.1.3",
    [[
        SELECT count(*) FROM t1 WHERE z = 444;
    ]], {
        -- <26.1.3>
        20
        -- </26.1.3>
    })

-- The analyzer knows that any (z=?) expression matches 20 rows. So it
-- will use index "t1z" if the estimate of hits for (x=10000 AND y<50)
-- is greater than 20 rows.
--
-- And it should be. The analyzer has a stat4 sample as follows:
--
--   sample=(x=10000, y=100) nLt=(10000 10099)
--
-- There should be no other samples that start with (x=10000). So it knows 
-- that (x=10000 AND y<50) must match somewhere between 0 and 99 rows, but
-- know more than that. Guessing less than 20 is therefore unreasonable.
--
-- At one point though, due to a problem in whereKeyStats(), the planner was
-- estimating that (x=10000 AND y<50) would match only 2 rows.
--
test:do_execsql_test(
    "26.1.4",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE x = 10000 AND y < 50 AND z = 444;
    ]], {
        -- <26.1.4>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1Z (Z=?)"
        -- </26.1.4>
    })

-- This test - 26.2.* - tests that another manifestation of the same problem
-- is no longer present in the library. Assuming:
-- 
--   CREATE INDEX t1xy ON t1(x, y)
--
-- and that have samples for index t1xy as follows:
--
--
--   sample=('A', 70)        nEq=(100, 2)        nLt=(900, 970)
--   sample=('B', 70)        nEq=(100, 2)        nLt=(1000, 1070)    
--
-- the planner should estimate that (x = 'B' AND y > 25) matches 76 rows
-- (70 * 2/3 + 30). Before, due to the problem, the planner was estimating 
-- that this matched 100 rows.
-- 
test:do_execsql_test(
    "26.2.1",
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, x, y, z);
        CREATE INDEX i1 ON t1(x, y);
        CREATE INDEX i2 ON t1(z);


        WITH cnt(y) AS (SELECT 0 UNION ALL SELECT y+1 FROM cnt WHERE y<99), 
            letters(x) AS (SELECT 'A' UNION SELECT 'B' UNION SELECT 'C' UNION SELECT 'D') 
                INSERT INTO t1(id, x, y) SELECT null, x, y FROM letters, cnt;

        WITH letters(x) AS (SELECT 'A' UNION SELECT 'B' UNION SELECT 'C' UNION SELECT 'D') 
            INSERT INTO t1(id, x, y) SELECT null, x, 70 FROM letters;

        WITH cnt(i) AS (SELECT 407 UNION ALL SELECT i+1 FROM cnt WHERE i<9999) 
            INSERT INTO t1(id, x, y) SELECT i, i, i FROM cnt;

        UPDATE t1 SET z = (id / 95);
        ANALYZE;
    ]])

test:do_execsql_test(
    "26.2.2",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE x='B' AND y>25 AND z=?;
    ]], {
        -- <26.2.2>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX I1 (X=? AND Y>?)"
        -- </26.2.2>
    })


test:finish_test()
