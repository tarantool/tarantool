#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(21157)
-- it is bad idea to store 20k positive test results in git
test.silent = true

--!./tcltestrunner.lua
-- 2008 June 24
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library. 
--
-- $Id: select9.test,v 1.4 2008/07/01 14:39:35 danielk1977 Exp $
-- The tests in this file are focused on test compound SELECT statements 
-- that have any or all of an ORDER BY, LIMIT or OFFSET clauses. As of
-- version 3.6.0, SQLite contains code to use SQL indexes where possible 
-- to optimize such statements.
--
-- TODO Points:
--
--   * Are there any "column affinity" issues to consider?
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
---------------------------------------------------------------------------
-- test_compound_select TESTNAME SELECT RESULT
--
--   This command is used to run multiple LIMIT/OFFSET test cases based on 
--   the single SELECT statement passed as the second argument. The SELECT
--   statement may not contain a LIMIT or OFFSET clause. This proc tests
--   many statements of the form:
--    
--     "$SELECT limit $X offset $Y"
--    
--   for various values of $X and $Y.
--    
--   The third argument, $RESULT, should contain the expected result of
--   the command [execsql $SELECT].
--    
--   The first argument, $TESTNAME, is used as the base test case name to
--   pass to [do_test] for each individual LIMIT OFFSET test case.
--

function trim(s)
    return s:match "^%s*(.-)%s*$"
end

function subrange(t, first, last)
    local sub = {}
    for i=first,last do
        sub[#sub + 1] = t[i]
    end
    return sub
end

local function test_compound_select(testname, sql, result)
    local nCol = 1
    local A = box.sql.execute(sql) --test.box(sql)
    nCol = #A[0]
    local nRow = #result / nCol
    local compound_sql = sql
    test:do_execsql_test(
        testname,
        compound_sql,
            result)
    --return
    local iLimitIncr = 1
    local iOffsetIncr = 1
    -- speedup condition
    --if X(67, "X!cmd", [=[["expr","[info exists ::G(isquick)] && $::G(isquick) && $nRow>=5"]]=])
    --then
    --    iOffsetIncr = (nRow / 5)
    --    iLimitIncr = (nRow / 5)
    --end
    local iLimitEnd = (nRow + iLimitIncr)
    local iOffsetEnd = (nRow + iOffsetIncr)
    for iOffset = 0, iOffsetEnd - 1, 1 do
        for iLimit = 0, iLimitEnd - 1, 1 do
            compound_sql = string.format("%s LIMIT %s", sql, iLimit)
            if (iOffset ~= 0) then
                compound_sql = compound_sql .. " OFFSET "..iOffset..""
            end
            local iStart = (iOffset * nCol)
            local iEnd = (((iOffset * nCol) + (iLimit * nCol)) - 1)
            test:do_execsql_test(
                string.format("%s.limit=%s.offset=%s", testname, iLimit, iOffset),
                compound_sql, subrange(result, iStart + 1, iEnd + 1))
        end
    end
end

---------------------------------------------------------------------------
-- test_compound_select_flippable TESTNAME SELECT RESULT
--
--   This command is for testing statements of the form:
--
--     <simple select 1> <compound op> <simple select 2> ORDER BY <order by>
--
--   where each <simple select> is a simple (non-compound) select statement
--   and <compound op> is one of "INTERSECT", "UNION ALL" or "UNION".
--
--   This proc calls [test_compound_select] twice, once with the select
--   statement as it is passed to this command, and once with the positions
--   of <select statement 1> and <select statement 2> exchanged.
--
local function test_compound_select_flippable(testname, sql, result)
    test_compound_select(testname, sql, result)
    local select = trim(sql)
    local RE = "(.*)(UNION ALL|INTERSECT|UNION)(.*)(ORDER BY.*)"
    local s1, op, s2, order_by = string.match(select, RE)
    --local rc = X(112, "X!cmd", [=[["regexp",["RE"],["select"],"->","s1","op","s2","order_by"]]=])
    --if (not rc)
    --then
    --    X(113, "X!cmd", [=[["error",["Statement is unflippable: ",["select"],""]]]=])
    --end
    if  s2 and op and s1 and order_by then
        local flipsql = string.format("%s %s %s %s", s2, op, s1, order_by)
        test_compound_select(testname..".flipped", flipsql, result)
    end

end

------------------------------------------------------------------------------
-- Begin tests.
--
-- Create and populate a sample database.
--
test:do_execsql_test(
    "select9-1.0",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t1(id primary key, a, b, c);
        CREATE TABLE t2(id primary key, d, e, f);
        BEGIN;
          INSERT INTO t1 VALUES(0, 1,  'one',   'I');
          INSERT INTO t1 VALUES(1, 3,  NULL,    NULL);
          INSERT INTO t1 VALUES(2, 5,  'five',  'V');
          INSERT INTO t1 VALUES(3, 7,  'seven', 'VII');
          INSERT INTO t1 VALUES(4, 9,  NULL,    NULL);
          INSERT INTO t1 VALUES(5, 2,  'two',   'II');
          INSERT INTO t1 VALUES(6, 4,  'four',  'IV');
          INSERT INTO t1 VALUES(7, 6,  NULL,    NULL);
          INSERT INTO t1 VALUES(8, 8,  'eight', 'VIII');
          INSERT INTO t1 VALUES(9, 10, 'ten',   'X');

          INSERT INTO t2 VALUES(0, 1,  'two',      'IV');
          INSERT INTO t2 VALUES(1, 2,  'four',     'VIII');
          INSERT INTO t2 VALUES(2, 3,  NULL,       NULL);
          INSERT INTO t2 VALUES(3, 4,  'eight',    'XVI');
          INSERT INTO t2 VALUES(4, 5,  'ten',      'XX');
          INSERT INTO t2 VALUES(5, 6,  NULL,       NULL);
          INSERT INTO t2 VALUES(6, 7,  'fourteen', 'XXVIII');
          INSERT INTO t2 VALUES(7, 8,  'sixteen',  'XXXII');
          INSERT INTO t2 VALUES(8, 9,  NULL,       NULL);
          INSERT INTO t2 VALUES(9, 10, 'twenty',   'XL');

        COMMIT;
    ]], {
        -- <select9-1.0>
        
        -- </select9-1.0>
    })

