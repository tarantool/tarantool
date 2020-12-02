#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(509)

--!./tcltestrunner.lua
-- 2010 July 16
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
-- This file implements tests to verify that the "testable statements" in
-- the lang_select.html document are correct.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_execsql_test(
    "e_select-1.0",
    [[
        CREATE TABLE t1(a TEXT PRIMARY KEY, b TEXT);
        INSERT INTO t1 VALUES('a', 'one');
        INSERT INTO t1 VALUES('b', 'two');
        INSERT INTO t1 VALUES('c', 'three');

        CREATE TABLE t2(a TEXT PRIMARY KEY, b TEXT);
        INSERT INTO t2 VALUES('a', 'I');
        INSERT INTO t2 VALUES('b', 'II');
        INSERT INTO t2 VALUES('c', 'III');

        CREATE TABLE t3(a TEXT PRIMARY KEY, c INT);
        INSERT INTO t3 VALUES('a', 1);
        INSERT INTO t3 VALUES('b', 2);

        CREATE TABLE t4(a TEXT PRIMARY KEY, c INT);
        INSERT INTO t4 VALUES('a', NULL);
        INSERT INTO t4 VALUES('b', 2);
    ]], {
        -- <e_select-1.0>

        -- </e_select-1.0>
    })

local t1_cross_t2 = { "a", "one", "a", "I", "a", "one", "b", "II", "a", "one", "c", "III", "b", "two", "a", "I", "b", "two", "b", "II", "b", "two", "c", "III", "c", "three", "a", "I", "c", "three", "b", "II", "c", "three", "c", "III" }
local t1_cross_t1 = { "a", "one", "a", "one", "a", "one", "b", "two", "a", "one", "c", "three", "b", "two", "a", "one", "b", "two", "b", "two", "b", "two", "c", "three", "c", "three", "a", "one", "c", "three", "b", "two", "c", "three", "c", "three" }
-- This proc is a specialized version of [do_execsql_test].
-- The second argument to this proc must be a SELECT statement that
-- features a cross join of some time. Instead of the usual ",",
-- "CROSS JOIN" or "INNER JOIN" join-op, the string JOIN_PATTERN must be
-- substituted.
--
-- This test runs the SELECT three times - once with:
--
--   * s/JOIN_PATTERN/,/
--   * s/JOIN_PATTERN/JOIN/
--   * s/JOIN_PATTERN/INNER JOIN/
--   * s/JOIN_PATTERN/CROSS JOIN/
--
-- and checks that each time the results of the SELECT are $res.
--
local function do_join_test(tn, select, res)
    for tn2, joinop in ipairs({",", "CROSS JOIN", "INNER JOIN"}) do
        local S = string.gsub(select, "JOIN_PATTERN", joinop)
        test:do_execsql_test(
            tn.."."..tn2,
            S,
            res)
    end
end

---------------------------------------------------------------------------
-- The following tests check that all paths on the syntax diagrams on
-- the lang_select.html page may be taken.
--
-- -- syntax diagram join-constraint
--
do_join_test("e_select-0.1.1", [[
  SELECT count(*) FROM t1 JOIN_PATTERN t2 ON (t1.a=t2.a)
]], {3})
do_join_test("e_select-0.1.2", [[
  SELECT count(*) FROM t1 JOIN_PATTERN t2 USING (a)
]], {3})
do_join_test("e_select-0.1.3", [[
  SELECT count(*) FROM t1 JOIN_PATTERN t2
]], {9})
test:do_catchsql_test(
    "e_select-0.1.4",
    [[
        SELECT count(*) FROM t1, t2 ON (t1.a=t2.a) USING (a)
    ]], {
        -- <e_select-0.1.4>
        1, "cannot have both ON and USING clauses in the same join"
        -- </e_select-0.1.4>
    })

test:do_catchsql_test(
    "e_select-0.1.5",
    [[
        SELECT count(*) FROM t1, t2 USING (a) ON (t1.a=t2.a)
    ]], {
        -- <e_select-0.1.5>
        1, [[At line 1 at or near position 47: keyword 'ON' is reserved. Please use double quotes if 'ON' is an identifier.]]
        -- </e_select-0.1.5>
    })

-- -- syntax diagram select-core
--
--   0: SELECT ...
--   1: SELECT DISTINCT ...
--   2: SELECT ALL ...
--
--   0: No FROM clause
--   1: Has FROM clause
--
--   0: No WHERE clause
--   1: Has WHERE clause
--
--   0: No GROUP BY clause
--   1: Has GROUP BY clause
--   2: Has GROUP BY and HAVING clauses
--
test:do_select_tests(
    "e_select-0.2",
    {
        {"0000.1", "SELECT 1, 2, 3 ", {1, 2, 3}},
        {"1000.1", "SELECT DISTINCT 1, 2, 3 ", {1, 2, 3}},
        {"2000.1", "SELECT ALL 1, 2, 3 ", {1, 2, 3}},

        {"0100.1", "SELECT a, b, a||b FROM t1 ", {"a", "one", "aone", "b", "two", "btwo", "c", "three", "cthree"}},
        {"1100.1", "SELECT DISTINCT a, b, a||b FROM t1 ", {"a", "one", "aone", "b", "two", "btwo", "c", "three", "cthree"}},
        {"1200.1", "SELECT ALL a, b, a||b FROM t1 ", {"a", "one", "aone", "b", "two", "btwo", "c", "three", "cthree"}},

        {"0010.1", "SELECT 1, 2, 3 WHERE true ", {1, 2, 3}},
        {"0010.2", "SELECT 1, 2, 3 WHERE false ", {}},
        {"0010.3", "SELECT 1, 2, 3 WHERE NULL ", {}},

        {"1010.1", "SELECT DISTINCT 1, 2, 3 WHERE true ", {1, 2, 3}},

        {"2010.1", "SELECT ALL 1, 2, 3 WHERE true ", {1, 2, 3}},

        {"0110.1", "SELECT a, b, a||b FROM t1 WHERE a!='x' ", {"a", "one", "aone", "b", "two", "btwo", "c", "three", "cthree"}},
        {"0110.2", "SELECT a, b, a||b FROM t1 WHERE a=='x'", {}},

        {"1110.1", "SELECT DISTINCT a, b, a||b FROM t1 WHERE a!='x' ", {"a", "one", "aone", "b", "two", "btwo", "c", "three", "cthree"}},

        {"2110.0", "SELECT ALL a, b, a||b FROM t1 WHERE a=='x'", {}},

        {"0001.1", "SELECT 1, 2, 3 GROUP BY 2", {1, 2, 3}},
        {"0002.1", "SELECT 1, 2, 3 GROUP BY 2 HAVING count(*)=1", {1, 2, 3}},
        {"0002.2", "SELECT 1, 2, 3 GROUP BY 2 HAVING count(*)>1", {}},

        {"1001.1", "SELECT DISTINCT 1, 2, 3 GROUP BY 2", {1, 2, 3}},
        {"1002.1", "SELECT DISTINCT 1, 2, 3 GROUP BY 2 HAVING count(*)=1", {1, 2, 3}},
        {"1002.2", "SELECT DISTINCT 1, 2, 3 GROUP BY 2 HAVING count(*)>1", {}},

        {"2001.1", "SELECT ALL 1, 2, 3 GROUP BY 2", {1, 2, 3}},
        {"2002.1", "SELECT ALL 1, 2, 3 GROUP BY 2 HAVING count(*)=1", {1, 2, 3}},
        {"2002.2", "SELECT ALL 1, 2, 3 GROUP BY 2 HAVING count(*)>1", {}},

        {"0101.1", "SELECT count(*), max(a) FROM t1 GROUP BY b", {1, "a", 1, "c", 1, "b"}},
        {"0102.1", "SELECT count(*), max(a) FROM t1 GROUP BY b HAVING count(*)=1", {1, "a", 1, "c", 1, "b"}},
        {"0102.2", "SELECT count(*), max(a) FROM t1 GROUP BY b HAVING count(*)=2", { }},

        {"1101.1", "SELECT DISTINCT count(*), max(a) FROM t1 GROUP BY b", {1, "a", 1, "c", 1, "b"}},
        {"1102.1", "SELECT DISTINCT count(*), max(a) FROM t1 GROUP BY b HAVING count(*)=1", {1, "a", 1, "c", 1, "b"}},
        {"1102.2", "SELECT DISTINCT count(*), max(a) FROM t1 GROUP BY b HAVING count(*)=2", { }},

        {"2101.1", "SELECT ALL count(*), max(a) FROM t1 GROUP BY b", {1, "a", 1, "c", 1, "b"}},
        {"2102.1", "SELECT ALL count(*), max(a) FROM t1 GROUP BY b HAVING count(*)=1", {1, "a", 1, "c", 1, "b"}},
        {"2102.2", "SELECT ALL count(*), max(a) FROM t1 GROUP BY b HAVING count(*)=2", {}},

        {"0011.1", "SELECT 1, 2, 3 WHERE true GROUP BY 2", {1, 2, 3}},
        {"0012.1", "SELECT 1, 2, 3 WHERE false GROUP BY 2 HAVING count(*)=1", {}},
        {"0012.2", "SELECT 1, 2, 3 WHERE false GROUP BY 2 HAVING count(*)>1", {}},

        {"1011.1", "SELECT DISTINCT 1, 2, 3 WHERE false GROUP BY 2", {}},
        {"1012.1", "SELECT DISTINCT 1, 2, 3 WHERE true GROUP BY 2 HAVING count(*)=1", {1, 2, 3}},
        {"1012.2", "SELECT DISTINCT 1, 2, 3 WHERE NULL GROUP BY 2 HAVING count(*)>1", {}},

        {"2011.1", "SELECT ALL 1, 2, 3 WHERE true GROUP BY 2", {1, 2, 3}},
        {"2012.1", "SELECT ALL 1, 2, 3 WHERE false GROUP BY 2 HAVING count(*)=1", {}},
        {"2012.2", "SELECT ALL 1, 2, 3 WHERE true GROUP BY 2 HAVING count(*)>1", {}},

        {"0111.1", "SELECT count(*), max(a) FROM t1 WHERE a='a' GROUP BY b", {1, "a"}},
        {"0112.1", "SELECT count(*), max(a) FROM t1 WHERE a='c' GROUP BY b HAVING count(*)=1", {1, "c"}},
        {"0112.2", "SELECT count(*), max(a) FROM t1 WHERE false GROUP BY b HAVING count(*)=2", { }},
        {"1111.1", "SELECT DISTINCT count(*), max(a) FROM t1 WHERE a<'c' GROUP BY b", {1, "a", 1, "b"}},
        {"1112.1", "SELECT DISTINCT count(*), max(a) FROM t1 WHERE a>'a' GROUP BY b HAVING count(*)=1", {1, "c", 1, "b"}},
        {"1112.2", "SELECT DISTINCT count(*), max(a) FROM t1 WHERE false GROUP BY b HAVING count(*)=2", { }},

        {"2111.1", "SELECT ALL count(*), max(a) FROM t1 WHERE b>'one' GROUP BY b", {1, "c", 1, "b"}},
        {"2112.1", "SELECT ALL count(*), max(a) FROM t1 WHERE a!='b' GROUP BY b HAVING count(*)=1", {1, "a", 1, "c"}},
        {"2112.2", "SELECT ALL count(*), max(a) FROM t1 WHERE false GROUP BY b HAVING count(*)=2", { }},
    })

-- -- syntax diagram result-column
--
test:do_select_tests(
    "e_select-0.3",
    {
        {"1", "SELECT * FROM t1", {"a", "one", "b", "two", "c", "three"}},
        {"2", "SELECT t1.* FROM t1", {"a", "one", "b", "two", "c", "three"}},
        {"3", "SELECT 'x'||a||'x' FROM t1", {"xax", "xbx", "xcx"}},
        {"4", "SELECT 'x'||a||'x' alias FROM t1", {"xax", "xbx", "xcx"}},
        {"5", "SELECT 'x'||a||'x' AS alias FROM t1", {"xax", "xbx", "xcx"}},
    })

-- # -- syntax diagram join-source
-- #
-- # -- syntax diagram join-op
-- #
-- do_select_tests e_select-0.4 {
--   1  "SELECT t1.rowid FROM t1" {1, 2, 3}
--   2  "SELECT t1.rowid FROM t1,t2" {1, 1, 1, 2, 2, 2, 3, 3, 3}
--   3  "SELECT t1.rowid FROM t1,t2,t3" {1 1 1 1 1 1 2 2 2 2, 2, 2, 3, 3, 3, 3, 3, 3}
--   4  "SELECT t1.rowid FROM t1" {1, 2, 3}
--   5  "SELECT t1.rowid FROM t1 JOIN t2" {1, 1, 1, 2, 2, 2, 3, 3, 3}
--   6  "SELECT t1.rowid FROM t1 JOIN t2 JOIN t3"--      {1 1 1 1 1 1 2 2 2 2, 2, 2, 3, 3, 3, 3, 3, 3}
--   7  "SELECT t1.rowid FROM t1 NATURAL JOIN t3" {1, 2}
--   8  "SELECT t1.rowid FROM t1 NATURAL LEFT OUTER JOIN t3" {1, 2, 3}
--   9  "SELECT t1.rowid FROM t1 NATURAL LEFT JOIN t3" {1, 2, 3}
--   10 "SELECT t1.rowid FROM t1 NATURAL INNER JOIN t3" {1, 2}
--   11 "SELECT t1.rowid FROM t1 NATURAL CROSS JOIN t3" {1, 2}
--   12 "SELECT t1.rowid FROM t1 JOIN t3" {1, 1, 2, 2, 3, 3}
--   13 "SELECT t1.rowid FROM t1 LEFT OUTER JOIN t3" {1, 1, 2, 2, 3, 3}
--   14 "SELECT t1.rowid FROM t1 LEFT JOIN t3" {1, 1, 2, 2, 3, 3}
--   15 "SELECT t1.rowid FROM t1 INNER JOIN t3" {1, 1, 2, 2, 3, 3}
--   16 "SELECT t1.rowid FROM t1 CROSS JOIN t3" {1, 1, 2, 2, 3, 3}
-- }
-- # -- syntax diagram compound-operator
-- #
-- do_select_tests e_select-0.5 {
--   1  "SELECT rowid FROM t1 UNION ALL SELECT rowid+2 FROM t4" {1, 2, 3, 3, 4}
--   2  "SELECT rowid FROM t1 UNION     SELECT rowid+2 FROM t4" {1, 2, 3, 4}
--   3  "SELECT rowid FROM t1 INTERSECT SELECT rowid+2 FROM t4" {3}
--   4  "SELECT rowid FROM t1 EXCEPT    SELECT rowid+2 FROM t4" {1, 2}
-- }
-- -- syntax diagram ordering-term
--
test:do_select_tests(
    "e_select-0.6",
    {
        {"1", "SELECT b||a FROM t1 ORDER BY b||a", {"onea", "threec", "twob"}},
        {"2", "SELECT b||a FROM t1 ORDER BY (b||a) COLLATE \"unicode_ci\"", {"onea", "threec", "twob"}},
        {"3", "SELECT b||a FROM t1 ORDER BY (b||a) ASC", {"onea", "threec", "twob"}},
        {"4", "SELECT b||a FROM t1 ORDER BY (b||a) DESC", {"twob", "threec", "onea"}},
    })

