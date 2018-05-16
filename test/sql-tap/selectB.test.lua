#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(170)

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
-- $Id: selectB.test,v 1.10 2009/04/02 16:59:47 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


local function test_transform(testname, sql1, sql2, results)
    -- this variables are filled with
    -- opcodes only (line[2]) of explain command)
    local vdbe1 = {  }
    local vdbe2 = {  }
    local data = box.sql.execute("explain "..sql1)
    for i, line in ipairs(data) do
        table.insert(vdbe1, line[2])
    end
    data = box.sql.execute("explain "..sql2)
    for i, line in ipairs(data) do
        table.insert(vdbe2, line[2])
    end
    test:do_test(
        testname..".transform",
        function()
            return vdbe1
        end,
            vdbe2
        )

    sql1 = sql1
    test:do_execsql_test(
        testname..".sql1",
        sql1,
        results)

    sql2 = sql2
    test:do_execsql_test(
        testname..".sql2",
        sql2,
        results)

end

-- MUST_WORK_TEST
-- CREATE TABLE t1(a int, b int, c int);
-- CREATE TABLE t2(d, e, f);
test:do_execsql_test(
    "selectB-1.1",
    [[
        CREATE TABLE t1(id int primary key, a int, b int, c int);
        CREATE TABLE t2(id int primary key, d int, e int, f int);

        INSERT INTO t1 VALUES(0,  2,  4,  6);
        INSERT INTO t1 VALUES(1,  8, 10, 12);
        INSERT INTO t1 VALUES(2, 14, 16, 18);

        INSERT INTO t2 VALUES(0, 3,   6,  9);
        INSERT INTO t2 VALUES(1, 12, 15, 18);
        INSERT INTO t2 VALUES(2, 21, 24, 27);
    ]], {
        -- <selectB-1.1>
        
        -- </selectB-1.1>
    })