-- Each iteration of this loop runs the same tests with a different set
-- of indexes present within the database schema. The data returned by
-- the compound SELECT statements in the test cases should be the same 
-- in each case.
--
local iOuterLoop = 1
for _, indexes in ipairs({ [[
      /* Do not create any indexes. */
    ]], [[
      CREATE INDEX i1 ON t1(a)
    ]], [[
      CREATE INDEX i2 ON t1(b)
    ]], [[
      CREATE INDEX i3 ON t2(d)
    ]], [[
      CREATE INDEX i4 ON t2(e)
    ]] }) do
    test:do_execsql_test(
        "select9-1."..iOuterLoop..".1",
        indexes, {
        })

    -- Test some 2-way UNION ALL queries. No WHERE clauses.
    --
    test_compound_select("select9-1."..iOuterLoop..".2", [[
    SELECT a, b FROM t1 UNION ALL SELECT d, e FROM t2
  ]], { 1.0, "one", 3.0, "", 5.0, "five", 7.0, "seven", 9.0, "", 2.0, "two", 4.0, "four", 6.0, "", 8.0, "eight", 10.0, "ten", 1.0, "two", 2.0, "four", 3.0, "", 4.0, "eight", 5.0, "ten", 6.0, "", 7.0, "fourteen", 8.0, "sixteen", 9.0, "", 10.0, "twenty"})
    test_compound_select("select9-1."..iOuterLoop..".3", [[
    SELECT a, b FROM t1 UNION ALL SELECT d, e FROM t2 ORDER BY 1
  ]], { 1.0, "one", 1.0, "two", 2.0, "two", 2.0, "four", 3.0, "", 3.0, "", 4.0, "four", 4.0, "eight", 5.0, "five", 5.0, "ten", 6.0, "", 6.0, "", 7.0, "seven", 7.0, "fourteen", 8.0, "eight", 8.0, "sixteen", 9.0, "", 9.0, "", 10.0, "ten", 10.0, "twenty"})
    test_compound_select("select9-1."..iOuterLoop..".4", [[
    SELECT a, b FROM t1 UNION ALL SELECT d, e FROM t2 ORDER BY 2
  ]], { 3.0, "", 9.0, "", 6.0, "", 3.0, "", 6.0, "", 9.0, "", 8.0, "eight", 4.0, "eight", 5.0, "five", 4.0, "four", 2.0, "four", 7.0, "fourteen", 1.0, "one", 7.0, "seven", 8.0, "sixteen", 10.0, "ten", 5.0, "ten", 10.0, "twenty", 2.0, "two", 1.0, "two"})
   -- test_compound_select_flippable("select9-1."..iOuterLoop..".5", [[
   -- SELECT a, b FROM t1 UNION ALL SELECT d, e FROM t2 ORDER BY 1, 2
  --]], "1 one 1 two 2 four 2 two 3 {} 3 {} 4 eight 4 four 5 five 5 ten 6 {} 6 {} 7 fourteen 7 seven 8 eight 8 sixteen 9 {} 9 {} 10 ten 10 twenty")
    test_compound_select_flippable("select9-1."..iOuterLoop..".6", [[
    SELECT a, b FROM t1 UNION ALL SELECT d, e FROM t2 ORDER BY 2, 1
  ]], { 3.0, "", 3.0, "", 6.0, "", 6.0, "", 9.0, "", 9.0, "", 4.0, "eight", 8.0, "eight", 5.0, "five", 2.0, "four", 4.0, "four", 7.0, "fourteen", 1.0, "one", 7.0, "seven", 8.0, "sixteen", 5.0, "ten", 10.0, "ten", 10.0, "twenty", 1.0, "two", 2.0, "two"})
    -- Test some 2-way UNION queries.
    --
    test_compound_select("select9-1."..iOuterLoop..".7", [[
    SELECT a, b FROM t1 UNION SELECT d, e FROM t2
  ]], { 1.0, "one", 1.0, "two", 2.0, "four", 2.0, "two", 3.0, "", 4.0, "eight", 4.0, "four", 5.0, "five", 5.0, "ten", 6.0, "", 7.0, "fourteen", 7.0, "seven", 8.0, "eight", 8.0, "sixteen", 9.0, "", 10.0, "ten", 10.0, "twenty"})
    test_compound_select("select9-1."..iOuterLoop..".8", [[
    SELECT a, b FROM t1 UNION SELECT d, e FROM t2 ORDER BY 1
  ]], { 1.0, "one", 1.0, "two", 2.0, "four", 2.0, "two", 3.0, "", 4.0, "eight", 4.0, "four", 5.0, "five", 5.0, "ten", 6.0, "", 7.0, "fourteen", 7.0, "seven", 8.0, "eight", 8.0, "sixteen", 9.0, "", 10.0, "ten", 10.0, "twenty"})
    test_compound_select("select9-1."..iOuterLoop..".9", [[
    SELECT a, b FROM t1 UNION SELECT d, e FROM t2 ORDER BY 2
  ]], { 3.0, "", 6.0, "", 9.0, "", 4.0, "eight", 8.0, "eight", 5.0, "five", 2.0, "four", 4.0, "four", 7.0, "fourteen", 1.0, "one", 7.0, "seven", 8.0, "sixteen", 5.0, "ten", 10.0, "ten", 10.0, "twenty", 1.0, "two", 2.0, "two"})
    test_compound_select_flippable("select9-1."..iOuterLoop..".10", [[
    SELECT a, b FROM t1 UNION SELECT d, e FROM t2 ORDER BY 1, 2
  ]], { 1.0, "one", 1.0, "two", 2.0, "four", 2.0, "two", 3.0, "", 4.0, "eight", 4.0, "four", 5.0, "five", 5.0, "ten", 6.0, "", 7.0, "fourteen", 7.0, "seven", 8.0, "eight", 8.0, "sixteen", 9.0, "", 10.0, "ten", 10.0, "twenty"})
    test_compound_select_flippable("select9-1."..iOuterLoop..".11", [[
    SELECT a, b FROM t1 UNION SELECT d, e FROM t2 ORDER BY 2, 1
  ]], { 3.0, "", 6.0, "", 9.0, "", 4.0, "eight", 8.0, "eight", 5.0, "five", 2.0, "four", 4.0, "four", 7.0, "fourteen", 1.0, "one", 7.0, "seven", 8.0, "sixteen", 5.0, "ten", 10.0, "ten", 10.0, "twenty", 1.0, "two", 2.0, "two"})
    -- Test some 2-way INTERSECT queries.
    --
    test_compound_select("select9-1."..iOuterLoop..".11", [[
    SELECT a, b FROM t1 INTERSECT SELECT d, e FROM t2
  ]], { 3.0, "", 6.0, "", 9.0, ""})
    test_compound_select_flippable("select9-1."..iOuterLoop..".12", [[
    SELECT a, b FROM t1 INTERSECT SELECT d, e FROM t2 ORDER BY 1
  ]], { 3.0, "", 6.0, "", 9.0, ""})
    test_compound_select("select9-1."..iOuterLoop..".13", [[
    SELECT a, b FROM t1 INTERSECT SELECT d, e FROM t2 ORDER BY 2
  ]], { 3.0, "", 6.0, "", 9.0, ""})
    test_compound_select_flippable("select9-1."..iOuterLoop..".14", [[
    SELECT a, b FROM t1 INTERSECT SELECT d, e FROM t2 ORDER BY 2, 1
  ]], { 3.0, "", 6.0, "", 9.0, ""})
    test_compound_select_flippable("select9-1."..iOuterLoop..".15", [[
    SELECT a, b FROM t1 INTERSECT SELECT d, e FROM t2 ORDER BY 1, 2
  ]], { 3.0, "", 6.0, "", 9.0, ""})
    -- Test some 2-way EXCEPT queries.
    --
    test_compound_select("select9-1."..iOuterLoop..".16", [[
    SELECT a, b FROM t1 EXCEPT SELECT d, e FROM t2
  ]], { 1.0, "one", 2.0, "two", 4.0, "four", 5.0, "five", 7.0, "seven", 8.0, "eight", 10.0, "ten"})
    test_compound_select("select9-1."..iOuterLoop..".17", [[
    SELECT a, b FROM t1 EXCEPT SELECT d, e FROM t2 ORDER BY 1
  ]], { 1.0, "one", 2.0, "two", 4.0, "four", 5.0, "five", 7.0, "seven", 8.0, "eight", 10.0, "ten"})
    test_compound_select("select9-1."..iOuterLoop..".18", [[
    SELECT a, b FROM t1 EXCEPT SELECT d, e FROM t2 ORDER BY 2
  ]], { 8.0, "eight", 5.0, "five", 4.0, "four", 1.0, "one", 7.0, "seven", 10.0, "ten", 2.0, "two"})
    test_compound_select("select9-1."..iOuterLoop..".19", [[
    SELECT a, b FROM t1 EXCEPT SELECT d, e FROM t2 ORDER BY 1, 2
  ]], { 1.0, "one", 2.0, "two", 4.0, "four", 5.0, "five", 7.0, "seven", 8.0, "eight", 10.0, "ten"})
    test_compound_select("select9-1."..iOuterLoop..".20", [[
    SELECT a, b FROM t1 EXCEPT SELECT d, e FROM t2 ORDER BY 2, 1
  ]], { 8.0, "eight", 5.0, "five", 4.0, "four", 1.0, "one", 7.0, "seven", 10.0, "ten", 2.0, "two"})
    iOuterLoop = iOuterLoop + 1