-- -- syntax diagram select-stmt
--
test:do_select_tests(
    "e_select-0.7",
    {
        {"1", "SELECT * FROM t1", {"a", "one", "b", "two", "c", "three"}},
        {"2", "SELECT * FROM t1 ORDER BY b", {"a", "one", "c", "three", "b", "two"}},
        {"3", "SELECT * FROM t1 ORDER BY b, a", {"a", "one", "c", "three", "b", "two"}},

        {"4", "SELECT * FROM t1 LIMIT 10", {"a", "one", "b", "two", "c", "three"}},
        {"5", "SELECT * FROM t1 LIMIT 10 OFFSET 5", {}},
        {"6", "SELECT * FROM t1 LIMIT 10, 5", {}},

        {"7", "SELECT * FROM t1 ORDER BY a LIMIT 10", {"a", "one", "b", "two", "c", "three"}},
        {"8", "SELECT * FROM t1 ORDER BY b LIMIT 10 OFFSET 5", {}},
        {"9", "SELECT * FROM t1 ORDER BY a,b LIMIT 10, 5", {}},

        {"10", "SELECT * FROM t1 UNION SELECT b, a FROM t1", {"a", "one", "b", "two", "c", "three", "one", "a", "three", "c", "two", "b"}},
        {"11", "SELECT * FROM t1 UNION SELECT b, a FROM t1 ORDER BY b", {"one", "a", "two", "b", "three", "c", "a", "one", "c", "three", "b", "two"}},
        {"12", "SELECT * FROM t1 UNION SELECT b, a FROM t1 ORDER BY b, a", {"one", "a", "two", "b", "three", "c", "a", "one", "c", "three", "b", "two"}},
        {"13", "SELECT * FROM t1 UNION SELECT b, a FROM t1 LIMIT 10", {"a", "one", "b", "two", "c", "three", "one", "a", "three", "c", "two", "b"}},
        {"14", "SELECT * FROM t1 UNION SELECT b, a FROM t1 LIMIT 10 OFFSET 5", {"two", "b"}},
        {"15", "SELECT * FROM t1 UNION SELECT b, a FROM t1 LIMIT 10, 5", {}},
        {"16", "SELECT * FROM t1 UNION SELECT b, a FROM t1 ORDER BY a LIMIT 10", {"a", "one", "b", "two", "c", "three", "one", "a", "three", "c", "two", "b"}},
        {"17", "SELECT * FROM t1 UNION SELECT b, a FROM t1 ORDER BY b LIMIT 10 OFFSET 5", {"b", "two"}},
        {"18", "SELECT * FROM t1 UNION SELECT b, a FROM t1 ORDER BY a,b LIMIT 10, 5", {}},
    })

---------------------------------------------------------------------------
-- The following tests focus on FROM clause (join) processing.
--
-- EVIDENCE-OF: R-16074-54196 If the FROM clause is omitted from a simple
-- SELECT statement, then the input data is implicitly a single row zero
-- columns wide
--
test:do_select_tests(
    "e_select-1.1",
    {
        {"1", "SELECT 'abc'", {"abc"}},
        {"2", "SELECT 'abc' WHERE NULL", {}},
        {"3", "SELECT NULL", {""}},
        {"4", "SELECT count(*)", {1}},
        {"5", "SELECT count(*) WHERE false", {0}},
        {"6", "SELECT count(*) WHERE true", {1}},
    })

--
-- The following block of tests - e_select-1.4.* - test that the description
-- of cartesian joins in the SELECT documentation is consistent with sql.
-- In doing so, we test the following three requirements as a side-effect:
--
-- EVIDENCE-OF: R-49872-03192 If the join-operator is "CROSS JOIN",
-- "INNER JOIN", "JOIN" or a comma (",") and there is no ON or USING
-- clause, then the result of the join is simply the cartesian product of
-- the left and right-hand datasets.
--
--    The tests are built on this assertion. NUMBERly, they test that the output
--    of a CROSS JOIN, JOIN, INNER JOIN or "," join matches the expected result
--    of calculating the cartesian product of the left and right-hand datasets.
--
-- EVIDENCE-OF: R-46256-57243 There is no difference between the "INNER
-- JOIN", "JOIN" and "," join operators.
--
-- EVIDENCE-OF: R-25071-21202 The "CROSS JOIN" join operator produces the
-- same result as the "INNER JOIN", "JOIN" and "," operators
--
--    All tests are run 4 times, with the only difference in each run being
--    which of the 4 equivalent cartesian product join operators are used.
--    Since the output data is the same in all cases, we consider that this
--    qualifies as testing the two statements above.
--
test:do_execsql_test(
    "e_select-1.4.0",
    [[
        CREATE TABLE x1(id  INT primary key, a TEXT , b TEXT );
        CREATE TABLE x2(id  INT primary key, c NUMBER , d NUMBER , e NUMBER );
        CREATE TABLE x3(id  INT primary key, f TEXT , g TEXT , h TEXT , i TEXT );

        -- x1: 3 rows, 2 columns
        INSERT INTO x1 VALUES(1,'24', 'converging');
        INSERT INTO x1 VALUES(2, NULL, CAST(X'CB71' as TEXT));
        INSERT INTO x1 VALUES(3,'blonds', 'proprietary');

        -- x2: 2 rows, 3 columns
        INSERT INTO x2 VALUES(1,-60.06, NULL, NULL);
        INSERT INTO x2 VALUES(2,-58, NULL, 1.21);

        -- x3: 5 rows, 4 columns
        INSERT INTO x3 VALUES(1,'-39.24', NULL, 'encompass', '-1');
        INSERT INTO x3 VALUES(2,'presenting', '51', 'reformation', 'dignified');
        INSERT INTO x3 VALUES(3,'conducting', '-87.24', '37.56', NULL);
        INSERT INTO x3 VALUES(4,'coldest', '-96', 'dramatists', '82.3');
        INSERT INTO x3 VALUES(5,'alerting', NULL, '-93.79', NULL);
    ]], {
        -- <e_select-1.4.0>

        -- </e_select-1.4.0>
    })

-- EVIDENCE-OF: R-59089-25828 The columns of the cartesian product
-- dataset are, in order, all the columns of the left-hand dataset
-- followed by all the columns of the right-hand dataset.
--
do_join_test("e_select-1.4.1.1", [[
  SELECT a,b,c,d,e FROM x1 JOIN_PATTERN x2 LIMIT 1
]], {"24", "converging", -60.06, "", ""})
do_join_test("e_select-1.4.1.2", [[
  SELECT c,d,e,a,b FROM x2 JOIN_PATTERN x1 LIMIT 1
]], {-60.06, "", "", "24", "converging"})
do_join_test("e_select-1.4.1.3", [[
  SELECT f,g,h,i,c,d,e FROM x3 JOIN_PATTERN x2 LIMIT 1
]], {'-39.24', "", "encompass", '-1', -60.06, "", ""})
do_join_test("e_select-1.4.1.4", [[
  SELECT c,d,e,f,g,h,i FROM x2 JOIN_PATTERN x3 LIMIT 1
]], {-60.06, "", "", '-39.24', "", "encompass", '-1'})
-- EVIDENCE-OF: R-44414-54710 There is a row in the cartesian product
-- dataset formed by combining each unique combination of a row from the
-- left-hand and right-hand datasets.
--
do_join_test("e_select-1.4.2.1", [[
  SELECT c,d,e,f,g,h,i FROM x2 JOIN_PATTERN x3 ORDER BY +c, +f
]], { -60.06,"","","-39.24","","encompass","-1",-60.06,"","","alerting","","-93.79","",-60.06,"","","coldest","-96","dramatists","82.3",-60.06,"","","conducting","-87.24","37.56","",-60.06,"","","presenting","51","reformation","dignified",-58,"",1.21,"-39.24","","encompass","-1",-58,"",1.21,"alerting","","-93.79","",-58,"",1.21,"coldest","-96","dramatists","82.3",-58,"",1.21,"conducting","-87.24","37.56","",-58,"",1.21,"presenting","51","reformation","dignified" })
-- TODO: Come back and add a few more like the above.
-- EVIDENCE-OF: R-18439-38548 In other words, if the left-hand dataset
-- consists of Nleft rows of Mleft columns, and the right-hand dataset of
-- Nright rows of Mright columns, then the cartesian product is a dataset
-- of Nleft&times;Nright rows, each containing Mleft+Mright columns.
--
-- x1, x2    (Nlhs=3, Nrhs=2)   (Mlhs=2, Mrhs=3)
do_join_test("e_select-1.4.3.1", [[
  SELECT count(*) FROM x1 JOIN_PATTERN x2
]], {3 * 2})
test:do_test(
    "e_select-1.4.3.2",
    function()
        return #test:execsql("SELECT a,b,c,d,e FROM x1, x2") / 6
    end, 2 + 3)

-- x2, x3    (Nlhs=2, Nrhs=5)   (Mlhs=3, Mrhs=4)
do_join_test("e_select-1.4.3.3", [[
  SELECT count(*) FROM x2 JOIN_PATTERN x3
]], {2 * 5})
test:do_test(
    "e_select-1.4.3.4",
    function()
        return #test:execsql("SELECT c,d,e,f,g,h,i FROM x2 JOIN x3") / 10
    end,(3 + 4))

-- x3, x1    (Nlhs=5, Nrhs=3)   (Mlhs=4, Mrhs=2)
do_join_test("e_select-1.4.3.5", [[
  SELECT count(*) FROM x3 JOIN_PATTERN x1
]], {5 * 3})
test:do_test(
    "e_select-1.4.3.6",
    function()
        return #test:execsql("SELECT f,g,h,i,a,b FROM x3 CROSS JOIN x1") / 15
    end, (4 + 2))

-- x3, x3    (Nlhs=5, Nrhs=5)   (Mlhs=4, Mrhs=4)
do_join_test("e_select-1.4.3.7", [[
  SELECT count(*) FROM x3 JOIN_PATTERN x3
]], {5 * 5})
test:do_test(
    "e_select-1.4.3.8",
    function()
        return #test:execsql("SELECT x3.f,x3.g,x3.h,x3.i,x4.f,x4.g,x4.h,x4.i FROM x3 INNER JOIN x3 AS x4") / 25
    end, (4 + 4))

-- Some extra cartesian product tests using tables t1 and t2.
--
test:do_execsql_test(
    "e_select-1.4.4.1",
    [[
        SELECT * FROM t1, t2
    ]], t1_cross_t2)

test:do_execsql_test(
    "e_select-1.4.4.2",
    [[
        SELECT * FROM t1 AS x, t1 AS y
    ]], t1_cross_t1)

test:do_select_tests(
    "e_select-1.4.5",{
    {1, " SELECT * FROM t1 CROSS JOIN t2 ", t1_cross_t2},
    {2, " SELECT * FROM t1 AS y CROSS JOIN t1 AS x ", t1_cross_t1},
    {3, " SELECT * FROM t1 INNER JOIN t2 ", t1_cross_t2},
    {4, " SELECT * FROM t1 AS y INNER JOIN t1 AS x ", t1_cross_t1 }})


-- EVIDENCE-OF: R-38465-03616 If there is an ON clause then the ON
-- expression is evaluated for each row of the cartesian product as a
-- boolean expression. Only rows for which the expression evaluates to
-- true are included from the dataset.
--
local data ={
    {"1"," SELECT * FROM t1 JOIN_PATTERN t2 ON (true) ",t1_cross_t2},
    {"2"," SELECT * FROM t1 JOIN_PATTERN t2 ON (false) ",{}},
    {"3"," SELECT * FROM t1 JOIN_PATTERN t2 ON (NULL) ",{}},
    {"6"," SELECT * FROM t1 JOIN_PATTERN t2 ON (true) ",t1_cross_t2},
    {"9"," SELECT t1.b, t2.b FROM t1 JOIN_PATTERN t2 ON (t1.a = t2.a) ",{"one", "I", "two", "II", "three", "III"}},
    {"10"," SELECT t1.b, t2.b FROM t1 JOIN_PATTERN t2 ON (t1.a = 'a') ",{"one", "I", "one", "II", "one", "III"}},
    {"11"," SELECT t1.b, t2.b FROM t1 JOIN_PATTERN t2 ON (CASE WHEN t1.a = 'a' THEN NULL ELSE true END)",
    {"two", "I", "two", "II", "two", "III", "three", "I", "three", "II", "three", "III"}},
}
for _, val in ipairs(data) do
    local tn = val[1]
    local select = val[2]
    local res = val[3]
    do_join_test("e_select-1.3."..tn, select, res)
end
-- EVIDENCE-OF: R-49933-05137 If there is a USING clause then each of the
-- column names specified must exist in the datasets to both the left and
-- right of the join-operator.
--
test:do_catchsql_test(
    "e_select-1.4.1",
    "SELECT * FROM t1, t3 USING (b)",
    {1, "/cannot join using column B -- column not present in both tables/"})
test:do_catchsql_test(
    "e_select-1.4.2",
    "SELECT * FROM t3, t1 USING (c)",
    {1, "/cannot join using column C -- column not present in both tables/"})
test:do_catchsql_test(
    "e_select-1.4.3",
    "SELECT * FROM t3, (SELECT a AS b, b AS c FROM t1) USING (a)",
    {1, "/cannot join using column A -- column not present in both tables/"})
-- EVIDENCE-OF: R-22776-52830 For each pair of named columns, the
-- expression "lhs.X = rhs.X" is evaluated for each row of the cartesian
-- product as a boolean expression. Only rows for which all such
-- expressions evaluates to true are included from the result set.
--
test:do_select_tests(
    "e_select-1.5",
    {
        {1, "SELECT * FROM t1, t3 USING (a)", {"a", "one", 1, "b", "two", 2}},
        {2, "SELECT * FROM t3, t4 USING (a,c)", {"b", 2}}
    })

-- MUST_WORK_TEST
if (0 > 0)
 then
    -- EVIDENCE-OF: R-54046-48600 When comparing values as a result of a
    -- USING clause, the normal rules for handling affinities, collation
    -- sequences and NULL values in comparisons apply.
    --
    -- EVIDENCE-OF: R-38422-04402 The column from the dataset on the
    -- left-hand side of the join-operator is considered to be on the
    -- left-hand side of the comparison operator (=) for the purposes of
    -- collation sequence and affinity precedence.
    --

    -- Legacy from the original code. Must be replaced with analogue
    -- functions from box.
    local X = nil
    test:do_execsql_test(
        "e_select-1.6.0",
        [[
            CREATE TABLE t5(a  TEXT COLLATE "unicode_ci", b  TEXT COLLATE "binary");
            INSERT INTO t5 VALUES('AA', 'cc');
            INSERT INTO t5 VALUES('BB', 'dd');
            INSERT INTO t5 VALUES(NULL, NULL);
            CREATE TABLE t6(a  TEXT COLLATE "binary", b  TEXT COLLATE "unicode_ci");
            INSERT INTO t6 VALUES('aa', 'cc');
            INSERT INTO t6 VALUES('bb', 'DD');
            INSERT INTO t6 VALUES(NULL, NULL);
        ]], {
            -- <e_select-1.6.0>

            -- </e_select-1.6.0>
        })

    -- Legacy from the original code. Must be replaced with analogue
    -- functions from box.
    local res = nil
    local tn = nil
    for _ in X(0, "X!foreach", [=[["tn select res","\n     1 { SELECT * FROM t5 JOIN_PATTERN t6 USING (a) } {AA cc cc BB dd DD}\n     2 { SELECT * FROM t6 JOIN_PATTERN t5 USING (a) } {}\n     3 { SELECT * FROM (SELECT a COLLATE "unicode_ci", b FROM t6) JOIN_PATTERN t5 USING (a) } \n       {aa cc cc bb DD dd}\n     4 { SELECT * FROM t5 JOIN_PATTERN t6 USING (a,b) } {AA cc}\n     5 { SELECT * FROM t6 JOIN_PATTERN t5 USING (a,b) } {}\n   "]]=]) do
        do_join_test("e_select-1.6."..tn, select, res)
    end
    -- EVIDENCE-OF: R-57047-10461 For each pair of columns identified by a
    -- USING clause, the column from the right-hand dataset is omitted from
    -- the joined dataset.
    --
    -- EVIDENCE-OF: R-56132-15700 This is the only difference between a USING
    -- clause and its equivalent ON constraint.
    --
    for _ in X(0, "X!foreach", [=[["tn select res","\n     1a { SELECT * FROM t1 JOIN_PATTERN t2 USING (a)      } \n        {a one I b two II c three III}\n     1b { SELECT * FROM t1 JOIN_PATTERN t2 ON (t1.a=t2.a) }\n        {a one a I b two b II c three c III}\n\n     2a { SELECT * FROM t3 JOIN_PATTERN t4 USING (a)      }  \n        {a 1 {} b 2 2}\n     2b { SELECT * FROM t3 JOIN_PATTERN t4 ON (t3.a=t4.a) } \n        {a 1 a {} b 2 b 2}\n\n     3a { SELECT * FROM t3 JOIN_PATTERN t4 USING (a,c)                  } {b 2}\n     3b { SELECT * FROM t3 JOIN_PATTERN t4 ON (t3.a=t4.a AND t3.c=t4.c) } {b 2 b 2}\n\n     4a { SELECT * FROM (SELECT a COLLATE "unicode_ci", b FROM t6) AS x \n          JOIN_PATTERN t5 USING (a) } \n        {aa cc cc bb DD dd}\n     4b { SELECT * FROM (SELECT a COLLATE "unicode_ci", b FROM t6) AS x\n          JOIN_PATTERN t5 ON (x.a=t5.a) } \n        {aa cc AA cc bb DD BB dd}\n   "]]=]) do
        do_join_test("e_select-1.7."..tn, select, res)
    end
    X(630, "X!cmd", [=[["EVIDENCE-OF:","R-42531-52874","If","the","join-operator","is","a","LEFT JOIN","or"]]=])

    X(632, "X!cmd", [=[["been","applied,","an","extra","row","is","added","to","the","output","for","each","row","in","the"]]=])
    X(633, "X!cmd", [=[["original","left-hand","input","dataset","that","corresponds","to","no","rows","at","all","in"]]=])
    X(634, "X!cmd", [=[["the","composite","dataset","(if","any)."]]=])