for ii = 1, 2, 1 do
    if (ii == 2) then
        test:do_execsql_test(
            "selectB-2.1",
            [[
                CREATE INDEX i1 ON t1(a);
                CREATE INDEX i2 ON t2(d);
            ]], {
                -- <selectB-2.1>
                
                -- </selectB-2.1>
            })

    end
    test_transform("selectB-"..ii..".2", [[
    SELECT * FROM (SELECT a FROM t1 UNION ALL SELECT d FROM t2)
  ]], [[
    SELECT a FROM t1 UNION ALL SELECT d FROM t2
  ]], { 2.0, 8.0, 14.0, 3.0, 12.0, 21.0})
    test_transform("selectB-"..ii..".3", [[
    SELECT * FROM (SELECT a FROM t1 UNION ALL SELECT d FROM t2) ORDER BY 1
  ]], [[
    SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY 1
  ]], { 2.0, 3.0, 8.0, 12.0, 14.0, 21.0})
    test_transform("selectB-"..ii..".4", [[
    SELECT * FROM 
      (SELECT a FROM t1 UNION ALL SELECT d FROM t2) 
    WHERE a>10 ORDER BY 1
  ]], [[
    SELECT a FROM t1 WHERE a>10 UNION ALL SELECT d FROM t2 WHERE d>10 ORDER BY 1
  ]], { 12.0, 14.0, 21.0})
    test_transform("selectB-"..ii..".5", [[
    SELECT * FROM 
      (SELECT a FROM t1 UNION ALL SELECT d FROM t2) 
    WHERE a>10 ORDER BY a
  ]], [[
    SELECT a FROM t1 WHERE a>10 
      UNION ALL 
    SELECT d FROM t2 WHERE d>10 
    ORDER BY a
  ]], { 12.0, 14.0, 21.0})
    test_transform("selectB-"..ii..".6", [[
    SELECT * FROM 
      (SELECT a FROM t1 UNION ALL SELECT d FROM t2 WHERE d > 12) 
    WHERE a>10 ORDER BY a
  ]], [[
    SELECT a FROM t1 WHERE a>10
      UNION ALL 
    SELECT d FROM t2 WHERE d>12 AND d>10
    ORDER BY a
  ]], { 14.0, 21.0})
    test_transform("selectB-"..ii..".7", [[
    SELECT * FROM (SELECT a FROM t1 UNION ALL SELECT d FROM t2) ORDER BY 1 
    LIMIT 2
  ]], [[
    SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY 1 LIMIT 2
  ]], { 2.0, 3.0})
    test_transform("selectB-"..ii..".8", [[
    SELECT * FROM (SELECT a FROM t1 UNION ALL SELECT d FROM t2) ORDER BY 1 
    LIMIT 2 OFFSET 3
  ]], [[
    SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY 1 LIMIT 2 OFFSET 3
  ]], { 12.0, 14.0})
    test_transform("selectB-"..ii..".9", [[
    SELECT * FROM (
      SELECT a FROM t1 UNION ALL SELECT d FROM t2 UNION ALL SELECT c FROM t1
    ) 
  ]], [[
    SELECT a FROM t1 UNION ALL SELECT d FROM t2 UNION ALL SELECT c FROM t1
  ]], { 2.0, 8.0, 14.0, 3.0, 12.0, 21.0, 6.0, 12.0, 18.0})
    test_transform("selectB-"..ii..".10", [[
    SELECT * FROM (
      SELECT a FROM t1 UNION ALL SELECT d FROM t2 UNION ALL SELECT c FROM t1
    ) ORDER BY 1
  ]], [[
    SELECT a FROM t1 UNION ALL SELECT d FROM t2 UNION ALL SELECT c FROM t1
    ORDER BY 1
  ]], { 2.0, 3.0, 6.0, 8.0, 12.0, 12.0, 14.0, 18.0, 21.0})
    test_transform("selectB-"..ii..".11", [[
    SELECT * FROM (
      SELECT a FROM t1 UNION ALL SELECT d FROM t2 UNION ALL SELECT c FROM t1
    ) WHERE a>=10 ORDER BY 1 LIMIT 3
  ]], [[
    SELECT a FROM t1 WHERE a>=10 UNION ALL SELECT d FROM t2 WHERE d>=10
    UNION ALL SELECT c FROM t1 WHERE c>=10
    ORDER BY 1 LIMIT 3
  ]], { 12.0, 12.0, 14.0})
    test_transform("selectB-"..ii..".12", [[
    SELECT * FROM (SELECT a FROM t1 UNION ALL SELECT d FROM t2 LIMIT 2)
  ]], [[
    SELECT a FROM t1 UNION ALL SELECT d FROM t2 LIMIT 2
  ]], { 2.0, 8.0})
    -- MUST_WORK_TEST
    if (0 > 0) then
        -- An ORDER BY in a compound subqueries defeats flattening.  Ticket #3773
        test_transform("selectB-"..ii..".13", [[
       SELECT * FROM (SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY a ASC)
     ]], [[
       SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY 1 ASC
     ]], "2 3 8 12 14 21")
        test_transform("selectB-"..ii..".14", [[
      SELECT * FROM (SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY a DESC)
     ]], [[
      SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY 1 DESC
     ]], "21 14 12 8 3 2")
        test_transform("selectB-"..ii..".14", [[
       SELECT * FROM (
         SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY a DESC
       ) LIMIT 2 OFFSET 2
     ]], [[
       SELECT a FROM t1 UNION ALL SELECT d FROM t2 ORDER BY 1 DESC
        LIMIT 2 OFFSET 2
     ]], "12 8")
        test_transform("selectB-"..ii..".15", [[
       SELECT * FROM (
         SELECT a, b FROM t1 UNION ALL SELECT d, e FROM t2 ORDER BY a ASC, e DESC
      )
     ]], [[
       SELECT a, b FROM t1 UNION ALL SELECT d, e FROM t2 ORDER BY a ASC, e DESC
     ]], "2 4 3 6 8 10 12 15 14 16 21 24")
    end
end
test:do_execsql_test(
    "selectB-3.0",
    [[
        DROP INDEX i1 ON t1;
        DROP INDEX i2 ON t2;
    ]], {
        -- <selectB-3.0>
        
        -- </selectB-3.0>
    })