end
test:do_execsql_test(
    "select9-2.0",
    [[
        DROP INDEX i1;
        DROP INDEX i2;
        DROP INDEX i3;
        DROP INDEX i4;
    ]], {
        -- <select9-2.0>
        
        -- </select9-2.0>
    })

local t1_space_id = ""
local t2_space_id = ""
t1_space_id = test:execsql("SELECT * from _space where name='t1'")["id"]
t2_space_id = test:execsql("SELECT * from _space where name='t2'")["id"]
--X(276, "X!cmd", [=[["db","eval","SELECT * from _space where name='t2'","data","\n  set t2_space_id $data(id)\n"]]=])
--local function reverse(lhs, rhs)
--    return X(283, "X!cmd", [=[["string","compare",["rhs"],["lhs"]]]=])
--end

--db("collate", "reverse", "reverse")
-- This loop is similar to the previous one (test cases select9-1.*) 
-- except that the simple select statements have WHERE clauses attached
-- to them. Sometimes the WHERE clause may be satisfied using the same
-- index used for ORDER BY, sometimes not.
--
local recreate_i1 = "DROP INDEX i1; CREATE INDEX i1 ON t1(b, a)"
iOuterLoop = 1
for _, indexes in ipairs({ [[
  /* Do not create any indexes. */
]], [[
  CREATE INDEX i1 ON t1(a)
]], [[
  DROP INDEX 'i1';
  CREATE INDEX i1 ON t1(b,a);
]],
    -- TODO collation is not supported by now
 --   [[
 -- CREATE INDEX i2 ON t2(d DESC, e COLLATE REVERSE ASC);
--]],
    [[
  CREATE INDEX i3 ON t1(a DESC);
]] }) do
    test:do_execsql_test(
"select9-2."..iOuterLoop..".1",
indexes, {
            
        })

    test_compound_select_flippable("select9-2."..iOuterLoop..".2", [[
    SELECT a,b,c FROM t1 WHERE a<5 UNION SELECT d,e,f FROM t2 WHERE d>=5 ORDER BY 1
  ]], { 1.0, "one", "I", 2.0, "two", "II", 3.0, "", "", 4.0, "four", "IV", 5.0, "ten", "XX", 6.0, "", "", 7.0, "fourteen", "XXVIII", 8.0, "sixteen", "XXXII", 9.0, "", "", 10.0, "twenty", "XL"})
    test_compound_select_flippable("select9-2."..iOuterLoop..".2", [[
    SELECT a,b,c FROM t1 WHERE a<5 UNION SELECT d,e,f FROM t2 WHERE d>=5 ORDER BY 2, 1
  ]], { 3.0, "", "", 6.0, "", "", 9.0, "", "", 4.0, "four", "IV", 7.0, "fourteen", "XXVIII", 1.0, "one", "I", 8.0, "sixteen", "XXXII", 5.0, "ten", "XX", 10.0, "twenty", "XL", 2.0, "two", "II"})
    -- TODO collation is not supported by now
    --test_compound_select_flippable("select9-2."..iOuterLoop..".3", [[
    --SELECT a,b,c FROM t1 WHERE a<5 UNION SELECT d,e,f FROM t2 WHERE d>=5
    --ORDER BY 2 COLLATE reverse, 1
  --]], { 3.0, "", "", 6.0, "", "", 9.0, "", "", 2.0, "two", "II", 10.0, "twenty", "XL", 5.0, "ten", "XX", 8.0, "sixteen", "XXXII", 1.0, "one", "I", 7.0, "fourteen", "XXVIII", 4.0, "four", "IV"})
    test_compound_select_flippable("select9-2."..iOuterLoop..".4", [[
    SELECT a,b,c FROM t1 WHERE a<5 UNION ALL SELECT d,e,f FROM t2 WHERE d>=5 ORDER BY 1
  ]], { 1.0, "one", "I", 2.0, "two", "II", 3.0, "", "", 4.0, "four", "IV", 5.0, "ten", "XX", 6.0, "", "", 7.0, "fourteen", "XXVIII", 8.0, "sixteen", "XXXII", 9.0, "", "", 10.0, "twenty", "XL"})
    test_compound_select_flippable("select9-2."..iOuterLoop..".5", [[
    SELECT a,b,c FROM t1 WHERE a<5 UNION ALL SELECT d,e,f FROM t2 WHERE d>=5 ORDER BY 2, 1
  ]], { 3.0, "", "", 6.0, "", "", 9.0, "", "", 4.0, "four", "IV", 7.0, "fourteen", "XXVIII", 1.0, "one", "I", 8.0, "sixteen", "XXXII", 5.0, "ten", "XX", 10.0, "twenty", "XL", 2.0, "two", "II"})

    -- TODO collation is not supported by now
    --test_compound_select_flippable("select9-2."..iOuterLoop..".6", [[
    --SELECT a,b,c FROM t1 WHERE a<5 UNION ALL SELECT d,e,f FROM t2 WHERE d>=5
    --ORDER BY 2 COLLATE reverse, 1
  --]], { 3.0, "", "", 6.0, "", "", 9.0, "", "", 2.0, "two", "II", 10.0, "twenty", "XL", 5.0, "ten", "XX", 8.0, "sixteen", "XXXII", 1.0, "one", "I", 7.0, "fourteen", "XXVIII", 4.0, "four", "IV"})
    test_compound_select("select9-2."..iOuterLoop..".4", [[
    SELECT a FROM t1 WHERE a<8 EXCEPT SELECT d FROM t2 WHERE d<=3 ORDER BY 1
  ]], { 4.0, 5.0, 6.0, 7.0})
    test_compound_select("select9-2."..iOuterLoop..".4", [[
    SELECT a FROM t1 WHERE a<8 INTERSECT SELECT d FROM t2 WHERE d<=3 ORDER BY 1
  ]], { 1.0, 2.0, 3.0})