end
test:do_execsql_test(
    "e_select-1.8.0",
    [[
        CREATE TABLE t7(a TEXT PRIMARY KEY, b TEXT, c INT );
        CREATE TABLE t8(a TEXT PRIMARY KEY, d TEXT, e INT );

        INSERT INTO t7 VALUES('x', 'ex',  24);
        INSERT INTO t7 VALUES('y', 'why', 25);

        INSERT INTO t8 VALUES('x', 'abc', 24);
        INSERT INTO t8 VALUES('z', 'ghi', 26);
    ]], {
        -- <e_select-1.8.0>

        -- </e_select-1.8.0>
    })

test:do_select_tests(
    "e_select-1.8",
    {
        {"1a", "SELECT count(*) FROM t7 JOIN t8 ON (t7.a=t8.a)", {1}},
        {"1b", "SELECT count(*) FROM t7 LEFT JOIN t8 ON (t7.a=t8.a)", {2}},
        {"2a", "SELECT count(*) FROM t7 JOIN t8 USING (a)", {1}},
        {"2b", "SELECT count(*) FROM t7 LEFT JOIN t8 USING (a)", {2}},
    })

-- EVIDENCE-OF: R-15607-52988 The added rows contain NULL values in the
-- columns that would normally contain values copied from the right-hand
-- input dataset.
--
test:do_select_tests(
    "e_select-1.9",
    {
        {"1a", "SELECT * FROM t7 JOIN t8 ON (t7.a=t8.a)", {"x", "ex", 24, "x", "abc", 24}},
        {"1b", "SELECT * FROM t7 LEFT JOIN t8 ON (t7.a=t8.a)", {"x", "ex", 24, "x", "abc", 24, "y", "why", 25, "", "", ""}},
        {"2a", "SELECT * FROM t7 JOIN t8 USING (a)", {"x", "ex", 24, "abc", 24}},
        {"2b", "SELECT * FROM t7 LEFT JOIN t8 USING (a)", {"x", "ex", 24, "abc", 24, "y", "why", 25, "", ""}},
    })

-- EVIDENCE-OF: R-04932-55942 If the NATURAL keyword is in the
-- join-operator then an implicit USING clause is added to the
-- join-constraints. The implicit USING clause contains each of the
-- column names that appear in both the left and right-hand input
-- datasets.
--
test:do_select_tests(
    "e_select-1-10",
    {
        {"1a", "SELECT * FROM t7 JOIN t8 USING (a)", {"x", "ex", 24, "abc", 24}},
        {"1b", "SELECT * FROM t7 NATURAL JOIN t8", {"x", "ex", 24, "abc", 24}},

        {"2a", "SELECT * FROM t8 JOIN t7 USING (a)", {"x", "abc", 24, "ex", 24}},
        {"2b", "SELECT * FROM t8 NATURAL JOIN t7", {"x", "abc", 24, "ex", 24}},

        {"3a", "SELECT * FROM t7 LEFT JOIN t8 USING (a)", {"x", "ex", 24, "abc", 24, "y", "why", 25, "", ""}},
        {"3b", "SELECT * FROM t7 NATURAL LEFT JOIN t8", {"x", "ex", 24, "abc", 24, "y", "why", 25, "", ""}},

        {"4a", "SELECT * FROM t8 LEFT JOIN t7 USING (a)", {"x", "abc", 24, "ex", 24, "z", "ghi", 26, "", ""}},
        {"4b", "SELECT * FROM t8 NATURAL LEFT JOIN t7", {"x", "abc", 24, "ex", 24, "z", "ghi", 26, "", ""}},

        {"5a", "SELECT * FROM t3 JOIN t4 USING (a,c)", {"b", 2}},
        {"5b", "SELECT * FROM t3 NATURAL JOIN t4", {"b", 2}},

        {"6a", "SELECT * FROM t3 LEFT JOIN t4 USING (a,c)", {"a", 1, "b", 2}},
        {"6b", "SELECT * FROM t3 NATURAL LEFT JOIN t4", {"a", 1, "b", 2}},
    })

-- EVIDENCE-OF: R-49566-01570 If the left and right-hand input datasets
-- feature no common column names, then the NATURAL keyword has no effect
-- on the results of the join.
--
test:do_execsql_test(
    "e_select-1.11.0",
    [[
        CREATE TABLE t10(id  INT primary key, x INT , y TEXT);
        INSERT INTO t10 VALUES(1, 1, 'true');
        INSERT INTO t10 VALUES(2, 0, 'false');
    ]], {
        -- <e_select-1.11.0>

        -- </e_select-1.11.0>
    })

test:do_select_tests(
    "e_select-1-11",
    {
        {"1a", "SELECT a, x FROM t1 CROSS JOIN t10", {"a", 1, "a", 0, "b", 1, "b", 0, "c", 1, "c", 0}},
        {"1b", "SELECT a, x FROM t1 NATURAL CROSS JOIN t10", {"a", 1, "a", 0, "b", 1, "b", 0, "c", 1, "c", 0}},
    })

-- EVIDENCE-OF: R-39625-59133 A USING or ON clause may not be added to a
-- join that specifies the NATURAL keyword.
--
data = {
    "SELECT * FROM t1 NATURAL LEFT JOIN t2 USING (a)",
    "SELECT * FROM t1 NATURAL LEFT JOIN t2 ON (t1.a=t2.a)",
    "SELECT * FROM t1 NATURAL LEFT JOIN t2 ON (45)",
}
for tn, sql in ipairs(data) do
    test:do_catchsql_test(
        "e_select-1.12."..tn,
string.format([[
            %s
        ]], sql), {
            1, "a NATURAL join may not have an ON or USING clause"
        })

end
---------------------------------------------------------------------------
-- The next block of tests - e_select-3.* - concentrate on verifying
-- statements made regarding WHERE clause processing.
--
test:drop_all_tables()
test:do_execsql_test(
    "e_select-3.0",
    [[
        CREATE TABLE x1(id  INT PRIMARY KEY, k INT , x TEXT , y TEXT , z TEXT );
        INSERT INTO x1 VALUES(1, 1, 'relinquished', 'aphasia', '78.43');
        INSERT INTO x1 VALUES(2, 2, 'A8E8D66F',    '07CF',   '-81');
        INSERT INTO x1 VALUES(3, 3, '-22',            '-27.57',    NULL);
        INSERT INTO x1 VALUES(4, 4, NULL,           'bygone',  'picky');
        INSERT INTO x1 VALUES(5, 5, NULL,           '96.28',     NULL);
        INSERT INTO x1 VALUES(6, 6, '0',              '1',         '2');

        CREATE TABLE x2(id  INT primary key, k INT , x TEXT , y2 TEXT );
        INSERT INTO x2 VALUES(1, 1, '50', 'B82838');
        INSERT INTO x2 VALUES(2, 5, '84.79', '65.88');
        INSERT INTO x2 VALUES(3, 3, '-22', '0E1BE452A393');
        INSERT INTO x2 VALUES(4, 7, 'mistrusted', 'standardized');
    ]], {
        -- <e_select-3.0>

        -- </e_select-3.0>
    })

-- EVIDENCE-OF: R-60775-64916 If a WHERE clause is specified, the WHERE
-- expression is evaluated for each row in the input data as a boolean
-- expression. Only rows for which the WHERE clause expression evaluates
-- to true are included from the dataset before continuing.
--

test:do_execsql_test(
    "e_select-3.1.5",
    [[
        SELECT k FROM x1 WHERE x IS NULL
    ]], {
        -- <e_select-3.1.5>
        4, 5
        -- </e_select-3.1.5>
    })

test:do_execsql_test(
    "e_select-3.2.1a",
    [[
        SELECT k FROM x1 LEFT JOIN x2 USING(k)
    ]], {
        -- <e_select-3.2.1a>
        1, 2, 3, 4, 5, 6
        -- </e_select-3.2.1a>
    })

test:do_execsql_test(
    "e_select-3.2.1b",
    [[
        SELECT k FROM x1 LEFT JOIN x2 USING(k) WHERE x2.k <> 0
    ]], {
        -- <e_select-3.2.1b>
        1, 3, 5
        -- </e_select-3.2.1b>
    })

test:do_execsql_test(
    "e_select-3.2.2",
    [[
        SELECT k FROM x1 LEFT JOIN x2 USING(k) WHERE x2.k IS NULL
    ]], {
        -- <e_select-3.2.2>
        2, 4, 6
        -- </e_select-3.2.2>
    })

test:do_execsql_test(
    "e_select-3.2.3",
    [[
        SELECT k FROM x1 NATURAL JOIN x2 WHERE x2.k <> 0
    ]], {
        -- <e_select-3.2.3>
        3
        -- </e_select-3.2.3>
    })

test:do_execsql_test(
    "e_select-3.2.4",
    [[
        SELECT k FROM x1 NATURAL JOIN x2 WHERE x2.k-3 <> 0
    ]], {
        -- <e_select-3.2.4>

        -- </e_select-3.2.4>
    })

---------------------------------------------------------------------------
-- Tests below this point are focused on verifying the testable statements
-- related to caculating the result rows of a simple SELECT statement.
--
test:drop_all_tables()
test:do_execsql_test(
    "e_select-4.0",
    [[
        CREATE TABLE z1(id  INT primary key, a NUMBER, b NUMBER, c TEXT);
        CREATE TABLE z2(id  INT primary key, d NUMBER, e NUMBER);
        CREATE TABLE z3(id  INT primary key, a NUMBER, b NUMBER);

        INSERT INTO z1 VALUES(1, 51.65, -59.58, 'belfries');
        INSERT INTO z1 VALUES(2, -5, NULL, '75');
        INSERT INTO z1 VALUES(3, -2.2, -23.18, 'suiters');
        INSERT INTO z1 VALUES(4, NULL, 67, 'quartets');
        INSERT INTO z1 VALUES(5, -1.04, -32.3, 'aspen');
        INSERT INTO z1 VALUES(6, 63, 0, '-26');

        INSERT INTO z2 VALUES(1, NULL, 21);
        INSERT INTO z2 VALUES(2, 36.0, 6.0);

        INSERT INTO z3 VALUES(1, 123.21, 123.12);
        INSERT INTO z3 VALUES(2, 49.17, -67);
    ]], {
        -- <e_select-4.0>

        -- </e_select-4.0>
    })

-- EVIDENCE-OF: R-36327-17224 If a result expression is the special
-- expression "*" then all columns in the input data are substituted for
-- that one expression.
--
-- EVIDENCE-OF: R-43693-30522 If the expression is the alias of a table
-- or subquery in the FROM clause followed by ".*" then all columns from
-- the named table or subquery are substituted for the single expression.
--
test:do_select_tests(
    "e_select-4.1",
    {
        {"1", "SELECT a,b,c FROM z1 LIMIT 1", {51.65, -59.58, "belfries"}},
        {"2", "SELECT a,b,c,d,e FROM z1,z2 LIMIT 1", {51.65, -59.58, "belfries", "", 21}},
        {"3", "SELECT z1.a, z1.b, z1.c FROM z1,z2 LIMIT 1", {51.65, -59.58, "belfries"}},
        {"4", "SELECT z2.d, z2.e FROM z1,z2 LIMIT 1", {"", 21}},
        {"5", "SELECT z2.d, z2.e, z1.a, z1.b, z1.c FROM z1,z2 LIMIT 1", {"", 21, 51.65, -59.58, "belfries"}},

        {"6", "SELECT count(*), a,b,c FROM z1", {6, 63, 0, "-26"}},
        {"7", "SELECT max(a), a,b,c FROM z1", {63, 63, 0, "-26"}},
        {"8", "SELECT a,b,c, min(a) FROM z1", {-5, "", "75", -5}},

        {"9", "SELECT a,b,c,d,e,a,b,c,d,e FROM z1,z2 LIMIT 1", {
            51.65, -59.58, "belfries", "", 21, 51.65, -59.58, "belfries", "", 21}},
        {"10", "SELECT z1.a, z1.b, z1.c,z1.a, z1.b, z1.c FROM z2,z1 LIMIT 1", {
            51.65, -59.58, "belfries", 51.65, -59.58, "belfries"}},
    })

-- EVIDENCE-OF: R-38023-18396 It is an error to use a "*" or "alias.*"
-- expression in any context other than a result expression list.
--
-- EVIDENCE-OF: R-44324-41166 It is also an error to use a "*" or
-- "alias.*" expression in a simple SELECT query that does not have a
-- FROM clause.
--
data = {
    {"1.1", "SELECT a, b, c FROM z1 WHERE *",  "Syntax error at line 1 near '*'"},
    {"1.2", "SELECT a, b, c FROM z1 GROUP BY *", "Syntax error at line 1 near '*'"},
    {"1.3", "SELECT 1 + * FROM z1",  "Syntax error at line 1 near '*'"},
    {"1.4", "SELECT * + 1 FROM z1", test.sqlparser == 'box_execute' and
        "Failed to expand '*' in SELECT statement without FROM clause" or
        "Syntax error at line 1 near '+'"},
    {"2.1", "SELECT *", "Failed to expand '*' in SELECT statement without FROM clause"},
    {"2.2", "SELECT * WHERE 1", "Failed to expand '*' in SELECT statement without FROM clause"},
    {"2.3", "SELECT * WHERE 0", "Failed to expand '*' in SELECT statement without FROM clause"},
    {"2.4", "SELECT count(*), *", "Failed to expand '*' in SELECT statement without FROM clause"}
}

for _, val in ipairs(data) do
    local tn = val[1]
    local select = val[2]
    local err = val[3]
    test:do_catchsql_test(
        "e_select-4.2."..tn,
        select, {
            1, err
        })

end
-- EVIDENCE-OF: R-08669-22397 The number of columns in the rows returned
-- by a simple SELECT statement is equal to the number of expressions in
-- the result expression list after substitution of * and alias.*
-- expressions.
--