for ii = 3, 6, 1 do
    if ii == 4 then
        -- TODO
        --X(2, "X!cmd", [=[["optimization_control","db","query-flattener","off"]]=])
    elseif ii == 5 then
        --X(2, "X!cmd", [=[["optimization_control","db","query-flattener","on"]]=])
        test:do_execsql_test(
            "selectB-5.0",
            [[
                CREATE INDEX i1 ON t1(a);
                CREATE INDEX i2 ON t1(b);
                CREATE INDEX i3 ON t1(c);
                CREATE INDEX i4 ON t2(d);
                CREATE INDEX i5 ON t2(e);
                CREATE INDEX i6 ON t2(f);
            ]], {
                -- <selectB-5.0>
                
                -- </selectB-5.0>
            })

    elseif ii == 6 then
        --X(2, "X!cmd", [=[["optimization_control","db","query-flattener","off"]]=])
    end
    test:do_execsql_test(
        "selectB-"..ii..".1",
        [[
            SELECT DISTINCT * FROM 
              (SELECT c FROM t1 UNION ALL SELECT e FROM t2) 
            ORDER BY 1;
        ]], {
            6, 12, 15, 18, 24
        })

    test:do_execsql_test(
        "selectB-"..ii..".2",
        [[
            SELECT c, count(*) FROM 
              (SELECT c FROM t1 UNION ALL SELECT e FROM t2) 
            GROUP BY c ORDER BY 1;
        ]], {
            6, 2, 12, 1, 15, 1, 18, 1, 24, 1
        })

    test:do_execsql_test(
        "selectB-"..ii..".3",
        [[
            SELECT c, count(*) FROM 
              (SELECT c FROM t1 UNION ALL SELECT e FROM t2) 
            GROUP BY c HAVING count(*)>1;
        ]], {
            6, 2
        })

    test:do_execsql_test(
        "selectB-"..ii..".4",
        [[
            SELECT t4.c, t3.a FROM 
              (SELECT c FROM t1 UNION ALL SELECT e FROM t2) AS t4, t1 AS t3
            WHERE t3.a=14
            ORDER BY 1
        ]], {
            6, 14, 6, 14, 12, 14, 15, 14, 18, 14, 24, 14
        })

    test:do_execsql_test(
        "selectB-"..ii..".5",
        [[
            SELECT d FROM t2 
            EXCEPT 
            SELECT a FROM (SELECT a FROM t1 UNION ALL SELECT d FROM t2)
        ]], {
            
        })

    test:do_execsql_test(
        "selectB-"..ii..".6",
        [[
            SELECT * FROM (SELECT a FROM t1 UNION ALL SELECT d FROM t2)
            EXCEPT 
            SELECT * FROM (SELECT a FROM t1 UNION ALL SELECT d FROM t2)
        ]], {
            
        })

    test:do_execsql_test(
        "selectB-"..ii..".7",
        [[
            SELECT c FROM t1
            EXCEPT 
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
        ]], {
            12
        })

    test:do_execsql_test(
        "selectB-"..ii..".8",
        [[
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
            EXCEPT 
            SELECT c FROM t1
        ]], {
            9, 15, 24, 27
        })

    test:do_execsql_test(
        "selectB-"..ii..".9",
        [[
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
            EXCEPT 
            SELECT c FROM t1
            ORDER BY c DESC
        ]], {
            27, 24, 15, 9
        })

    test:do_execsql_test(
        "selectB-"..ii..".10",
        [[
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
            UNION 
            SELECT c FROM t1
            ORDER BY c DESC
        ]], {
            27, 24, 18, 15, 12, 9, 6
        })

    test:do_execsql_test(
        "selectB-"..ii..".11",
        [[
            SELECT c FROM t1
            UNION 
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
            ORDER BY c
        ]], {
            6, 9, 12, 15, 18, 24, 27
        })

    test:do_execsql_test(
        "selectB-"..ii..".12",
        [[
            SELECT c FROM t1 UNION SELECT e FROM t2 UNION ALL SELECT f FROM t2
            ORDER BY c
        ]], {
            6, 9, 12, 15, 18, 18, 24, 27
        })

    test:do_execsql_test(
        "selectB-"..ii..".13",
        [[
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
            UNION 
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
            ORDER BY 1
        ]], {
            6, 9, 15, 18, 24, 27
        })

    test:do_execsql_test(
        "selectB-"..ii..".14",
        [[
            SELECT c FROM t1
            INTERSECT 
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
            ORDER BY 1
        ]], {
            6, 18
        })

    test:do_execsql_test(
        "selectB-"..ii..".15",
        [[
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
            INTERSECT 
            SELECT c FROM t1
            ORDER BY 1
        ]], {
            6, 18
        })

    test:do_execsql_test(
        "selectB-"..ii..".16",
        [[
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
            INTERSECT 
            SELECT * FROM (SELECT e FROM t2 UNION ALL SELECT f FROM t2)
            ORDER BY 1
        ]], {
            6, 9, 15, 18, 24, 27
        })

    test:do_execsql_test(
        "selectB-"..ii..".17",
        [[
            SELECT * FROM (
              SELECT a FROM t1 UNION ALL SELECT d FROM t2 LIMIT 4
            ) LIMIT 2
        ]], {
            2, 8
        })

    test:do_execsql_test(
        "selectB-"..ii..".18",
        [[
            SELECT * FROM (
              SELECT a FROM t1 UNION ALL SELECT d FROM t2 LIMIT 4 OFFSET 2
            ) LIMIT 2
        ]], {
            14, 3
        })

    test:do_execsql_test(
        "selectB-"..ii..".19",
        [[
            SELECT * FROM (
              SELECT DISTINCT (a/10) FROM t1 UNION ALL SELECT DISTINCT(d%2) FROM t2
            )
        ]], {
            0, 1, 1, 0
        })

    test:do_execsql_test(
        "selectB-"..ii..".20",
        [[
            SELECT DISTINCT * FROM (
              SELECT DISTINCT (a/10) FROM t1 UNION ALL SELECT DISTINCT(d%2) FROM t2
            )
        ]], {
            0, 1
        })

    test:do_execsql_test(
        "selectB-"..ii..".21",
        [[
            SELECT * FROM (SELECT a,b,c FROM t1 UNION ALL SELECT d,e,f FROM t2) ORDER BY a+b
        ]], {
            2, 4, 6, 3, 6, 9, 8, 10, 12, 12, 15, 18, 14, 16, 18, 21, 24, 27
        })

    test:do_execsql_test(
        "selectB-"..ii..".22",
        [[
            SELECT * FROM (SELECT 345 UNION ALL SELECT d FROM t2) ORDER BY 1;
        ]], {
            3, 12, 21, 345
        })

    test:do_execsql_test(
        "selectB-"..ii..".23",
        [[
            SELECT x, y FROM (
              SELECT a AS x, b AS y FROM t1
              UNION ALL
              SELECT a*10 + 0.1, f*10 + 0.1 FROM t1 JOIN t2 ON (c=d)
              UNION ALL
              SELECT a*100, b*100 FROM t1
            ) ORDER BY 1;
        ]], {
            2, 4, 8, 10, 14, 16, 80.1, 180.1, 200, 400, 800, 1000, 1400, 1600
        })

    test:do_execsql_test(
        "selectB-"..ii..".24",
        [[
            SELECT x, y FROM (
              SELECT a AS x, b AS y FROM t1
              UNION ALL
              SELECT a*10 + 0.1, f*10 + 0.1 FROM t1 LEFT JOIN t2 ON (c=d)
              UNION ALL
              SELECT a*100, b*100 FROM t1
            ) ORDER BY 1;
        ]], {
            2, 4, 8, 10, 14, 16, 20.1, "", 80.1, 180.1, 140.1, "", 200, 400, 800, 1000, 1400, 1600
        })

    test:do_execsql_test(
        "selectB-"..ii..".25",
        [[
            SELECT x+y FROM (
              SELECT a AS x, b AS y FROM t1
              UNION ALL
              SELECT a*10 + 0.1, f*10 + 0.1 FROM t1 LEFT JOIN t2 ON (c=d)
              UNION ALL
              SELECT a*100, b*100 FROM t1
            ) WHERE y+x IS NOT NULL ORDER BY 1;
        ]], {
            6, 18, 30, 260.2, 600, 1800, 3000
        })

end
test:finish_test()