end
test:do_execsql_test(
    "select9-2.X",
    [[
        DROP INDEX i1;
        /* TODO collation is not supported by nowDROP INDEX i2;*/
        DROP INDEX i3;
    ]], {
        -- <select9-2.X>
        
        -- </select9-2.X>
    })

-- This procedure executes the SQL.  Then it checks the generated program
-- for the SQL and appends a "nosort" to the result if the program contains the
-- SortCallback opcode.  If the program does not contain the SortCallback
-- opcode it appends "sort"
--
-- TODO: need access to sqlite_sort_count vatiable
--local function cksort(sql)
--    sqlite_sort_count = 0
--    data = test:execsql(sql)
--    if sqlite_sort_count then
--        x = "sort"
--    end
--    table.insert(data,x) or data
--    return data
--end

-- If the right indexes exist, the following query:
--
--     SELECT t1.a FROM t1 UNION ALL SELECT t2.d FROM t2 ORDER BY 1
--
-- can use indexes to run without doing a in-memory sort operation.
-- This block of tests (select9-3.*) is used to check if the same 
-- is possible with:
--
--     CREATE VIEW v1 AS SELECT a FROM t1 UNION ALL SELECT d FROM t2
--     SELECT a FROM v1 ORDER BY 1
--
-- It turns out that it is.
--
-- TODO: need access to sqlite_sort_count vatiable
--test:do_test(
--    "select9-3.1",
--    function()
--        return cksort(" SELECT a FROM t1 ORDER BY 1 ")
--    end, {
--        -- <select9-3.1>
--        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, "sort"
--        -- </select9-3.1>
--    })