-- MUST_WORK_TEST prepared statement
if 0>0 then
-- Legacy from the original code. Must be replaced with analogue
-- functions from box.
local X = nil
local sql_finalize = nil
local sql_prepare_v2 = nil
local nCol = nil
local tn = nil
for _ in X(0, "X!foreach", [=[["tn select nCol","\n  1   \"SELECT a,b,c FROM z1\" 3\n  2   \"SELECT a,b,c FROM z1 NATURAL JOIN z3\"  3\n  3   \"SELECT z1.a,z1.b,z1.c FROM z1 NATURAL JOIN z3\" 3\n  4   \"SELECT z3.a,z3.b FROM z1 NATURAL JOIN z3\" 2\n  5   \"SELECT z1.a,z1.b,z1.c, z3.a,z3.b FROM z1 NATURAL JOIN z3\" 5\n  6   \"SELECT 1, 2, z1.a,z1.b,z1.c FROM z1\" 5\n  7   \"SELECT a, a,b,c, b, c FROM z1\" 6\n"]]=]) do
    local stmt = sql_prepare_v2("db", select, -1, "DUMMY")
    test:do_sql_column_count_test(
        "e_select-4.3."..tn,
        stmt, {
            nCol
        })

    sql_finalize(stmt)
end
end
-- In lang_select.html, a non-aggregate query is defined as any simple SELECT
-- that has no GROUP BY clause and no aggregate expressions in the result
-- expression list. Other queries are aggregate queries. Test cases
-- e_select-4.4.* through e_select-4.12.*, inclusive, which test the part of
-- simple SELECT that is different for aggregate and non-aggregate queries
-- verify (in a way) that these definitions are consistent:
--
-- EVIDENCE-OF: R-20637-43463 A simple SELECT statement is an aggregate
-- query if it contains either a GROUP BY clause or one or more aggregate
-- functions in the result-set.
--
-- EVIDENCE-OF: R-23155-55597 Otherwise, if a simple SELECT contains no
-- aggregate functions or a GROUP BY clause, it is a non-aggregate query.
--
-- EVIDENCE-OF: R-44050-47362 If the SELECT statement is a non-aggregate
-- query, then each expression in the result expression list is evaluated
-- for each row in the dataset filtered by the WHERE clause.
--
test:do_select_tests(
    "e_select-4.4",
    {
        {"1", "SELECT a, b FROM z1", {51.65, -59.58, -5, "", -2.2, -23.18, "", 67, -1.04, -32.3, 63, 0}},

        {"2", "SELECT a IS NULL, b+1, a,b,c FROM z1",
            {false, -58.58, 51.65, -59.58, "belfries", false, "", -5, "", "75",
                false, -22.18, -2.2, -23.18, "suiters", true, 68, "", 67, "quartets", false, -31.3,
                -1.04, -32.3, "aspen", false, 1, 63, 0, "-26"}},

        {"3", "SELECT 32*32, CAST(d AS TEXT) || CAST(e AS TEXT) FROM z2", {1024, "", 1024, "36.06.0"}},
    })

-- Test cases e_select-4.5.* and e_select-4.6.* together show that:
--
-- EVIDENCE-OF: R-51988-01124 The single row of result-set data created
-- by evaluating the aggregate and non-aggregate expressions in the
-- result-set forms the result of an aggregate query without a GROUP BY
-- clause.
--
-- EVIDENCE-OF: R-57629-25253 If the SELECT statement is an aggregate
-- query without a GROUP BY clause, then each aggregate expression in the
-- result-set is evaluated once across the entire dataset.
--
test:do_select_tests(
    "e_select-4.5",
    {
        {"1", "SELECT count(a), max(a), count(b), max(b) FROM z1", {5, 63, 5, 67}},
        {"2", "SELECT count(*), max(1)", {1, 1}},

        {"3", "SELECT sum(b+1) FROM z1 NATURAL LEFT JOIN z3", {-43.06}},
        {"4", "SELECT sum(b+2) FROM z1 NATURAL LEFT JOIN z3", {-38.06}},
        {"5", "SELECT sum(CAST(b IS NOT NULL AS INTEGER)) FROM z1 NATURAL LEFT JOIN z3", {5}},
    })

-- EVIDENCE-OF: R-26684-40576 Each non-aggregate expression in the
-- result-set is evaluated once for an arbitrarily selected row of the
-- dataset.
--
-- EVIDENCE-OF: R-27994-60376 The same arbitrarily selected row is used
-- for each non-aggregate expression.
--
--   Note: The results of many of the queries in this block of tests are
--   technically undefined, as the documentation does not specify which row
--   sql will arbitrarily select to use for the evaluation of the
--   non-aggregate expressions.
--
test:drop_all_tables()
test:do_execsql_test(
    "e_select-4.6.0",
    [[
        CREATE TABLE a1(one  INT PRIMARY KEY, two INT );
        INSERT INTO a1 VALUES(1, 1);
        INSERT INTO a1 VALUES(2, 3);
        INSERT INTO a1 VALUES(3, 6);
        INSERT INTO a1 VALUES(4, 10);

        CREATE TABLE a2(one  INT PRIMARY KEY, three INT );
        INSERT INTO a2 VALUES(1, 1);
        INSERT INTO a2 VALUES(3, 2);
        INSERT INTO a2 VALUES(6, 3);
        INSERT INTO a2 VALUES(10, 4);
    ]], {
        -- <e_select-4.6.0>

        -- </e_select-4.6.0>
    })

test:do_select_tests(
    "e_select-4.6",
    {
        {"1", "SELECT one, two, count(*) FROM a1", {4, 10, 4}},
        {"2", "SELECT one, two, count(*) FROM a1 WHERE one<3", {2, 3, 2}},
        {"3", "SELECT one, two, count(*) FROM a1 WHERE one>3", {4, 10, 1}},
        {"4", "SELECT *, count(*) FROM a1 JOIN a2", {4, 10, 10, 4, 16}},
        {"5", "SELECT *, sum(three) FROM a1 NATURAL JOIN a2", {3, 6, 2, 3}},
        {"6", "SELECT *, sum(three) FROM a1 NATURAL JOIN a2", {3, 6, 2, 3}},
        {"7", "SELECT group_concat(three, ''), a1.* FROM a1 NATURAL JOIN a2", {"12", 3, 6}},
    })

-- EVIDENCE-OF: R-04486-07266 Or, if the dataset contains zero rows, then
-- each non-aggregate expression is evaluated against a row consisting
-- entirely of NULL values.
--
test:do_select_tests(
    "e_select-4.7",
    {
        {"1", "SELECT one, two, count(*) FROM a1 WHERE false", {"", "", 0}},
        {"2", "SELECT sum(two), * FROM a1, a2 WHERE three>5", {"", "", "", "", ""}},
        {"3", "SELECT max(one) IS NULL, one IS NULL, two IS NULL FROM a1 WHERE two=7", {true, true, true}},
    })

-- EVIDENCE-OF: R-64138-28774 An aggregate query without a GROUP BY
-- clause always returns exactly one row of data, even if there are zero
-- rows of input data.
--
-- MUST_WORK_TEST prepared statements
if 0>0 then
-- Legacy from the original code. Must be replaced with analogue
-- functions from box.
local X = nil
local sql_finalize = nil
local sql_prepare_v2 = nil
for _ in X(0, "X!foreach", [=[["tn select","\n  8.1  \"SELECT count(*) FROM a1\"\n  8.2  \"SELECT count(*) FROM a1 WHERE 0\"\n  8.3  \"SELECT count(*) FROM a1 WHERE 1\"\n  8.4  \"SELECT max(a1.one)+min(two), a1.one, two, * FROM a1, a2 WHERE 1\"\n  8.5  \"SELECT max(a1.one)+min(two), a1.one, two, * FROM a1, a2 WHERE 0\"\n"]]=]) do
    -- Set $nRow to the number of rows returned by $select:
    local stmt, nRow
    stmt = sql_prepare_v2("db", select, -1, "DUMMY")
    nRow = 0
    while X(979, "X!cmd", [=[["expr","\"sql_ROW\" == [sql_step $::stmt]"]]=])
 do
        nRow = nRow + 1
    end
    sql_finalize(stmt)
    -- Test that $nRow==1 and that statement execution was successful
    -- (rc==sql_OK).

    -- Legacy from the original code. Must be replaced with analogue
    -- functions from box.
    local X = nil
    X(983, "X!cmd", [=[["do_test",["e_select-4.",["tn"]],[["list","list",["rc"],["nRow"]]],"sql_OK 1"]]=])
end
end
test:drop_all_tables()
test:do_execsql_test(
    "e_select-4.9.0",
    [[
        CREATE TABLE b1(one  INT PRIMARY KEY, two TEXT);
        INSERT INTO b1 VALUES(1, 'o');
        INSERT INTO b1 VALUES(4, 'f');
        INSERT INTO b1 VALUES(3, 't');
        INSERT INTO b1 VALUES(2, 't');
        INSERT INTO b1 VALUES(5, 'f');
        INSERT INTO b1 VALUES(7, 's');
        INSERT INTO b1 VALUES(6, 's');

        CREATE TABLE b2(x TEXT, y  INT PRIMARY KEY);
        INSERT INTO b2 VALUES(NULL, 0);
        INSERT INTO b2 VALUES(NULL, 1);
        INSERT INTO b2 VALUES('xyz', 2);
        INSERT INTO b2 VALUES('abc', 3);
        INSERT INTO b2 VALUES('xyz', 4);

        CREATE TABLE b3(id  INT PRIMARY KEY, a  TEXT COLLATE "unicode_ci", b  TEXT COLLATE "binary");
        INSERT INTO b3 VALUES(1, 'abc', 'abc');
        INSERT INTO b3 VALUES(2, 'aBC', 'aBC');
        INSERT INTO b3 VALUES(3, 'Def', 'Def');
        INSERT INTO b3 VALUES(4, 'dEF', 'dEF');
    ]], {
        -- <e_select-4.9.0>

        -- </e_select-4.9.0>
    })

-- EVIDENCE-OF: R-07284-35990 If the SELECT statement is an aggregate
-- query with a GROUP BY clause, then each of the expressions specified
-- as part of the GROUP BY clause is evaluated for each row of the
-- dataset. Each row is then assigned to a "group" based on the results;
-- rows for which the results of evaluating the GROUP BY expressions are
-- the same get assigned to the same group.
--
--   These tests also show that the following is not untrue:
--
-- EVIDENCE-OF: R-25883-55063 The expressions in the GROUP BY clause do
-- not have to be expressions that appear in the result.
--
-- MUST_WORK_TEST ---> 4 is wrong
test:do_select_tests(
    "e_select-4.9",
    {
        {"1", "SELECT group_concat(one), two FROM b1 GROUP BY two", {"4,5","f","1","o","6,7","s","2,3","t"}},
        {"2", "SELECT group_concat(one), sum(one) FROM b1 GROUP BY (one>4)", {"1,2,3,4",10,"5,6,7",18}},
        {"3", "SELECT group_concat(one) FROM b1 GROUP BY (two>'o'), one%2", {"4","1,5","2,6","3,7"}},
        {"4", "SELECT group_concat(one) FROM b1 GROUP BY (one==2 OR two=='o')", {"3,4,5,6,7","1,2"}},
    })

-- EVIDENCE-OF: R-14926-50129 For the purposes of grouping rows, NULL
-- values are considered equal.
--
test:do_select_tests(
    "e_select-4.10",
    {
        {"1", "SELECT group_concat(y) FROM b2 GROUP BY x", {"0,1","3","2,4"}},
        {"2", "SELECT count(*) FROM b2 GROUP BY CASE WHEN y<4 THEN NULL ELSE 0 END", {4, 1}},
    })

-- EVIDENCE-OF: R-10470-30318 The usual rules for selecting a collation
-- sequence with which to compare text values apply when evaluating
-- expressions in a GROUP BY clause.
--
test:do_select_tests(
    "e_select-4.11",
    {
        {"1", "SELECT count(*) FROM b3 GROUP BY b", {1, 1, 1, 1}},
        {"2", "SELECT count(*) FROM b3 GROUP BY a", {2, 2}},
        {"3", "SELECT count(*) FROM b3 GROUP BY +b", {1, 1, 1, 1}},
        {"4", "SELECT count(*) FROM b3 GROUP BY +a", {2, 2}},
        {"5", "SELECT count(*) FROM b3 GROUP BY b||''", {1, 1, 1, 1}},
        {"6", "SELECT count(*) FROM b3 GROUP BY a||''", {1, 1, 1, 1}},
    })

-- EVIDENCE-OF: R-63573-50730 The expressions in a GROUP BY clause may
-- not be aggregate expressions.
--
data = {
    {"12.1", "SELECT a,b FROM b3 GROUP BY count(*)"},
    {"12.2", "SELECT max(a) FROM b3 GROUP BY max(b)"},
    {"12.3", "SELECT group_concat(a) FROM b3 GROUP BY a, max(b)"},
}
for _, val in ipairs(data) do
    local tn = val[1]
    local select = val[2]
    local res = {1, "aggregate functions are not allowed in the GROUP BY clause"}
    test:do_catchsql_test(
        "e_select-4."..tn,
        select, res)

end
-- EVIDENCE-OF: R-31537-00101 If a HAVING clause is specified, it is
-- evaluated once for each group of rows as a boolean expression. If the
-- result of evaluating the HAVING clause is false, the group is
-- discarded.
--
--   This requirement is tested by all e_select-4.13.* tests.
--
-- EVIDENCE-OF: R-04132-09474 If the HAVING clause is an aggregate
-- expression, it is evaluated across all rows in the group.
--
--   Tested by e_select-4.13.1.*
--
-- EVIDENCE-OF: R-28262-47447 If a HAVING clause is a non-aggregate
-- expression, it is evaluated with respect to an arbitrarily selected
-- row from the group.
--
--   Tested by e_select-4.13.2.*
--
--   Tests in this block also show that this is not untrue:
--
-- EVIDENCE-OF: R-55403-13450 The HAVING expression may refer to values,
-- even aggregate functions, that are not in the result.
--
test:do_execsql_test(
    "e_select-4.13.0",
    [[
        CREATE TABLE c1(up TEXT, down  INT PRIMARY KEY);
        INSERT INTO c1 VALUES('x', 1);
        INSERT INTO c1 VALUES('x', 2);
        INSERT INTO c1 VALUES('x', 4);
        INSERT INTO c1 VALUES('x', 8);
        INSERT INTO c1 VALUES('y', 16);
        INSERT INTO c1 VALUES('y', 32);

        CREATE TABLE c2(i  INT PRIMARY KEY, j INT );
        INSERT INTO c2 VALUES(1, 0);
        INSERT INTO c2 VALUES(2, 1);
        INSERT INTO c2 VALUES(3, 3);
        INSERT INTO c2 VALUES(4, 6);
        INSERT INTO c2 VALUES(5, 10);
        INSERT INTO c2 VALUES(6, 15);
        INSERT INTO c2 VALUES(7, 21);
        INSERT INTO c2 VALUES(8, 28);
        INSERT INTO c2 VALUES(9, 36);

        CREATE TABLE c3(i  INT PRIMARY KEY, k TEXT);
        INSERT INTO c3 VALUES(1,  'hydrogen');
        INSERT INTO c3 VALUES(2,  'helium');
        INSERT INTO c3 VALUES(3,  'lithium');
        INSERT INTO c3 VALUES(4,  'beryllium');
        INSERT INTO c3 VALUES(5,  'boron');
        INSERT INTO c3 VALUES(94, 'plutonium');
    ]], {
        -- <e_select-4.13.0>

        -- </e_select-4.13.0>
    })

test:do_select_tests(
    "e_select-4.13",
    {
        {"1.1", "SELECT up FROM c1 GROUP BY up HAVING count(*)>3", {"x"}},
        {"1.2", "SELECT up FROM c1 GROUP BY up HAVING sum(down)>16", {"y"}},
        {"1.3", "SELECT up FROM c1 GROUP BY up HAVING sum(down)<16", {"x"}},
        {"1.4", "SELECT up|| CAST(down AS TEXT) FROM c1 GROUP BY (down<5) HAVING max(down)<10", {"x4"}},

        {"2.1", "SELECT up FROM c1 GROUP BY up HAVING down>10", {"y"}},
        {"2.2", "SELECT up FROM c1 GROUP BY up HAVING up='y'", {"y"}},

        {"2.3", "SELECT i, j FROM c2 GROUP BY i>4 HAVING i>6", {9, 36}},
    })

-- EVIDENCE-OF: R-23927-54081 Each expression in the result-set is then
-- evaluated once for each group of rows.
--
-- EVIDENCE-OF: R-53735-47017 If the expression is an aggregate
-- expression, it is evaluated across all rows in the group.
--
test:do_select_tests(
    "e_select-4.15",
    {
        {"1", "SELECT sum(down) FROM c1 GROUP BY up", {15, 48}},
        {"2", "SELECT sum(j), max(j) FROM c2 GROUP BY (i%3)", {54, 36, 27, 21, 39, 28}},
        {"3", "SELECT sum(j), max(j) FROM c2 GROUP BY (j%2)", {80, 36, 40, 21}},
        {"4", "SELECT 1+sum(j), max(j)+1 FROM c2 GROUP BY (j%2)", {81, 37, 41, 22}},
        {"5", "SELECT count(*), round(avg(i),2) FROM c1, c2 ON (i=down) GROUP BY j%2", {3, 4.33, 1, 2.0}},
    })

-- EVIDENCE-OF: R-62913-19830 Otherwise, it is evaluated against a single
-- arbitrarily chosen row from within the group.
--
-- EVIDENCE-OF: R-53924-08809 If there is more than one non-aggregate
-- expression in the result-set, then all such expressions are evaluated
-- for the same row.
--
test:do_select_tests(
    "e_select-4.15",
    {
        {"1", "SELECT i, j FROM c2 GROUP BY i%2", {8, 28, 9, 36}},
        {"2", "SELECT i, j FROM c2 GROUP BY i%2 HAVING j<30", {8, 28}},
        {"3", "SELECT i, j FROM c2 GROUP BY i%2 HAVING j>30", {9, 36}},
        {"4", "SELECT i, j FROM c2 GROUP BY i%2 HAVING j>30", {9, 36}},
        {"5", "SELECT count(*), i, k FROM c2 NATURAL JOIN c3 GROUP BY substr(k, 1, 1)",
            {2, 5, "boron", 2, 2, "helium", 1, 3, "lithium"}},
})

-- EVIDENCE-OF: R-19334-12811 Each group of input dataset rows
-- contributes a single row to the set of result rows.
--
-- EVIDENCE-OF: R-02223-49279 Subject to filtering associated with the
-- DISTINCT keyword, the number of rows returned by an aggregate query
-- with a GROUP BY clause is the same as the number of groups of rows
-- produced by applying the GROUP BY and HAVING clauses to the filtered
-- input dataset.
--
test:do_select_tests(
    "e_select.4.16",
    {
        {1, "select count(*) from (SELECT i, j FROM c2 GROUP BY i%2)", {2}},
        {2, "select count(*) from (SELECT i, j FROM c2 GROUP BY i)", {9}},
        {3, "select count(*) from (SELECT i, j FROM c2 GROUP BY i HAVING i<5)", {4}}})
---------------------------------------------------------------------------
-- The following tests attempt to verify statements made regarding the ALL
-- and DISTINCT keywords.
--
test:drop_all_tables()
test:do_execsql_test(
    "e_select-5.1.0",
    [[
        CREATE TABLE h1(id  INT primary key, a INT , b TEXT);
        INSERT INTO h1 VALUES(1, 1, 'one');
        INSERT INTO h1 VALUES(2, 1, 'I');
        INSERT INTO h1 VALUES(3, 1, 'i');
        INSERT INTO h1 VALUES(4, 4, 'four');
        INSERT INTO h1 VALUES(5, 4, 'IV');
        INSERT INTO h1 VALUES(6, 4, 'iv');

        CREATE TABLE h2(id  INT primary key, x  TEXT COLLATE "unicode_ci");
        INSERT INTO h2 VALUES(1, 'One');
        INSERT INTO h2 VALUES(2, 'Two');
        INSERT INTO h2 VALUES(3, 'Three');
        INSERT INTO h2 VALUES(4, 'Four');
        INSERT INTO h2 VALUES(5, 'one');
        INSERT INTO h2 VALUES(6, 'two');
        INSERT INTO h2 VALUES(7, 'three');
        INSERT INTO h2 VALUES(8, 'four');

        CREATE TABLE h3(c  INT PRIMARY KEY, d TEXT);
        INSERT INTO h3 VALUES(1, NULL);
        INSERT INTO h3 VALUES(2, NULL);
        INSERT INTO h3 VALUES(3, NULL);
        INSERT INTO h3 VALUES(4, '2');
        INSERT INTO h3 VALUES(5, NULL);
        INSERT INTO h3 VALUES(6, '2,3');
        INSERT INTO h3 VALUES(7, NULL);
        INSERT INTO h3 VALUES(8, '2,4');
        INSERT INTO h3 VALUES(9, '3');
    ]], {
        -- <e_select-5.1.0>

        -- </e_select-5.1.0>
    })

-- EVIDENCE-OF: R-60770-10612 One of the ALL or DISTINCT keywords may
-- follow the SELECT keyword in a simple SELECT statement.
--
test:do_select_tests(
    "e_select-5.1",
    {
        {"1", "SELECT ALL a FROM h1", {1, 1, 1, 4, 4, 4}},
        {"2", "SELECT DISTINCT a FROM h1", {1, 4}},
    })

-- EVIDENCE-OF: R-08861-34280 If the simple SELECT is a SELECT ALL, then
-- the entire set of result rows are returned by the SELECT.
--
-- EVIDENCE-OF: R-01256-01950 If neither ALL or DISTINCT are present,
-- then the behavior is as if ALL were specified.
--
-- EVIDENCE-OF: R-14442-41305 If the simple SELECT is a SELECT DISTINCT,
-- then duplicate rows are removed from the set of result rows before it
-- is returned.
--
--   The three testable statements above are tested by e_select-5.2.*,
--   5.3.* and 5.4.* respectively.
--
test:do_select_tests(
    "e_select-5",
    {
        {"3.1", "SELECT ALL x FROM h2", {"One", "Two", "Three", "Four", "one", "two", "three", "four"}},
        {"3.2", "SELECT ALL x FROM h1, h2 ON (x=b)", {"One", "one", "Four", "four"}},

        {"3.1", "SELECT x FROM h2", {"One", "Two", "Three", "Four", "one", "two", "three", "four"}},
        {"3.2", "SELECT x FROM h1, h2 ON (x=b)", {"One", "one", "Four", "four"}},

        {"4.1", "SELECT DISTINCT x FROM h2", {"One", "Two", "Three", "Four"}},
        {"4.2", "SELECT DISTINCT x FROM h1, h2 ON (x=b)", {"One", "Four"}},
    })

-- EVIDENCE-OF: R-02054-15343 For the purposes of detecting duplicate
-- rows, two NULL values are considered to be equal.
--
test:do_select_tests(
    "e_select-5.5",
    {
        {"1", "SELECT DISTINCT d FROM h3", {"","2","2,3","2,4","3"}}
    })

-- EVIDENCE-OF: R-58359-52112 The normal rules for selecting a collation
-- sequence to compare text values with apply.
--
test:do_select_tests(
    "e_select-5.6",
    {
        {"1", "SELECT DISTINCT b FROM h1", {"one", "I", "i", "four", "IV", "iv"}},
        {"2", "SELECT DISTINCT b COLLATE \"unicode_ci\" FROM h1", {"one", "I", "four", "IV"}},
        {"3", "SELECT DISTINCT x FROM h2", {"One", "Two", "Three", "Four"}},
        {"4", "SELECT DISTINCT x COLLATE \"binary\" FROM h2", {
            "One", "Two", "Three", "Four", "one", "two", "three", "four"
        }},
    })

---------------------------------------------------------------------------
-- The following tests - e_select-7.* - test that statements made to do
-- with compound SELECT statements are correct.
--
-- EVIDENCE-OF: R-39368-64333 In a compound SELECT, all the constituent
-- SELECTs must return the same number of result columns.
--
--   All the other tests in this section use compound SELECTs created
--   using component SELECTs that do return the same number of columns.
--   So the tests here just show that it is an error to attempt otherwise.
--
test:drop_all_tables()
test:do_execsql_test(
    "e_select-7.1.0",
    [[
        CREATE TABLE j1(a  INT PRIMARY KEY, b INT , c INT );
        CREATE TABLE j2(e  INT PRIMARY KEY, f INT );
        CREATE TABLE j3(g  INT PRIMARY KEY);
    ]], {
        -- <e_select-7.1.0>

        -- </e_select-7.1.0>
    })
data = {
    {"SELECT a, b FROM j1    UNION ALL SELECT g FROM j3", "UNION ALL"},
    {"SELECT *    FROM j1    UNION ALL SELECT * FROM j3", "UNION ALL"},
    {"SELECT a, b FROM j1    UNION ALL SELECT g FROM j3", "UNION ALL"},
    {"SELECT a, b FROM j1    UNION ALL SELECT * FROM j3,j2", "UNION ALL"},
    {"SELECT *    FROM j3,j2 UNION ALL SELECT a, b FROM j1", "UNION ALL"},
    {"SELECT a, b FROM j1    UNION SELECT g FROM j3", "UNION"},
    {"SELECT *    FROM j1    UNION SELECT * FROM j3", "UNION"},
    {"SELECT a, b FROM j1    UNION SELECT g FROM j3", "UNION"},
    {"SELECT a, b FROM j1    UNION SELECT * FROM j3,j2", "UNION"},
    {"SELECT *    FROM j3,j2 UNION SELECT a, b FROM j1", "UNION"},
    {"SELECT a, b FROM j1    INTERSECT SELECT g FROM j3", "INTERSECT"},
    {"SELECT *    FROM j1    INTERSECT SELECT * FROM j3", "INTERSECT"},
    {"SELECT a, b FROM j1    INTERSECT SELECT g FROM j3", "INTERSECT"},
    {"SELECT a, b FROM j1    INTERSECT SELECT * FROM j3,j2", "INTERSECT"},
    {"SELECT *    FROM j3,j2 INTERSECT SELECT a, b FROM j1", "INTERSECT"},
    {"SELECT a, b FROM j1    EXCEPT SELECT g FROM j3", "EXCEPT"},
    {"SELECT *    FROM j1    EXCEPT SELECT * FROM j3", "EXCEPT"},
    {"SELECT a, b FROM j1    EXCEPT SELECT g FROM j3", "EXCEPT"},
    {"SELECT a, b FROM j1    EXCEPT SELECT * FROM j3,j2", "EXCEPT"},
    {"SELECT *    FROM j3,j2 EXCEPT SELECT a, b FROM j1", "EXCEPT"},
}
for tn, val in ipairs(data) do
    local sql = val[1]
    local subst = val[2]
    local label = "e_select-7.1."..tn
    local error = string.format("SELECTs to the left and right of %s do not have the same number of result columns", subst)
    test:do_catchsql_test(
        label,
        sql,
        {1, error})
end

data = {
    {1, "SELECT * FROM j1 ORDER BY a UNION ALL SELECT * FROM j2,j3", "ORDER BY", "UNION ALL"},
    {2, "SELECT count(*) FROM j1 ORDER BY 1 UNION ALL SELECT max(e) FROM j2",  "ORDER BY", "UNION ALL"},
    {3, "SELECT count(*), * FROM j1 ORDER BY 1,2,3 UNION ALL SELECT *,* FROM j2",  "ORDER BY", "UNION ALL"},
    {4, "SELECT * FROM j1 LIMIT 10 UNION ALL SELECT * FROM j2,j3",   "LIMIT", "UNION ALL"},
    {5, "SELECT * FROM j1 LIMIT 10 OFFSET 5 UNION ALL SELECT * FROM j2,j3",   "LIMIT", "UNION ALL"},
    {6, "SELECT a FROM j1 LIMIT (SELECT e FROM j2) UNION ALL SELECT g FROM j2,j3",   "LIMIT", "UNION ALL"},
    {7, "SELECT * FROM j1 ORDER BY a UNION SELECT * FROM j2,j3",   "ORDER BY", "UNION"},
    {8, "SELECT count(*) FROM j1 ORDER BY 1 UNION SELECT max(e) FROM j2",  "ORDER BY", "UNION"},
    {9, "SELECT count(*), * FROM j1 ORDER BY 1,2,3 UNION SELECT *,* FROM j2",  "ORDER BY", "UNION"},
    {10, "SELECT * FROM j1 LIMIT 10 UNION SELECT * FROM j2,j3",   "LIMIT", "UNION"},
    {11, "SELECT * FROM j1 LIMIT 10 OFFSET 5 UNION SELECT * FROM j2,j3",   "LIMIT", "UNION"},
    {12, "SELECT a FROM j1 LIMIT (SELECT e FROM j2) UNION SELECT g FROM j2,j3",   "LIMIT", "UNION"},
    {13, "SELECT * FROM j1 ORDER BY a EXCEPT SELECT * FROM j2,j3",   "ORDER BY", "EXCEPT"},
    {14, "SELECT count(*) FROM j1 ORDER BY 1 EXCEPT SELECT max(e) FROM j2",  "ORDER BY", "EXCEPT"},
    {15, "SELECT count(*), * FROM j1 ORDER BY 1,2,3 EXCEPT SELECT *,* FROM j2",  "ORDER BY", "EXCEPT"},
    {16, "SELECT * FROM j1 LIMIT 10 EXCEPT SELECT * FROM j2,j3",   "LIMIT", "EXCEPT"},
    {17, "SELECT * FROM j1 LIMIT 10 OFFSET 5 EXCEPT SELECT * FROM j2,j3",   "LIMIT", "EXCEPT"},
    {18, "SELECT a FROM j1 LIMIT (SELECT e FROM j2) EXCEPT SELECT g FROM j2,j3",   "LIMIT", "EXCEPT"},
    {19, "SELECT * FROM j1 ORDER BY a INTERSECT SELECT * FROM j2,j3",   "ORDER BY", "INTERSECT"},
    {20, "SELECT count(*) FROM j1 ORDER BY 1 INTERSECT SELECT max(e) FROM j2",  "ORDER BY", "INTERSECT"},
    {21, "SELECT count(*), * FROM j1 ORDER BY 1,2,3 INTERSECT SELECT *,* FROM j2",  "ORDER BY", "INTERSECT"},
    {22, "SELECT * FROM j1 LIMIT 10 INTERSECT SELECT * FROM j2,j3",   "LIMIT", "INTERSECT"},
    {23, "SELECT * FROM j1 LIMIT 10 OFFSET 5 INTERSECT SELECT * FROM j2,j3",   "LIMIT", "INTERSECT"},
    {24, "SELECT a FROM j1 LIMIT (SELECT e FROM j2) INTERSECT SELECT g FROM j2,j3",   "LIMIT", "INTERSECT"},
}
for _, val in ipairs(data) do
    local tn = val[1]
    local select = val[2]
    local op1 = val[3]
    local op2 = val[4]
    local label = "e_select-7.2."..tn
    local error = string.format("%s clause should come after %s not before", op1, op2)
    test:do_catchsql_test(
        label,
        select,
        {1, error})
end

-- EVIDENCE-OF: R-45440-25633 ORDER BY and LIMIT clauses may only occur
-- at the end of the entire compound SELECT, and then only if the final
-- element of the compound is not a VALUES clause.

--