-- MUST_WORK_TEST
-- do_test select9-3.2 {
--   execsql { CREATE INDEX i1 ON t1(a) }
--   cksort { SELECT a FROM t1 ORDER BY 1 }
-- } {1 2 3 4 5 6 7 8 9 10 nosort}
-- TODO: need access to sqlite_sort_count vatiable
--test:do_test(
--    "select9-3.3",
--    function()
--        return cksort(" SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY 1 LIMIT 5 ")
--    end, {
--        -- <select9-3.3>
--        1, 1, 2, 2, 3, "sort"
--        -- </select9-3.3>
--    })

-- MUST_WORK_TEST
-- do_test select9-3.4 {
--   execsql { CREATE INDEX i2 ON t2(d) }
--   cksort { SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY 1 LIMIT 5 }
-- } {1 1 2 2 3 nosort}
-- do_test select9-3.5 {
--   execsql { CREATE VIEW v1 AS SELECT a FROM t1 UNION ALL SELECT d FROM t2 }
--   cksort { SELECT a FROM v1 ORDER BY 1 LIMIT 5 }
-- } {1 1 2 2 3 nosort}
-- do_test select9-3.X {
--   execsql {
--     DROP INDEX i1;
--     DROP INDEX i2;
--     DROP VIEW v1;
--   }
-- } {}
-- This block of tests is the same as the preceding one, except that
-- "UNION" is tested instead of "UNION ALL".
--
-- TODO: need access to sqlite_sort_count vatiable
--test:do_test(
--    "select9-4.1",
--    function()
--        return cksort(" SELECT a FROM t1 ORDER BY 1 ")
--    end, {
--        -- <select9-4.1>
--        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, "sort"
--        -- </select9-4.1>
--    })

-- MUST_WORK_TEST
-- do_test select9-4.2 {
--   execsql { CREATE INDEX i1 ON t1(a) }
--   cksort { SELECT a FROM t1 ORDER BY 1 }
-- } {1 2 3 4 5 6 7 8 9 10 nosort}
-- TODO: need access to sqlite_sort_count vatiable
--test:do_test(
--    "select9-4.3",
--    function()
--        return cksort(" SELECT a FROM t1 UNION SELECT d FROM t2 ORDER BY 1 LIMIT 5 ")
--    end, {
--        -- <select9-4.3>
--        1, 2, 3, 4, 5, "sort"
--        -- </select9-4.3>
--    })

-- MUST_WORK_TEST
-- do_test select9-4.4 {
--   execsql { CREATE INDEX i2 ON t2(d) }
--   cksort { SELECT a FROM t1 UNION SELECT d FROM t2 ORDER BY 1 LIMIT 5 }
-- } {1 2 3 4 5 nosort}
-- do_test select9-4.5 {
--   execsql { CREATE VIEW v1 AS SELECT a FROM t1 UNION SELECT d FROM t2 }
--   cksort { SELECT a FROM v1 ORDER BY 1 LIMIT 5 }
-- } {1 2 3 4 5 sort}
-- do_test select9-4.X {
--   execsql {
--     DROP INDEX i1;
--     DROP INDEX i2;
--     DROP VIEW v1;
--   }
-- } {}
-- Testing to make sure that queries involving a view of a compound select
-- are planned efficiently.  This detects a problem reported on the mailing
-- list on 2012-04-26.  See
--
--  http://www.mail-archive.com/sqlite-users%40sqlite.org/msg69746.html
--
-- For additional information.
--
local json = require('json')
test:do_test(
    "select9-5.1",
    function()
        return test:execsql [[
            CREATE TABLE t51(x primary key, y);
            CREATE TABLE t52(x primary key, y);
            CREATE VIEW v5 as
               SELECT x, y FROM t51
               UNION ALL
               SELECT x, y FROM t52;
            EXPLAIN QUERY PLAN
               SELECT * FROM v5 WHERE x='12345' ORDER BY y;
        ]]
    end, {
        -- <select9-5.1>
        "~/SCAN TABLE/"
        -- </select9-5.1>
    })