data = {
    {1, "SELECT * FROM j1 UNION ALL SELECT * FROM j2,j3 ORDER BY a"},
    {2, "SELECT count(*) FROM j1 UNION ALL SELECT max(e) FROM j2 ORDER BY 1"},
    {3, "SELECT count(*), * FROM j1 UNION ALL SELECT *,* FROM j2 ORDER BY 1,2,3"},
    {4, "SELECT * FROM j1 UNION ALL SELECT * FROM j2,j3 LIMIT 10"},
    {5, "SELECT * FROM j1 UNION ALL SELECT * FROM j2,j3 LIMIT 10 OFFSET 5" },
    {6, "SELECT a FROM j1 UNION ALL SELECT g FROM j2,j3 LIMIT (SELECT 10)"},
    {7, "SELECT * FROM j1 UNION SELECT * FROM j2,j3 ORDER BY a"},
    {8, "SELECT count(*) FROM j1 UNION SELECT max(e) FROM j2 ORDER BY 1"},
    {"8b", "VALUES('8b') UNION SELECT max(e) FROM j2 ORDER BY 1"},
    {9, "SELECT count(*), * FROM j1 UNION SELECT *,* FROM j2 ORDER BY 1,2,3"},
    {10, "SELECT * FROM j1 UNION SELECT * FROM j2,j3 LIMIT 10"},
    {11, "SELECT * FROM j1 UNION SELECT * FROM j2,j3 LIMIT 10 OFFSET 5"},
    {12, "SELECT a FROM j1 UNION SELECT g FROM j2,j3 LIMIT (SELECT 10)"},
    {13, "SELECT * FROM j1 EXCEPT SELECT * FROM j2,j3 ORDER BY a"},
    {14, "SELECT count(*) FROM j1 EXCEPT SELECT max(e) FROM j2 ORDER BY 1"},
    {15, "SELECT count(*), * FROM j1 EXCEPT SELECT *,* FROM j2 ORDER BY 1,2,3"},
    {16, "SELECT * FROM j1 EXCEPT SELECT * FROM j2,j3 LIMIT 10"},
    {17, "SELECT * FROM j1 EXCEPT SELECT * FROM j2,j3 LIMIT 10 OFFSET 5"},
    {18, "SELECT a FROM j1 EXCEPT SELECT g FROM j2,j3 LIMIT (SELECT 10)"},
    {19, "SELECT * FROM j1 INTERSECT SELECT * FROM j2,j3 ORDER BY a"},
    {20, "SELECT count(*) FROM j1 INTERSECT SELECT max(e) FROM j2 ORDER BY 1"},
    {21, "SELECT count(*), * FROM j1 INTERSECT SELECT *,* FROM j2 ORDER BY 1,2,3"},
    {22, "SELECT * FROM j1 INTERSECT SELECT * FROM j2,j3 LIMIT 10"},
    {23, "SELECT * FROM j1 INTERSECT SELECT * FROM j2,j3 LIMIT 10 OFFSET 5"},
    {24, "SELECT a FROM j1 INTERSECT SELECT g FROM j2,j3 LIMIT (SELECT 10)"}
}
for _, val in ipairs(data) do
    local tn = val[1]
    local select = val[2]
    test:do_test(
        "e_select-7.3."..tn,
        function()
            return pcall (function () test:execsql(select) end)
        end, true)

end
for _, val in ipairs({
    {50, "SELECT * FROM j1 ORDER BY 1 UNION ALL SELECT * FROM j2,j3", },
    {51, "SELECT * FROM j1 LIMIT 1 UNION ALL SELECT * FROM j2,j3", },
    {52, "SELECT count(*) FROM j1 UNION ALL VALUES(11) ORDER BY 1", },
    {53, "SELECT count(*) FROM j1 UNION ALL VALUES(11) LIMIT 1"}}) do
    local tn = val[1]
    local select = val[2]
    test:do_test(
        "e_select-7.3."..tn,
        function()
            return pcall (function () test:execsql(select) end)
        end, false)
end

-- EVIDENCE-OF: R-08531-36543 A compound SELECT created using UNION ALL
-- operator returns all the rows from the SELECT to the left of the UNION
-- ALL operator, and all the rows from the SELECT to the right of it.
--
test:drop_all_tables()
test:do_execsql_test(
    "e_select-7.4.0",
    [[
        CREATE TABLE q1(id  INT primary key, a TEXT, b NUMBER, c NUMBER);
        CREATE TABLE q2(id  INT primary key, d TEXT, e NUMBER);
        CREATE TABLE q3(id  INT primary key, f TEXT, g INT);

        INSERT INTO q1 VALUES(1, '16', -87.66, NULL);
        INSERT INTO q1 VALUES(2, 'legible', 94, -42.47);
        INSERT INTO q1 VALUES(3, 'beauty', 36, NULL);

        INSERT INTO q2 VALUES(1, 'legible', 1);
        INSERT INTO q2 VALUES(2, 'beauty', 2);
        INSERT INTO q2 VALUES(3, '-65', 4);
        INSERT INTO q2 VALUES(4, 'emanating', -16.56);

        INSERT INTO q3 VALUES(1, 'beauty', 2);
        INSERT INTO q3 VALUES(2, 'beauty', 2);
    ]], {
        -- <e_select-7.4.0>

        -- </e_select-7.4.0>
    })

test:do_select_tests(
    "e_select-7.4",
    {
        {1, "SELECT a FROM q1 UNION ALL SELECT d FROM q2", {"16", "legible", "beauty", "legible", "beauty", "-65", "emanating"}},
        {"3", "SELECT count(*) FROM q1 UNION ALL SELECT min(e) FROM q2", {3, -16.56}},
        {"4", "SELECT d,e FROM q2 UNION ALL SELECT f,g FROM q3", {"legible" , 1, "beauty", 2, "-65", 4, "emanating", -16.56, "beauty", 2, "beauty", 2}},
        })

-- EVIDENCE-OF: R-20560-39162 The UNION operator works the same way as
-- UNION ALL, except that duplicate rows are removed from the final
-- result set.
--
test:do_select_tests(
    "e_select-7.5",
    {
        {1, "SELECT a FROM q1 UNION SELECT d FROM q2",
            {"-65", "16", "beauty", "emanating", "legible"}},

        {3, "SELECT count(*) FROM q1 UNION SELECT min(e) FROM q2",
            {-16.56, 3}},

        {4, "SELECT d,e FROM q2 UNION SELECT f,g FROM q3",
            {"-65", 4, "beauty", 2, "emanating", -16.56, "legible", 1}}
    })

-- EVIDENCE-OF: R-45764-31737 The INTERSECT operator returns the
-- intersection of the results of the left and right SELECTs.
--
test:do_select_tests(
    "e_select-7.6",
    {
        {"1", "SELECT a FROM q1 INTERSECT SELECT d FROM q2", {"beauty" , "legible"}},
        {"2", "SELECT d,e FROM q2 INTERSECT SELECT f,g FROM q3", {"beauty" , 2}},
    })

-- EVIDENCE-OF: R-25787-28949 The EXCEPT operator returns the subset of
-- rows returned by the left SELECT that are not also returned by the
-- right-hand SELECT.
--
test:do_select_tests(
    "e_select-7.7",
    {
        {"1", "SELECT a FROM q1 EXCEPT SELECT d FROM q2", {"16"}},
        {"2", "SELECT d,e FROM q2 EXCEPT SELECT f,g FROM q3", {"-65", 4, "emanating", -16.56, "legible", 1}},
    })

-- EVIDENCE-OF: R-40729-56447 Duplicate rows are removed from the results
-- of INTERSECT and EXCEPT operators before the result set is returned.
--
test:do_select_tests(
    "e_select-7.8",
    {
        {"0", "SELECT f,g FROM q3", {"beauty" , 2, "beauty", 2}},
        {"1", "SELECT f,g FROM q3 INTERSECT SELECT f,g FROM q3", {"beauty", 2}},
        {"2", "SELECT f,g FROM q3 EXCEPT SELECT a,b FROM q1", {"beauty", 2}},
    })

-- EVIDENCE-OF: R-46765-43362 For the purposes of determining duplicate
-- rows for the results of compound SELECT operators, NULL values are
-- considered equal to other NULL values and distinct from all non-NULL
-- values.
--
-- MUST_WORK_TEST
if 0>0 then
-- Legacy from the original code. Must be replaced with analogue
-- functions and valid values.
local db = nil
local null = nil
db("nullvalue", "null")
test:do_select_tests(
    "e_select-7.9",
    {
        {"1", "SELECT NULL UNION ALL SELECT NULL", {null, null}},
        {"2", "SELECT NULL UNION     SELECT NULL", {null}},
        {"3", "SELECT NULL INTERSECT SELECT NULL", {null}},
        {"4", "SELECT NULL EXCEPT    SELECT NULL", {}},
        {"5", "SELECT NULL UNION ALL SELECT 'ab'", {null, "ab"}},
        {"6", "SELECT NULL UNION     SELECT 'ab'", {null, "ab"}},
        {"7", "SELECT NULL INTERSECT SELECT 'ab'", {}},
        {"8", "SELECT NULL EXCEPT    SELECT 'ab'", {null}},
        {"9", "SELECT NULL UNION ALL SELECT 0", {null, 0}},
        {"10", "SELECT NULL UNION     SELECT 0", {null, 0}},
        {"11", "SELECT NULL INTERSECT SELECT 0", {}},
        {"12", "SELECT NULL EXCEPT    SELECT 0", {null}},
        {"13", "SELECT c FROM q1 UNION ALL SELECT g FROM q3", {null, -42.47, "null", 2, 2}},
        {"14", "SELECT c FROM q1 UNION     SELECT g FROM q3", {null, -42.47, 2}},
        {"15", "SELECT c FROM q1 INTERSECT SELECT g FROM q3", {}},
        {"16", "SELECT c FROM q1 EXCEPT    SELECT g FROM q3", {null, -42.47}},
    })

db("nullvalue", "")
end
-- EVIDENCE-OF: R-51232-50224 The collation sequence used to compare two
-- text values is determined as if the columns of the left and right-hand
-- SELECT statements were the left and right-hand operands of the equals
-- (=) operator, except that greater precedence is not assigned to a
-- collation sequence specified with the postfix COLLATE operator.
--
test:drop_all_tables()
test:do_execsql_test(
    "e_select-7.10.0",
    [[
        CREATE TABLE y1(a  TEXT COLLATE "unicode_ci" PRIMARY KEY, b  TEXT COLLATE "binary", c TEXT );
        INSERT INTO y1 VALUES('Abc', 'abc', 'aBC');
    ]], {
        -- <e_select-7.10.0>

        -- </e_select-7.10.0>
    })

test:do_select_tests(
    "e_select-7.10",
    {
        {"1", "SELECT 'abc'                UNION SELECT 'ABC'", {"ABC",  "abc"}},
        {"2", "SELECT 'abc' COLLATE \"unicode_ci\" UNION SELECT 'ABC'", {"ABC" }},
        {"3", "SELECT 'abc'                UNION SELECT 'ABC' COLLATE \"unicode_ci\"", {"ABC" }},
        {"4", "SELECT 'abc' COLLATE \"binary\" UNION SELECT 'ABC'", {"ABC",  "abc"}},
        {"5", "SELECT 'abc' COLLATE \"unicode_ci\" UNION SELECT 'ABC'", {"ABC" }},
        {"6", "SELECT a FROM y1 UNION SELECT c FROM y1", {"aBC" }},
        {"7", "SELECT a FROM y1 UNION SELECT c COLLATE \"binary\" FROM y1", {"Abc", "aBC" }},
    })

-- EVIDENCE-OF: R-32706-07403 No affinity transformations are applied to
-- any values when comparing rows as part of a compound SELECT.
--
test:drop_all_tables()
test:do_execsql_test(
    "e_select-7.10.0",
    [[
        CREATE TABLE w1(a TEXT PRIMARY KEY, b NUMBER);
        CREATE TABLE w2(a  INT PRIMARY KEY, b TEXT);

        INSERT INTO w1 VALUES('1', 4.1);
        INSERT INTO w2 VALUES(1, '4.1');
    ]], {
        -- <e_select-7.10.0>

        -- </e_select-7.10.0>
    })

test:do_select_tests(
    "e_select-7.11",
    {
        {"1", "SELECT a FROM w1 UNION SELECT a FROM w2 ", {1, "1"}},
        {"2", "SELECT a FROM w2 UNION SELECT a FROM w1 ", {1, "1"}},
        {"3", "SELECT b FROM w1 UNION SELECT b FROM w2 ", {4.1, "4.1"}},
        {"4", "SELECT b FROM w2 UNION SELECT b FROM w1 ", {4.1, "4.1"}},
        {"5", "SELECT a FROM w1 INTERSECT SELECT a FROM w2 ", {}},
        {"6", "SELECT a FROM w2 INTERSECT SELECT a FROM w1 ", {}},
        {"7", "SELECT b FROM w1 INTERSECT SELECT b FROM w2 ", {}},
        {"8", "SELECT b FROM w2 INTERSECT SELECT b FROM w1 ", {}},
        {"9", "SELECT a FROM w1 EXCEPT SELECT a FROM w2 ", {"1"}},
        {"10", "SELECT a FROM w2 EXCEPT SELECT a FROM w1 ", {1}},
        {"11", "SELECT b FROM w1 EXCEPT SELECT b FROM w2 ", {4.1}},
        {"12", "SELECT b FROM w2 EXCEPT SELECT b FROM w1 ", {"4.1"}},
    })

-- EVIDENCE-OF: R-32562-20566 When three or more simple SELECTs are
-- connected into a compound SELECT, they group from left to right. In
-- other words, if "A", "B" and "C" are all simple SELECT statements, (A
-- op B op C) is processed as ((A op B) op C).
--
--   e_select-7.12.1: Precedence of UNION vs. INTERSECT
--   e_select-7.12.2: Precedence of UNION vs. UNION ALL
--   e_select-7.12.3: Precedence of UNION vs. EXCEPT
--   e_select-7.12.4: Precedence of INTERSECT vs. UNION ALL
--   e_select-7.12.5: Precedence of INTERSECT vs. EXCEPT
--   e_select-7.12.6: Precedence of UNION ALL vs. EXCEPT
--   e_select-7.12.7: Check that "a EXCEPT b EXCEPT c" is processed as
--                   "(a EXCEPT b) EXCEPT c".
--
-- The INTERSECT and EXCEPT operations are mutually commutative. So
-- the e_select-7.12.5 test cases do not prove very much.
--
test:drop_all_tables()
test:do_execsql_test(
    "e_select-7.12.0",
    [[
        CREATE TABLE t1(x  INT PRIMARY KEY);
        INSERT INTO t1 VALUES(1);
        INSERT INTO t1 VALUES(2);
        INSERT INTO t1 VALUES(3);
    ]], {
        -- <e_select-7.12.0>

        -- </e_select-7.12.0>
    })
for _, val in ipairs({
    {"1a", "(1,2) INTERSECT (1)   UNION     (3)",    {1, 3}},
    {"1b", "(3)   UNION     (1,2) INTERSECT (1)",    {1}},
    {"2a", "(1,2) UNION     (3)   UNION ALL (1)",    {1, 2, 3, 1}},
    {"2b", "(1)   UNION ALL (3)   UNION     (1,2)",  {1, 2, 3}},
    {"3a", "(1,2) UNION     (3)   EXCEPT    (1)",    {2, 3}},
    {"3b", "(1,2) EXCEPT    (3)   UNION     (1)",    {1, 2}},
    {"4a", "(1,2) INTERSECT (1)   UNION ALL (3)",    {1, 3}},
    {"4b", "(3)   UNION     (1,2) INTERSECT (1)",    {1}},
    {"5a", "(1,2) INTERSECT (2)   EXCEPT    (2)",    {}},
    {"5b", "(2,3) EXCEPT    (2)   INTERSECT (2)",    {}},
    {"6a", "(2)   UNION ALL (2)   EXCEPT    (2)",    {}},
    {"6b", "(2)   EXCEPT    (2)   UNION ALL (2)",    {2}},
    {7, "(2,3) EXCEPT    (2)   EXCEPT    (3)",    {}}, }) do
    local tn = val[1]
    local select = val[2]
    local res = val[3]
    select = string.gsub(select, "%(", "SELECT x FROM t1 WHERE x IN (")
    test:do_execsql_test(
        "e_select-7.12."..tn,
        select, res)

end
---------------------------------------------------------------------------
-- ORDER BY clauses
--
test:drop_all_tables()
test:do_execsql_test(
    "e_select-8.1.0",
    [[
        CREATE TABLE d1(id  INT primary key, x INT , y INT , z INT );

        INSERT INTO d1 VALUES(1, 1, 2, 3);
        INSERT INTO d1 VALUES(2, 2, 5, -1);
        INSERT INTO d1 VALUES(3, 1, 2, 8);
        INSERT INTO d1 VALUES(4, 1, 2, 7);
        INSERT INTO d1 VALUES(5, 2, 4, 93);
        INSERT INTO d1 VALUES(6, 1, 2, -20);
        INSERT INTO d1 VALUES(7, 1, 4, 93);
        INSERT INTO d1 VALUES(8, 1, 5, -1);

        CREATE TABLE d2(id  INT primary key, a TEXT, b TEXT);
        INSERT INTO d2 VALUES(1, 'gently', 'failings');
        INSERT INTO d2 VALUES(2, 'commercials', 'bathrobe');
        INSERT INTO d2 VALUES(3, 'iterate', 'sexton');
        INSERT INTO d2 VALUES(4, 'babied', 'charitableness');
        INSERT INTO d2 VALUES(5, 'solemnness', 'annexed');
        INSERT INTO d2 VALUES(6, 'rejoicing', 'liabilities');
        INSERT INTO d2 VALUES(7, 'pragmatist', 'guarded');
        INSERT INTO d2 VALUES(8, 'barked', 'interrupted');
        INSERT INTO d2 VALUES(9, 'reemphasizes', 'reply');
        INSERT INTO d2 VALUES(10, 'lad', 'relenting');
    ]], {
        -- <e_select-8.1.0>

        -- </e_select-8.1.0>
    })

-- EVIDENCE-OF: R-44988-41064 Rows are first sorted based on the results
-- of evaluating the left-most expression in the ORDER BY list, then ties
-- are broken by evaluating the second left-most expression and so on.
--
test:do_select_tests(
    "e_select-8.1",
    {
        {"1", "SELECT x,y,z FROM d1 ORDER BY x, y, z", {  1, 2, -20, 1, 2, 3, 1, 2, 7, 1, 2, 8, 1, 4, 93, 1, 5, -1, 2, 4, 93, 2, 5, -1}},
    })

-- EVIDENCE-OF: R-06617-54588 Each ORDER BY expression may be optionally
-- followed by one of the keywords ASC (smaller values are returned
-- first) or DESC (larger values are returned first).
--
--   Test cases e_select-8.2.* test the above.
--
-- EVIDENCE-OF: R-18705-33393 If neither ASC or DESC are specified, rows
-- are sorted in ascending (smaller values first) order by default.
--
--   Test cases e_select-8.3.* test the above. All 8.3 test cases are
--   copies of 8.2 test cases with the explicit "ASC" removed.
--
test:do_select_tests(
    "e_select-8",
    {
        {"2.1", "SELECT x,y,z FROM d1 ORDER BY x ASC, y ASC, z ASC", {
            1,2,-20,1,2,3,1,2,7,1,2,8,1,4,93,1,5,-1,2,4,93,2,5,-1
        }},
        {"2.2", "SELECT x,y,z FROM d1 ORDER BY x DESC, y DESC, z DESC", {
            2,5,-1,2,4,93,1,5,-1,1,4,93,1,2,8,1,2,7,1,2,3,1,2,-20
        }},
        {"2.3", "SELECT x,y,z FROM d1 ORDER BY x DESC, y ASC, z DESC", {
            2,4,93,2,5,-1,1,2,8,1,2,7,1,2,3,1,2,-20,1,4,93,1,5,-1
        }},
        {"2.4", "SELECT x,y,z FROM d1 ORDER BY x DESC, y ASC, z ASC", {
            2,4,93,2,5,-1,1,2,-20,1,2,3,1,2,7,1,2,8,1,4,93,1,5,-1
        }},

        {"3.1", "SELECT x,y,z FROM d1 ORDER BY x, y, z", {
            1,2,-20,1,2,3,1,2,7,1,2,8,1,4,93,1,5,-1,2,4,93,2,5,-1
        }},
        {"3.3", "SELECT x,y,z FROM d1 ORDER BY x DESC, y, z DESC", {
            2,4,93,2,5,-1,1,2,8,1,2,7,1,2,3,1,2,-20,1,4,93,1,5,-1
        }},
        {"3.4", "SELECT x,y,z FROM d1 ORDER BY x DESC, y, z", {
            2,4,93,2,5,-1,1,2,-20,1,2,3,1,2,7,1,2,8,1,4,93,1,5,-1
        }},
    })

-- EVIDENCE-OF: R-29779-04281 If the ORDER BY expression is a constant
-- integer K then the expression is considered an alias for the K-th
-- column of the result set (columns are numbered from left to right
-- starting with 1).
--
test:do_select_tests(
    "e_select-8.4",
    {
        {"1", "SELECT x,y,z FROM d1 ORDER BY 1 ASC, 2 ASC, 3 ASC", {
            1,2,-20,1,2,3,1,2,7,1,2,8,1,4,93,1,5,-1,2,4,93,2,5,-1
        }},
        {"2", "SELECT x,y,z FROM d1 ORDER BY 1 DESC, 2 DESC, 3 DESC", {
            2,5,-1,2,4,93,1,5,-1,1,4,93,1,2,8,1,2,7,1,2,3,1,2,-20
        }},
        {"3", "SELECT x,y,z FROM d1 ORDER BY 1 DESC, 2 ASC, 3 DESC", {
            2,4,93,2,5,-1,1,2,8,1,2,7,1,2,3,1,2,-20,1,4,93,1,5,-1
        }},
        {"4", "SELECT x,y,z FROM d1 ORDER BY 1 DESC, 2 ASC, 3 ASC", {
            2,4,93,2,5,-1,1,2,-20,1,2,3,1,2,7,1,2,8,1,4,93,1,5,-1
        }},
        {"5", "SELECT x,y,z FROM d1 ORDER BY 1, 2, 3", {
            1,2,-20,1,2,3,1,2,7,1,2,8,1,4,93,1,5,-1,2,4,93,2,5,-1
        }},
        {"6", "SELECT x,y,z FROM d1 ORDER BY 1 DESC, 2, 3 DESC", {
            2,4,93,2,5,-1,1,2,8,1,2,7,1,2,3,1,2,-20,1,4,93,1,5,-1
        }},
        {"7", "SELECT x,y,z FROM d1 ORDER BY 1 DESC, 2, 3", {
            2,4,93,2,5,-1,1,2,-20,1,2,3,1,2,7,1,2,8,1,4,93,1,5,-1
        }},
        {"8", "SELECT z, x FROM d1 ORDER BY 2", {
            3,1,8,1,7,1,-20,1,93,1,-1,1,-1,2,93,2
        }},
        {"9", "SELECT z, x FROM d1 ORDER BY 1", {
            -20,1,-1,2,-1,1,3,1,7,1,8,1,93,2,93,1
        }},
    })

-- EVIDENCE-OF: R-63286-51977 If the ORDER BY expression is an identifier
-- that corresponds to the alias of one of the output columns, then the
-- expression is considered an alias for that column.
--
test:do_select_tests(
    "e_select-8.5",
    {
        {"1", "SELECT z+1 AS abc FROM d1 ORDER BY abc", {
            -19, 0, 0, 4, 8, 9, 94, 94
        }},
        {"2", "SELECT z+1 AS abc FROM d1 ORDER BY abc DESC", {
            94, 94, 9, 8, 4, 0, 0, -19
        }},
        {"3", "SELECT z AS x, x AS z FROM d1 ORDER BY z", {
            3,1,8,1,7,1,-20,1,93,1,-1,1,-1,2,93,2
        }},
        {"4", "SELECT z AS x, x AS z FROM d1 ORDER BY x", {
            -20,1,-1,2,-1,1,3,1,7,1,8,1,93,2,93,1
        }},
    })

-- EVIDENCE-OF: R-65068-27207 Otherwise, if the ORDER BY expression is
-- any other expression, it is evaluated and the returned value used to
-- order the output rows.
--
-- EVIDENCE-OF: R-03421-57988 If the SELECT statement is a simple SELECT,
-- then an ORDER BY may contain any arbitrary expressions.
--
test:do_select_tests(
    "e_select-8.6",
    {
        {"1", "SELECT x,y,z FROM d1 ORDER BY x+y+z", {
            1,2,-20,1,5,-1,1,2,3,2,5,-1,1,2,7,1,2,8,1,4,93,2,4,93
        }},
        {"2", "SELECT x,y,z FROM d1 ORDER BY x*z", {
            1,2,-20,2,5,-1,1,5,-1,1,2,3,1,2,7,1,2,8,1,4,93,2,4,93
        }},
        {"3", "SELECT x,y,z FROM d1 ORDER BY y*z", {
            1,2,-20,2,5,-1,1,5,-1,1,2,3,1,2,7,1,2,8,2,4,93,1,4,93
        }},
    })

-- EVIDENCE-OF: R-28853-08147 However, if the SELECT is a compound
-- SELECT, then ORDER BY expressions that are not aliases to output
-- columns must be exactly the same as an expression used as an output
-- column.
--

test:do_catchsql_test(
    "e_select-8.7.1.1",
    "SELECT x FROM d1 UNION ALL SELECT a FROM d2 ORDER BY x*z",
    {
        1, "Error at ORDER BY in place 1: term does not match any column in the result set"})

test:do_catchsql_test(
    "e_select-8.7.1.2",
    "SELECT x,z FROM d1 UNION ALL SELECT a,b FROM d2 ORDER BY x, x/z",
    {
        1, "Error at ORDER BY in place 2: term does not match any column in the result set"})

test:do_select_tests(
    "e_select-8.7.2",
    {
        {"1", "SELECT x*z FROM d1 UNION ALL SELECT a FROM d2 ORDER BY x*z", {
            -20,-2,-1,3,7,8,93,186,"babied","barked","commercials","gently","iterate","lad","pragmatist","reemphasizes","rejoicing","solemnness"
        }},
        {"2", "SELECT x, x/z FROM d1 UNION ALL SELECT a,b FROM d2 ORDER BY x, x/z", {
            1,-1,1,0,1,0,1,0,1,0,1,0,2,-2,2,0,"babied","charitableness","barked","interrupted","commercials","bathrobe","gently","failings","iterate","sexton","lad","relenting","pragmatist","guarded","reemphasizes","reply","rejoicing","liabilities","solemnness","annexed"
        }},
    })

test:do_execsql_test(
    "e_select-8.8.0",
    [[
        CREATE TABLE d3(id  INT primary key, a NUMBER);
        INSERT INTO d3 VALUES(1, 0);
        INSERT INTO d3 VALUES(2, 14.1);
        INSERT INTO d3 VALUES(3, 13);
        INSERT INTO d3 VALUES(4, 78787878);
        INSERT INTO d3 VALUES(5, 15);
        INSERT INTO d3 VALUES(6, 12.9);
        INSERT INTO d3 VALUES(7, null);

        CREATE TABLE d4(id  INT primary key, x  TEXT COLLATE "unicode_ci");
        INSERT INTO d4 VALUES(1, 'abc');
        INSERT INTO d4 VALUES(2, 'ghi');
        INSERT INTO d4 VALUES(3, 'DEF');
        INSERT INTO d4 VALUES(4, 'JKL');
    ]], {
        -- <e_select-8.8.0>

        -- </e_select-8.8.0>
    })

-- EVIDENCE-OF: R-10883-17697 For the purposes of sorting rows, values
-- are compared in the same way as for comparison expressions.
--
--   The following tests verify that values of different types are sorted
--   correctly, and that mixed real and integer values are compared properly.
--
test:do_execsql_test(
    "e_select-8.8.1",
    [[
        SELECT a FROM d3 ORDER BY a
    ]], {
        -- <e_select-8.8.1>
        "", 0, 12.9, 13, 14.1, 15, 78787878
        -- </e_select-8.8.1>
    })

test:do_execsql_test(
    "e_select-8.8.2",
    [[
        SELECT a FROM d3 ORDER BY a DESC
    ]], {
        -- <e_select-8.8.2>
        78787878, 15, 14.1, 13, 12.9, 0, ""
        -- </e_select-8.8.2>
    })

-- EVIDENCE-OF: R-64199-22471 If the ORDER BY expression is assigned a
-- collation sequence using the postfix COLLATE operator, then the
-- specified collation sequence is used.
--
test:do_execsql_test(
    "e_select-8.9.1",
    [[
        SELECT x FROM d4 ORDER BY 1 COLLATE "binary"
    ]], {
        -- <e_select-8.9.1>
        "DEF", "JKL", "abc", "ghi"
        -- </e_select-8.9.1>
    })

test:do_execsql_test(
    "e_select-8.9.2",
    [[
        SELECT x COLLATE "binary" FROM d4 ORDER BY 1 COLLATE "unicode_ci"
    ]], {
        -- <e_select-8.9.2>
        "abc", "DEF", "ghi", "JKL"
        -- </e_select-8.9.2>
    })

-- EVIDENCE-OF: R-09398-26102 Otherwise, if the ORDER BY expression is
-- an alias to an expression that has been assigned a collation sequence
-- using the postfix COLLATE operator, then the collation sequence
-- assigned to the aliased expression is used.
--
--   In the test 8.10.2, the only result-column expression has no alias. So the
--   ORDER BY expression is not a reference to it and therefore does not inherit
--   the collation sequence. In test 8.10.3, "x" is the alias (as well as the
--   column name), so the ORDER BY expression is interpreted as an alias and the
--   collation sequence attached to the result column is used for sorting.
--
test:do_execsql_test(
    "e_select-8.10.1",
    [[
        SELECT x COLLATE "binary" FROM d4 ORDER BY 1
    ]], {
        -- <e_select-8.10.1>
        "DEF", "JKL", "abc", "ghi"
        -- </e_select-8.10.1>
    })

test:do_execsql_test(
    "e_select-8.10.2",
    [[
        SELECT x COLLATE "binary" FROM d4 ORDER BY x
    ]], {
        -- <e_select-8.10.2>
        "abc", "DEF", "ghi", "JKL"
        -- </e_select-8.10.2>
    })

test:do_execsql_test(
    "e_select-8.10.3",
    [[
        SELECT x COLLATE "binary" AS x FROM d4 ORDER BY x
    ]], {
        -- <e_select-8.10.3>
        "DEF", "JKL", "abc", "ghi"
        -- </e_select-8.10.3>
    })

-- EVIDENCE-OF: R-27301-09658 Otherwise, if the ORDER BY expression is a
-- column or an alias of an expression that is a column, then the default
-- collation sequence for the column is used.
--
test:do_execsql_test(
    "e_select-8.11.1",
    [[
        SELECT x AS y FROM d4 ORDER BY y
    ]], {
        -- <e_select-8.11.1>
        "abc", "DEF", "ghi", "JKL"
        -- </e_select-8.11.1>
    })

test:do_execsql_test(
    "e_select-8.11.2",
    [[
        SELECT x||'' FROM d4 ORDER BY x
    ]], {
        -- <e_select-8.11.2>
        "abc", "DEF", "ghi", "JKL"
        -- </e_select-8.11.2>
    })

-- EVIDENCE-OF: R-49925-55905 Otherwise, the BINARY collation sequence is
-- used.
--
test:do_execsql_test(
    "e_select-8.12.1",
    [[
        SELECT x FROM d4 ORDER BY x||''
    ]], {
        -- <e_select-8.12.1>
        "DEF", "JKL", "abc", "ghi"
        -- </e_select-8.12.1>
    })