-- Uses indices with "*"
test:do_test(
    "select9-5.2",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
               SELECT x, y FROM v5 WHERE x='12345' ORDER BY y;
        ]]
    end, {
        -- <select9-5.2>
        "~/SCAN TABLE/"
        -- </select9-5.2>
    })

-- Uses indices with "x, y"
test:do_test(
    "select9-5.3",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
               SELECT x, y FROM v5 WHERE +x='12345' ORDER BY y;
        ]]
    end, {
        -- <select9-5.3>
        "/SCAN TABLE/"
        -- </select9-5.3>
    })

-- Full table scan if the "+x" prevents index usage.
-- 2013-07-09:  Ticket [490a4b7235624298]: 
-- "WHERE 0" on the first element of a UNION causes an assertion fault
--
test:do_execsql_test(
    "select9-6.1",
    [[
        DROP TABLE IF EXISTS t61;
        DROP TABLE IF EXISTS t62;
        CREATE TABLE t61(a primary key);
        CREATE TABLE t62(b primary key);
        INSERT INTO t61 VALUES(111);
        INSERT INTO t62 VALUES(222);
        SELECT a FROM t61 WHERE 0 UNION SELECT b FROM t62;
    ]], {
        -- <select9-6.1>
        222
        -- </select9-6.1>
    })

test:do_execsql_test(
    "select9-6.2",
    [[
        SELECT a FROM t61 WHERE 0 UNION ALL SELECT b FROM t62;
    ]], {
        -- <select9-6.2>
        222
        -- </select9-6.2>
    })

test:do_execsql_test(
    "select9-6.3",
    [[
        SELECT a FROM t61 UNION SELECT b FROM t62 WHERE 0;
    ]], {
        -- <select9-6.3>
        111
        -- </select9-6.3>
    })

test:finish_test()