-- EVIDENCE-OF: R-44130-32593 If an ORDER BY expression is not an integer
-- alias, then sql searches the left-most SELECT in the compound for a
-- result column that matches either the second or third rules above. If
-- a match is found, the search stops and the expression is handled as an
-- alias for the result column that it has been matched against.
-- Otherwise, the next SELECT to the right is tried, and so on.
--
test:do_execsql_test(
    "e_select-8.13.0",
    [[
        CREATE TABLE d5(a  INT PRIMARY KEY, b TEXT);
        CREATE TABLE d6(c  INT PRIMARY KEY, d TEXT);
        CREATE TABLE d7(e  INT PRIMARY KEY, f TEXT);

        INSERT INTO d5 VALUES(1, 'f');
        INSERT INTO d6 VALUES(2, 'e');
        INSERT INTO d7 VALUES(3, 'd');
        INSERT INTO d5 VALUES(4, 'c');
        INSERT INTO d6 VALUES(5, 'b');
        INSERT INTO d7 VALUES(6, 'a');

        CREATE TABLE d8(x  TEXT COLLATE "unicode_ci" PRIMARY KEY);
        CREATE TABLE d9(y  TEXT COLLATE "unicode_ci" PRIMARY KEY);

        INSERT INTO d8 VALUES('a');
        INSERT INTO d9 VALUES('B');
        INSERT INTO d8 VALUES('c');
        INSERT INTO d9 VALUES('D');
    ]], {
        -- <e_select-8.13.0>

        -- </e_select-8.13.0>
    })

test:do_select_tests(
    "e_select-8.13",
    {
        {"1", "SELECT a FROM d5 UNION ALL SELECT c FROM d6 UNION ALL SELECT e FROM d7 ORDER BY a", {1, 2, 3, 4, 5, 6}},
        {"2", "SELECT a FROM d5 UNION ALL SELECT c FROM d6 UNION ALL SELECT e FROM d7 ORDER BY 1", {1, 2, 3, 4, 5, 6}},
        {"3", "SELECT a, b FROM d5 UNION ALL SELECT b, a FROM d5 ORDER BY b ", {"f",  1,   "c", 4,   4, "c",   1, "f"}},
        {"4", "SELECT a, b FROM d5 UNION ALL SELECT b, a FROM d5 ORDER BY 2 ", {"f",  1,   "c", 4,   4, "c",   1, "f"}},
        {"5", "SELECT a, b FROM d5 UNION ALL SELECT b, a FROM d5 ORDER BY a ", {1, "f",   4, "c",   "c", 4,   "f", 1}},
        {"6", "SELECT a, b FROM d5 UNION ALL SELECT b, a FROM d5 ORDER BY 1 ", {1, "f",   4, "c",   "c", 4,   "f", 1}},
        {"7", "SELECT a, b FROM d5 UNION ALL SELECT b, a+1 FROM d5 ORDER BY a+1 ", {"f",  2,   "c", 5,   4, "c",   1, "f"}},
        {"8", "SELECT a, b FROM d5 UNION ALL SELECT b, a+1 FROM d5 ORDER BY 2 ", {"f",  2,   "c", 5,   4, "c",   1, "f"}},
        {"9", "SELECT a+1, b FROM d5 UNION ALL SELECT b, a+1 FROM d5 ORDER BY a+1 ", {2, "f",   5, "c",   "c", 5,   "f", 2}},
        {"10", "SELECT a+1, b FROM d5 UNION ALL SELECT b, a+1 FROM d5 ORDER BY 1 ", {2, "f",   5, "c",   "c", 5,   "f", 2}},
    })

-- EVIDENCE-OF: R-39265-04070 If no matching expression can be found in
-- the result columns of any constituent SELECT, it is an error.
--
for _, val in ipairs({{1, "SELECT a FROM d5 UNION SELECT c FROM d6 ORDER BY a+1", "1"},
    {2, "SELECT a FROM d5 UNION SELECT c FROM d6 ORDER BY a, a+1", "2"},
    {3, "SELECT * FROM d5 INTERSECT SELECT * FROM d6 ORDER BY \"hello\"", "1"},
    {4, "SELECT * FROM d5 INTERSECT SELECT * FROM d6 ORDER BY blah", "1"},
    {5, "SELECT * FROM d5 INTERSECT SELECT * FROM d6 ORDER BY c,d,c+d", "3"},
    {6, "SELECT * FROM d5 EXCEPT SELECT * FROM d7 ORDER BY 1,2,b,a/b", "4"}}) do
    local tn = val[1]
    local select = val[2]
    local err_param = val[3]
    test:do_catchsql_test(
        "e_select-8.14."..tn,
        select,
        {
            1, string.format("Error at ORDER BY in place %s: term does not match any column in the result set", err_param)})
end
-- EVIDENCE-OF: R-03407-11483 Each term of the ORDER BY clause is
-- processed separately and may be matched against result columns from
-- different SELECT statements in the compound.
--
test:do_select_tests(
    "e_select-8.15",
    {
        {"1", "SELECT a, b FROM d5 UNION ALL SELECT c-1, d FROM d6 ORDER BY 1, 2 ", {1, "e",   1, "f",   4, "b",   4, "c"}},
    })

---------------------------------------------------------------------------
-- Tests related to statements made about the LIMIT/OFFSET clause.
--
test:do_execsql_test(
    "e_select-9.0",
    [[
        CREATE TABLE f1(id  INT primary key, a INT, b TEXT);
        INSERT INTO f1 VALUES(1, 26, 'z');
        INSERT INTO f1 VALUES(2, 25, 'y');
        INSERT INTO f1 VALUES(3, 24, 'x');
        INSERT INTO f1 VALUES(4, 23, 'w');
        INSERT INTO f1 VALUES(5, 22, 'v');
        INSERT INTO f1 VALUES(6, 21, 'u');
        INSERT INTO f1 VALUES(7, 20, 't');
        INSERT INTO f1 VALUES(8, 19, 's');
        INSERT INTO f1 VALUES(9, 18, 'r');
        INSERT INTO f1 VALUES(10, 17, 'q');
        INSERT INTO f1 VALUES(11, 16, 'p');
        INSERT INTO f1 VALUES(12, 15, 'o');
        INSERT INTO f1 VALUES(13, 14, 'n');
        INSERT INTO f1 VALUES(14, 13, 'm');
        INSERT INTO f1 VALUES(15, 12, 'l');
        INSERT INTO f1 VALUES(16, 11, 'k');
        INSERT INTO f1 VALUES(17, 10, 'j');
        INSERT INTO f1 VALUES(18, 9, 'i');
        INSERT INTO f1 VALUES(19, 8, 'h');
        INSERT INTO f1 VALUES(20, 7, 'g');
        INSERT INTO f1 VALUES(21, 6, 'f');
        INSERT INTO f1 VALUES(22, 5, 'e');
        INSERT INTO f1 VALUES(23, 4, 'd');
        INSERT INTO f1 VALUES(24, 3, 'c');
        INSERT INTO f1 VALUES(25, 2, 'b');
        INSERT INTO f1 VALUES(26, 1, 'a');
    ]], {
        -- <e_select-9.0>

        -- </e_select-9.0>
    })

-- EVIDENCE-OF: R-30481-56627 Any scalar expression may be used in the
-- LIMIT clause, so long as it evaluates to an integer or a value that
-- can be losslessly converted to an integer.
--
test:do_select_tests(
    "e_select-9.1",
    {
        {"1", "SELECT b FROM f1 ORDER BY a LIMIT 5 ", {"a",  "b", "c", "d", "e"}},
        {"2", "SELECT b FROM f1 ORDER BY a LIMIT 2+3 ", {"a",  "b", "c", "d", "e"}},
        {"3", "SELECT b FROM f1 ORDER BY a LIMIT (SELECT a FROM f1 WHERE b = 'e') ", {"a",  "b", "c", "d", "e"}},
        {"4", "SELECT b FROM f1 ORDER BY a LIMIT 5.0 ", {"a",  "b", "c", "d", "e"}},
    })

-- EVIDENCE-OF: R-46155-47219 If the expression evaluates to a NULL value
-- or any other value that cannot be losslessly converted to an integer,
-- an error is returned.
--
for _, val in ipairs({
    {"1", "SELECT b FROM f1 ORDER BY a LIMIT 'hello' "},
    {"2", "SELECT b FROM f1 ORDER BY a LIMIT NULL "},
    {"3", "SELECT b FROM f1 ORDER BY a LIMIT X'ABCD' "},
    {"4", "SELECT b FROM f1 ORDER BY a LIMIT 5.1 "},
    {"5", "SELECT b FROM f1 ORDER BY a LIMIT (SELECT group_concat(b) FROM f1)"}}) do
    local tn = val[1]
    local select = val[2]
    test:do_catchsql_test(
        "e_select-9.2."..tn,
        select,
        {
            1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"})
end

-- EVIDENCE-OF: R-03014-26414 If the LIMIT expression evaluates to a
-- negative value, then there is no upper bound on the number of rows
-- returned.
--
test:do_select_tests(
    "e_select-9.4",
    {
        {"1", "SELECT b FROM f1 ORDER BY a LIMIT 10000 ", {"a",  "b",  "c",  "d",  "e",  "f",  "g",  "h",  "i",  "j",  "k",  "l",  "m",  "n",  "o",  "p",  "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"}},
        {"2", "SELECT b FROM f1 ORDER BY a LIMIT 123+length('abc')-100 ", {"a",  "b",  "c",  "d",  "e",  "f",  "g",  "h",  "i",  "j",  "k",  "l",  "m",  "n",  "o",  "p",  "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"}},
        {"3", "SELECT b FROM f1 ORDER BY a LIMIT (SELECT count(*) FROM f1)/2 - 10 ", {"a",  "b",  "c"}},
    })

-- EVIDENCE-OF: R-33750-29536 Otherwise, the SELECT returns the first N
-- rows of its result set only, where N is the value that the LIMIT
-- expression evaluates to.
--
test:do_select_tests(
    "e_select-9.5",
    {
        {"1", "SELECT b FROM f1 ORDER BY a LIMIT 0 ", {}},
        {"2", "SELECT b FROM f1 ORDER BY a DESC LIMIT 4 ", {"z", "y", "x", "w"}},
        {"3", "SELECT b FROM f1 ORDER BY a DESC LIMIT 8 ", {"z", "y", "x", "w", "v", "u", "t", "s"}},
        {"4", "SELECT b FROM f1 ORDER BY a DESC LIMIT 12 ", {"z", "y", "x", "w", "v", "u", "t", "s", "r", "q", "p", "o"}},
    })

-- EVIDENCE-OF: R-54935-19057 Or, if the SELECT statement would return
-- less than N rows without a LIMIT clause, then the entire result set is
-- returned.
--
test:do_select_tests(
    "e_select-9.6",
    {
        {"1", "SELECT b FROM f1 WHERE a>21 ORDER BY a LIMIT 10 ", {"v", "w", "x", "y", "z"}},
        {"2", "SELECT count(*) FROM f1 GROUP BY a/5 ORDER BY 1 LIMIT 10 ", {2, 4, 5, 5, 5, 5}},
    })

-- EVIDENCE-OF: R-24188-24349 The expression attached to the optional
-- OFFSET clause that may follow a LIMIT clause must also evaluate to an
-- integer, or a value that can be losslessly converted to an integer.
--
for _, val in ipairs({
    {1, "SELECT b FROM f1 ORDER BY a LIMIT 2 OFFSET 'hello'"},
    {2, "SELECT b FROM f1 ORDER BY a LIMIT 2 OFFSET NULL"},
    {3, "SELECT b FROM f1 ORDER BY a LIMIT 2 OFFSET X'ABCD'"},
    {4, "SELECT b FROM f1 ORDER BY a LIMIT 2 OFFSET 5.1"},
    {5, "SELECT b FROM f1 ORDER BY a LIMIT 2 OFFSET (SELECT group_concat(b) FROM f1)"}}) do
    local tn = val[1]
    local select = val[2]
    test:do_catchsql_test(
        "e_select-9.7."..tn,
        select, {
            1, "Failed to execute SQL statement: Only positive integers are allowed in the OFFSET clause"
        })

end
-- EVIDENCE-OF: R-20467-43422 If an expression has an OFFSET clause, then
-- the first M rows are omitted from the result set returned by the
-- SELECT statement and the next N rows are returned, where M and N are
-- the values that the OFFSET and LIMIT clauses evaluate to,
-- respectively.
--
test:do_select_tests(
    "e_select-9.8",
    {
        {"1", "SELECT b FROM f1 ORDER BY a LIMIT 10 OFFSET 5", {"f", "g", "h", "i", "j", "k", "l", "m", "n", "o"}},
        {"2", "SELECT b FROM f1 ORDER BY a LIMIT 2+3 OFFSET 10", {"k", "l", "m", "n", "o"}},
        {"3", "SELECT b FROM f1 ORDER BY a LIMIT  (SELECT a FROM f1 WHERE b='j') OFFSET (SELECT a FROM f1 WHERE b='b') ", {"c", "d", "e", "f", "g", "h", "i", "j", "k", "l"}},
        {"4", "SELECT b FROM f1 ORDER BY a LIMIT 5 OFFSET 3.0 ", {"d", "e", "f", "g", "h"}},
        {"5", "SELECT b FROM f1 ORDER BY a LIMIT 5 OFFSET 0 ", {"a", "b", "c", "d", "e"}},
        {"6", "SELECT b FROM f1 ORDER BY a LIMIT 0 OFFSET 10 ", {}},
        {"7", "SELECT b FROM f1 ORDER BY a LIMIT 3 OFFSET CAST('1'||'5' AS INTEGER) ", {"p", "q", "r"}},
    })

-- EVIDENCE-OF: R-34648-44875 Or, if the SELECT would return less than
-- M+N rows if it did not have a LIMIT clause, then the first M rows are
-- skipped and the remaining rows (if any) are returned.
--
test:do_select_tests(
    "e_select-9.9",
    {
        {"1", "SELECT b FROM f1 ORDER BY a LIMIT 10 OFFSET 20", {"u", "v", "w", "x", "y", "z"}},
        {"2", "SELECT a FROM f1 ORDER BY a DESC LIMIT 100 OFFSET 18+4", {4, 3, 2, 1}},
    })

-- EVIDENCE-OF: R-23293-62447 If the OFFSET clause evaluates to a
-- negative value, the results are the same as if it had evaluated to
-- zero.
--
test:do_select_tests(
    "e_select-9.10",
    {
        {"1", "SELECT b FROM f1 ORDER BY a LIMIT 5 OFFSET 0 ", {"a", "b", "c", "d", "e"}},
    })

-- EVIDENCE-OF: R-19509-40356 Instead of a separate OFFSET clause, the
-- LIMIT clause may specify two scalar expressions separated by a comma.
--
-- EVIDENCE-OF: R-33788-46243 In this case, the first expression is used
-- as the OFFSET expression and the second as the LIMIT expression.
--
test:do_select_tests(
    "e_select-9.11",
    {
        {"1", "SELECT b FROM f1 ORDER BY a LIMIT 5, 10 ", {"f", "g", "h", "i", "j", "k", "l", "m", "n", "o"}},
        {"2", "SELECT b FROM f1 ORDER BY a LIMIT 10, 2+3 ", {"k", "l", "m", "n", "o"}},
        {"3", "SELECT b FROM f1 ORDER BY a LIMIT (SELECT a FROM f1 WHERE b='b'), (SELECT a FROM f1 WHERE b='j')", {"c", "d", "e", "f", "g", "h", "i", "j", "k", "l"}},
        {"4", "SELECT b FROM f1 ORDER BY a LIMIT 3.0, 5 ", {"d", "e", "f", "g", "h"}},
        {"5", "SELECT b FROM f1 ORDER BY a LIMIT 0, 5 ", {"a", "b", "c", "d", "e"}},
        {"6", "SELECT b FROM f1 ORDER BY a LIMIT 10, 0 ", {}},
        {"7", "SELECT b FROM f1 ORDER BY a LIMIT CAST('1'||'5' AS INTEGER), 3 ", {"p", "q", "r"}},
        {"8", "SELECT b FROM f1 ORDER BY a LIMIT 20, 10 ", {"u", "v", "w", "x", "y", "z"}},
        {"9", "SELECT a FROM f1 ORDER BY a DESC LIMIT 18+4, 100 ", {4, 3, 2, 1}},
        {"10", "SELECT b FROM f1 ORDER BY a LIMIT 0, 5 ", {"a", "b", "c", "d", "e"}},
        {"11", "SELECT b FROM f1 ORDER BY a LIMIT 500-500, 5 ", {"a", "b", "c", "d", "e"}},
    })

test:finish_test()
