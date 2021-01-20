#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(83)

--!./tcltestrunner.lua
-- 2002 February 26
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
-- focus of this file is testing VIEW statements.
--
-- $Id: view.test,v 1.39 2008/12/14 14:45:21 danielk1977 Exp $
-- set testdir [file dirname $argv0]
-- source $testdir/tester.tcl
-- Omit this entire file if the library is not configured with views enabled.


-- ORIGINAL_TEST
-- do_test view-1.0 {
--   execsql {
--     CREATE TABLE t1(a INT,b INT,c INT);
--     INSERT INTO t1 VALUES(1,2,3);
--     INSERT INTO t1 VALUES(4,5,6);
--     INSERT INTO t1 VALUES(7,8,9);
--     SELECT * FROM t1;
--   }
-- } {1 2 3 4 5 6 7 8 9}
--
test:do_execsql_test(
    "view-1.0",
    [[
        CREATE TABLE t1(a INT primary key,b INT,c INT);
        INSERT INTO t1 VALUES(1,2,3);
        INSERT INTO t1 VALUES(4,5,6);
        INSERT INTO t1 VALUES(7,8,9);
        SELECT * FROM t1;
    ]], {
        -- <view-1.0>
        1, 2, 3, 4, 5, 6, 7, 8, 9
        -- </view-1.0>
    })

-- MUST_WORK_TEST
if (0 > 0)
 then
    test:do_execsql_test(
        "view-1.1",
        [[
            START TRANSACTION;
            CREATE VIEW IF NOT EXISTS v1 AS SELECT a,b FROM t1;
            SELECT * FROM v1 ORDER BY a;
        ]], {
            -- <view-1.1>
            1, 2, 4, 5, 7, 8
            -- </view-1.1>
        })

    test:do_catchsql_test(
        "view-1.2",
        [[
            ROLLBACK;
            SELECT * FROM v1 ORDER BY a;
        ]], {
            -- <view-1.2>
            1, "Space 'V1' does not exist"
            -- </view-1.2>
        })

end
test:do_execsql_test(
    "view-1.3",
    [[
        CREATE VIEW v1 AS SELECT a,b FROM t1;
        SELECT * FROM v1 ORDER BY a;
    ]], {
        -- <view-1.3>
        1, 2, 4, 5, 7, 8
        -- </view-1.3>
    })

test:do_test(
    "view-1.3.1",
    function()
        return test:execsql [[
            SELECT * FROM v1 ORDER BY a;
        ]]
    end, {
        -- <view-1.3.1>
        1, 2, 4, 5, 7, 8
        -- </view-1.3.1>
    })

test:do_catchsql_test(
    "view-1.4",
    [[
        DROP VIEW IF EXISTS v1;
        SELECT * FROM v1 ORDER BY a;
    ]], {
        -- <view-1.4>
        1, "Space 'V1' does not exist"
        -- </view-1.4>
    })

test:do_execsql_test(
    "view-1.5",
    [[
        CREATE VIEW v1 AS SELECT a,b FROM t1;
        SELECT * FROM v1 ORDER BY a;
    ]], {
        -- <view-1.5>
        1, 2, 4, 5, 7, 8
        -- </view-1.5>
    })

test:do_catchsql_test(
    "view-1.6",
    [[
        DROP TABLE t1;
    ]], {
        -- <view-1.6>
        1, "Can't drop space 'T1': other views depend on this space"
        -- </view-1.6>
    })

test:do_catchsql_test(
    "view-1.7",
    [[
        DROP VIEW v1;
        DROP TABLE t1;
    ]], {
        -- <view-1.7>
        0
        -- </view-1.7>
    })

-- ORIGINAL_TEST
-- do_test view-1.7 {
--   execsql {
--     CREATE TABLE t1(x INT,a INT,b INT,c INT);
--     INSERT INTO t1 VALUES(1,2,3,4);
--     INSERT INTO t1 VALUES(4,5,6,7);
--     INSERT INTO t1 VALUES(7,8,9,10);
--     SELECT * FROM v1 ORDER BY a;
--   }
-- } {2 3 5 6 8 9}
test:do_execsql_test(
    "view-1.8",
    [[
        CREATE TABLE t1(x INT primary key,a INT,b INT,c INT);
        INSERT INTO t1 VALUES(1,2,3,4);
        INSERT INTO t1 VALUES(4,5,6,7);
        INSERT INTO t1 VALUES(7,8,9,10);
        CREATE VIEW v1 AS SELECT a,b FROM t1;
        SELECT * FROM v1 ORDER BY a;
    ]], {
        -- <view-1.7>
        2, 3, 5, 6, 8, 9
        -- </view-1.7>
    })

test:do_test(
    "view-2.1",
    function()
        test:execsql [[
            CREATE VIEW v2 AS SELECT * FROM t1 WHERE a>5
        ]]
        -- No semicolon
        return test:execsql2 [[
            SELECT * FROM v2;
        ]]
    end, {
        -- <view-2.1>
        "X", 7, "A", 8, "B", 9, "C", 10
        -- </view-2.1>
    })

test:do_catchsql_test(
    "view-2.2",
    [[
        INSERT INTO v2 VALUES(1,2,3,4);
    ]], {
        -- <view-2.2>
        1, "Can't modify space 'V2': space is a view"
        -- </view-2.2>
    })

test:do_catchsql_test(
    "view-2.3",
    [[
        UPDATE v2 SET a=10 WHERE a=5;
    ]], {
        -- <view-2.3>
        1, "Can't modify space 'V2': space is a view"
        -- </view-2.3>
    })

test:do_catchsql_test(
    "view-2.4",
    [[
        DELETE FROM v2;
    ]], {
        -- <view-2.4>
        1, "Can't modify space 'V2': space is a view"
        -- </view-2.4>
    })

test:do_execsql_test(
    "view-2.5",
    [[
        INSERT INTO t1 VALUES(11,12,13,14);
        SELECT * FROM v2 ORDER BY x;
    ]], {
        -- <view-2.5>
        7, 8, 9, 10, 11, 12, 13, 14
        -- </view-2.5>
    })

test:do_execsql_test(
    "view-2.6",
    [[
        SELECT x FROM v2 WHERE a>10
    ]], {
        -- <view-2.6>
        11
        -- </view-2.6>
    })

--
-- Test that column name of views are generated correctly.
--
test:do_execsql2_test(
    "view-3.1",
    [[
        SELECT * FROM v1 LIMIT 1
    ]], {
        -- <view-3.1>
        "A", 2, "B", 3
        -- </view-3.1>
    })

test:do_execsql2_test(
    "view-3.2",
    [[
        SELECT * FROM v2 LIMIT 1
    ]], {
        -- <view-3.2>
        "X", 7, "A", 8, "B", 9, "C", 10
        -- </view-3.2>
    })

test:do_execsql2_test(
    "view-3.3.1",
    [[
        DROP VIEW v1;
        CREATE VIEW v1 AS SELECT a AS xyz, b+c AS pqr, c-b FROM t1;
        SELECT * FROM v1 LIMIT 1
    ]], {
        -- <view-3.3.1>
        "XYZ", 2, "PQR", 7, "COLUMN_1", 1
        -- </view-3.3.1>
    })

test:do_execsql2_test(
    "view-3.3.2",
    [[
        CREATE VIEW v1b AS SELECT t1.a, b+c, t1.c FROM t1;
        SELECT * FROM v1b LIMIT 1
    ]], {
        -- <view-3.3.2>
        "A", 2, "COLUMN_1", 7, "C", 4
        -- </view-3.3.2>
    })

test:do_execsql2_test(
    "view-3.3.3",
    [[
        CREATE VIEW v1c(x,y,z) AS SELECT a, b+c, c-b FROM t1;
        SELECT * FROM v1c LIMIT 1;
    ]],{"X", 2, "Y", 7, "Z", 1})

test:do_catchsql_test(
    "view-3.3.4",
    [[
        CREATE VIEW v1err(x,y DESC,z) AS SELECT a, b+c, c-b FROM t1;
    ]], {
        -- <view-3.3.4>
        1, [[At line 1 at or near position 31: keyword 'DESC' is reserved. Please use double quotes if 'DESC' is an identifier.]]
        -- </view-3.3.4>
    })

test:do_catchsql_test(
    "view-3.3.5.1",
    [[
        CREATE VIEW v1err(x,y) AS SELECT a, b+c, c-b FROM t1;
        SELECT * FROM v1err;
    ]], {1, "Failed to create space 'V1ERR': number of aliases doesn't match provided columns"})

test:do_catchsql_test(
    "view-3.3.5.2",
    [[
        DROP VIEW IF EXISTS v1err;
        CREATE VIEW v1err(w,x,y,z) AS SELECT a, b+c, c-b FROM t1;
        SELECT * FROM v1err;
    ]], {1, "Failed to create space 'V1ERR': number of aliases doesn't match provided columns"})

-- #MUST_WORK_TEST no query solution
-- # ifcapable compound {
test:do_execsql2_test(
    "view-3.4",
    [[
        CREATE VIEW v3 AS SELECT a FROM t1 UNION SELECT b FROM t1 ORDER BY b;
        SELECT * FROM v3 LIMIT 4;
    ]], {
        -- <view-3.4>
        "A", 2, "A", 3, "A", 5, "A", 6
        -- </view-3.4>
    })

-- X(237, "X!cmd", [=[["ifcapable","compound"]]=])
test:do_catchsql_test(
    "view-4.1",
    [[
        DROP VIEW t1;
    ]], {
        -- <view-4.1>
        1, "Can't drop space 'T1': use DROP TABLE"
        -- </view-4.1>
    })

test:do_execsql_test(
    "view-4.2",
    [[
        SELECT 1 FROM t1 LIMIT 1;
    ]], {
        -- <view-4.2>
        1
        -- </view-4.2>
    })

test:do_catchsql_test(
    "view-4.3",
    [[
        DROP TABLE v1;
    ]], {
        -- <view-4.3>
        1, "Can't drop space 'V1': use DROP VIEW"
        -- </view-4.3>
    })

test:do_execsql_test(
    "view-4.4",
    [[
        SELECT 1 FROM v1 LIMIT 1;
    ]], {
        -- <view-4.4>
        1
        -- </view-4.4>
    })

test:do_catchsql_test(
    "view-4.5",
    [[
        CREATE INDEX i1v1 ON v1(xyz);
    ]], {
        -- <view-4.5>
        1, "Can't create or modify index 'I1V1' in space 'V1': views can not be indexed"
        -- </view-4.5>
    })

test:do_execsql_test(
    "view-5.1",
    [[
        CREATE TABLE t2(y INT primary key,a INT);
        INSERT INTO t2 VALUES(22,2);
        INSERT INTO t2 VALUES(33,3);
        INSERT INTO t2 VALUES(44,4);
        INSERT INTO t2 VALUES(55,5);
        SELECT * FROM t2;
    ]], {
        -- <view-5.1>
        22, 2, 33, 3, 44, 4, 55, 5
        -- </view-5.1>
    })

test:do_execsql_test(
    "view-5.2",
    [[
        CREATE VIEW v5 AS
          SELECT t1.x AS v, t2.y AS w FROM t1 JOIN t2 USING(a);
        SELECT * FROM v5;
    ]], {
        -- <view-5.2>
        1, 22, 4, 55
        -- </view-5.2>
    })

-- Verify that the view v5 gets flattened.  see sqlFlattenSubquery().
-- This will only work if EXPLAIN is enabled.
-- Ticket #272
--
test:do_test(
    "view-5.3",
    function()
        return test:lsearch(test:execsql("EXPLAIN SELECT * FROM v5;"),"OpenEphemeral");
    end, -1)

test:do_execsql_test(
    "view-5.4",
    [[
        SELECT * FROM v5 AS a, t2 AS b WHERE a.w=b.y;
    ]], {
        -- <view-5.4>
        1, 22, 22, 2, 4, 55, 55, 5
        -- </view-5.4>
    })

test:do_test(
    "view-5.5",
    function()
        return test:lsearch(test:execsql("EXPLAIN SELECT * FROM v5 AS a, t2 AS b WHERE a.w=b.y;"),"OpenEphemeral");
    end,-1)

test:do_execsql_test(
    "view-5.6",
    [[
        SELECT * FROM t2 AS b, v5 AS a WHERE a.w=b.y;
    ]], {
        -- <view-5.6>
        22, 2, 1, 22, 55, 5, 4, 55
        -- </view-5.6>
    })

test:do_test(
    "view-5.7",
    function()
        return test:lsearch(test:execsql("EXPLAIN SELECT * FROM t2 AS b, v5 AS a WHERE a.w=b.y;"),"OpenEphemeral");
    end, -1)

test:do_execsql_test(
    "view-5.8",
    [[
        SELECT * FROM t1 AS a, v5 AS b, t2 AS c WHERE a.x=b.v AND b.w=c.y;
    ]], {
        -- <view-5.8>
        1, 2, 3, 4, 1, 22, 22, 2, 4, 5, 6, 7, 4, 55, 55, 5
        -- </view-5.8>
    })

test:do_test(
    "view-5.9",
    function()
        local r = test:execsql("EXPLAIN SELECT * FROM t1 AS a, v5 AS b, t2 AS c WHERE a.x=b.v AND b.w=c.y;")
        return test:lsearch(r,"OpenEphemeral");
    end, -1)



-- endif explain
test:do_execsql_test(
    "view-6.1",
    [[
        SELECT min(x), min(a), min(b), min(c), min(a+b+c) FROM v2;
    ]], {
        -- <view-6.1>
        7, 8, 9, 10, 27
        -- </view-6.1>
    })

test:do_execsql_test(
    "view-6.2",
    [[
        SELECT max(x), max(a), max(b), max(c), max(a+b+c) FROM v2;
    ]], {
        -- <view-6.2>
        11, 12, 13, 14, 39
        -- </view-6.2>
    })

test:do_execsql_test(
    "view-7.1",
    [[
        CREATE TABLE test1(id integer primary key, a INT);
        CREATE TABLE test2(id integer primary key, b INT);
        INSERT INTO test1 VALUES(1,2);
        INSERT INTO test2 VALUES(1,3);
        CREATE VIEW test AS
          SELECT test1.id, a, b
          FROM test1 JOIN test2 ON test2.id=test1.id;
        SELECT * FROM test;
    ]], {
        -- <view-7.1>
        1, 2, 3
        -- </view-7.1>
    })

test:do_test(
    "view-7.2",
    function()
        return test:execsql [[
            SELECT * FROM test;
        ]]
    end, {
        -- <view-7.2>
        1, 2, 3
        -- </view-7.2>
    })

test:do_execsql_test(
    "view-7.3",
    [[
        DROP VIEW test;
        CREATE VIEW test AS
          SELECT test1.id, a, b
          FROM test1 JOIN test2 USING(id);
        SELECT * FROM test;
    ]], {
        -- <view-7.3>
        1, 2, 3
        -- </view-7.3>
    })

test:do_test(
    "view-7.4",
    function()
        return test:execsql [[
            SELECT * FROM test;
        ]]
    end, {
        -- <view-7.4>
        1, 2, 3
        -- </view-7.4>
    })

test:do_execsql_test(
    "view-7.5",
    [[
        DROP VIEW test;
        CREATE VIEW test AS
          SELECT test1.id, a, b
          FROM test1 NATURAL JOIN test2;
        SELECT * FROM test;
    ]], {
        -- <view-7.5>
        1, 2, 3
        -- </view-7.5>
    })

test:do_test(
    "view-7.6",
    function()
        return test:execsql [[
            SELECT * FROM test;
        ]]
    end, {
        -- <view-7.6>
        1, 2, 3
        -- </view-7.6>
    })

test:do_execsql_test(
    "view-8.1",
    [[
        CREATE VIEW v6 AS SELECT pqr, xyz FROM v1;
        SELECT * FROM v6 ORDER BY xyz;
    ]], {
        -- <view-8.1>
        7, 2, 13, 5, 19, 8, 27, 12
        -- </view-8.1>
    })

test:do_test(
    "view-8.2",
    function()
        return test:execsql [[
            SELECT * FROM v6 ORDER BY xyz;
        ]]
    end, {
        -- <view-8.2>
        7, 2, 13, 5, 19, 8, 27, 12
        -- </view-8.2>
    })

test:do_execsql_test(
    "view-8.3",
    [[
        CREATE VIEW v7(a) AS SELECT pqr+xyz FROM v6;
        SELECT * FROM v7 ORDER BY a;
    ]], {
        -- <view-8.3>
        9, 18, 27, 39
        -- </view-8.3>
    })

test:do_execsql_test(
    "view-8.4",
    [[
        CREATE VIEW v8 AS SELECT max(cnt) AS mx FROM
          (SELECT a%2 AS eo, count(*) AS cnt FROM t1 GROUP BY eo);
        SELECT * FROM v8;
    ]], {
        -- <view-8.4>
        3
        -- </view-8.4>
    })

test:do_execsql_test(
    "view-8.5",
    [[
        SELECT mx+10, mx*2 FROM v8;
    ]], {
        -- <view-8.5>
        13, 6
        -- </view-8.5>
    })

test:do_execsql_test(
    "view-8.6",
    [[
        SELECT mx+10, pqr FROM v6, v8 WHERE xyz=2;
    ]], {
        -- <view-8.6>
        13, 7
        -- </view-8.6>
    })

test:do_execsql_test(
    "view-8.7",
    [[
        SELECT mx+10, pqr FROM v6, v8 WHERE xyz>2;
    ]], {
        -- <view-8.7>
        13, 13, 13, 19, 13, 27
        -- </view-8.7>
    })


-- ifcapable subquery
-- Tests for a bug found by Michiel de Wit involving ORDER BY in a VIEW.
--

test:do_execsql_test(
    "view-9.1",
    [[
        --- INSERT INTO t2 SELECT * FROM t2 WHERE a<5;
        --- INSERT INTO t2 SELECT * FROM t2 WHERE a<4;
        --- INSERT INTO t2 SELECT * FROM t2 WHERE a<3;
        INSERT INTO t2 SELECT y+100, a FROM t2 WHERE a<5;
        INSERT INTO t2 SELECT y+400, a FROM t2 WHERE a<4;
        INSERT INTO t2 SELECT y+800, a FROM t2 WHERE a<3;
        SELECT DISTINCT count(*) FROM t2 GROUP BY a ORDER BY 1;
    ]], {
        -- <view-9.1>
        1, 2, 4, 8
        -- </view-9.1>
    })

test:do_execsql_test(
    "view-9.2",
    [[
        SELECT DISTINCT count(*) FROM t2 GROUP BY a ORDER BY 1 LIMIT 3;
    ]], {
        -- <view-9.2>
        1, 2, 4
        -- </view-9.2>
    })

test:do_execsql_test(
    "view-9.3",
    [[
        CREATE VIEW v9 AS
           SELECT DISTINCT count(*) FROM t2 GROUP BY a ORDER BY 1 LIMIT 3;
        SELECT * FROM v9;
    ]], {
        -- <view-9.3>
        1, 2, 4
        -- </view-9.3>
    })

test:do_execsql_test(
    "view-9.4",
    [[
        SELECT * FROM v9 ORDER BY 1 DESC;
    ]], {
        -- <view-9.4>
        4, 2, 1
        -- </view-9.4>
    })

test:do_execsql_test(
    "view-9.5",
    [[
        CREATE VIEW v10 AS
           SELECT DISTINCT a, count(*) FROM t2 GROUP BY a ORDER BY 2 LIMIT 3;
        SELECT * FROM v10;
    ]], {
        -- <view-9.5>
        5, 1, 4, 2, 3, 4
        -- </view-9.5>
    })

test:do_execsql_test(
    "view-9.6",
    [[
        SELECT * FROM v10 ORDER BY 1;
    ]], {
        -- <view-9.6>
        3, 4, 4, 2, 5, 1
        -- </view-9.6>
    })

-- Tables with columns having peculiar quoted names used in views"]]=])
-- Ticket #756
test:do_execsql_test(
    "view-10.1",
    [=[
        CREATE TABLE t3("9" integer primary key, "4" text);
        INSERT INTO t3 VALUES(1,'2');
        CREATE VIEW v_t3_a AS SELECT a."9" FROM t3 AS a;
        CREATE VIEW v_t3_b AS SELECT "4" FROM t3;
        SELECT * FROM v_t3_a;
    ]=], {
        -- <view-10.1>
        1
        -- </view-10.1>
    })

test:do_execsql_test(
    "view-10.2",
    [[
        SELECT * FROM v_t3_b;
    ]], {
        -- <view-10.2>
        "2"
        -- </view-10.2>
    })

-- MUST_WORK_TEST COLLATE NOCASE
if (0 > 0)
 then
    test:do_execsql_test(
        "view-11.1",
        [[
            CREATE TABLE t4(a TEXT COLLATE "unicode_ci" primary key);
            INSERT INTO t4 VALUES('This');
            INSERT INTO t4 VALUES('this');
            INSERT INTO t4 VALUES('THIS');
            SELECT * FROM t4 WHERE a = 'THIS';
        ]], {
            -- <view-11.1>
            "This", "this", "THIS"
            -- </view-11.1>
        })

    -- MUST_WORK_TEST ,"nocase","dont","work"]]=])
    test:do_execsql_test(
        "view-11.1",
        [[
            CREATE TABLE t4(a TEXT COLLATE "unicode_ci" primary key);
            INSERT INTO t4 VALUES('This');
            INSERT INTO t4 VALUES('this');
            INSERT INTO t4 VALUES('THIS');
            SELECT * FROM t4 WHERE a = 'THIS';
        ]], {
            -- <view-11.1>
            "This", "this", "THIS"
            -- </view-11.1>
        })

    -- MUST_WORK_TEST ,"nocase","dont","work"]]=])
    test:do_execsql_test(
        "view-11.2",
        [[
            SELECT * FROM (SELECT * FROM t4) WHERE a = 'THIS';
        ]], {
            -- <view-11.2>
            "This", "this", "THIS"
            -- </view-11.2>
        })



    -- MUST_WORK_TEST nocase dont work
    test:do_execsql_test(
        "view-11.3",
        [[
            CREATE VIEW v11 AS SELECT * FROM t4;
            SELECT * FROM v11 WHERE a = 'THIS';
        ]], {
            -- <view-11.3>
            "This", "this", "THIS"
            -- </view-11.3>
        })

    -- Ticket #1270: Do not allow parameters in view definitions.
end
test:do_catchsql_test(
    "view-12.1",
    [[
        CREATE VIEW v12 AS SELECT a FROM t1 WHERE b=?
    ]], {
        -- <view-12.1>
        1, "Failed to create space 'V12': parameters are not allowed in views"
        -- </view-12.1>
    })

test:do_catchsql_test(
    "view-12.2",
    [[
        CREATE VIEW v12(x) AS SELECT a FROM t1 WHERE b=?
    ]], {
        -- <view-12.2>
        1, "Failed to create space 'V12': parameters are not allowed in views"
        -- </view-12.2>
    })

-- ORIGINAL_TEST
-- ifcapable attach {
--   do_test view-13.1 {
--     forcedelete test2.db
--     catchsql {
--       ATTACH 'test2.db' AS two;
--       CREATE TABLE two.t2(x INT,y INT);
--       CREATE VIEW v13 AS SELECT y FROM two.t2;
--     }
--   } {1 {view v13 cannot reference objects in database two}}
-- }
--ifcapable attach {
--  do_test view-13.1 {
--    forcedelete test2.db
--    catchsql {
--      ATTACH 'test2.db' AS two;
--      CREATE TABLE two.t2(x INT primary key,y INT);
--      CREATE VIEW v13 AS SELECT y FROM two.t2;
--    }
--  } {1 {view v13 cannot reference objects in database two}}
--}
-- Ticket #1658
--
-- MUST_WORK_TEST temp views do not work
if (0 > 0)
 then
    test:do_catchsql_test(
        "view-14.1",
        [[
            CREATE TEMP VIEW t1 AS SELECT a,b FROM t1;
            SELECT * FROM temp.t1;
        ]], {
            -- <view-14.1>
            1, "view t1 is circularly defined"
            -- </view-14.1>
        })

end
-- Tickets #1688 #1709
test:do_execsql2_test(
    "view-15.1",
    [[
        CREATE VIEW v15 AS SELECT a AS x, b AS y FROM t1;
        SELECT * FROM v15 LIMIT 1;
    ]], {
        -- <view-15.1>
        "X", 2, "Y", 3
        -- </view-15.1>
    })

test:do_execsql2_test(
    "view-15.2",
    [[
        SELECT x, y FROM v15 LIMIT 1
    ]], {
        -- <view-15.2>
        "X", 2, "Y", 3
        -- </view-15.2>
    })

test:do_catchsql_test(
    "view-16.1",
    [[
        CREATE VIEW IF NOT EXISTS v1 AS SELECT * FROM t1;
    ]], {
        -- <view-16.1>
        0
        -- </view-16.1>
    })

-- do not insert sql statement in sql_master
-- do_test view-16.2 {
--   execsql {
--     SELECT sql FROM sql_master WHERE name='v1'
--   }
-- } {{CREATE VIEW v1 AS SELECT a AS 'xyz', b+c AS 'pqr', c-b FROM t1}}
test:do_catchsql_test(
    "view-16.3",
    [[
        DROP VIEW IF EXISTS nosuchview
    ]], {
        -- <view-16.3>
        0
        -- </view-16.3>
    })

-- correct error message when attempting to drop a view that does not
-- exist.
--
test:do_catchsql_test(
    "view-17.1",
    [[
        DROP VIEW nosuchview
    ]], {
        -- <view-17.1>
        1, "Space 'NOSUCHVIEW' does not exist"
        -- </view-17.1>
    })

test:do_catchsql_test(
    "view-17.2",
    [[
        DROP VIEW main.nosuchview
    ]], {
        -- <view-17.2>
        1, "Space 'MAIN' does not exist"
        -- </view-17.2>
    })

-- MUST_WORK_TEST use drop table for delete problem
if (0 > 0)
 then
    test:do_execsql_test(
        "view-18.1",
        [[
            DROP VIEW t1;
            DROP TABLE t1;
            CREATE TABLE t1(a INT, b INT, c INT);
            INSERT INTO t1 VALUES(1, 2, 3);
            INSERT INTO t1 VALUES(4, 5, 6);

            CREATE VIEW vv1 AS SELECT * FROM t1;
            CREATE VIEW vv2 AS SELECT * FROM vv1;
            CREATE VIEW vv3 AS SELECT * FROM vv2;
            CREATE VIEW vv4 AS SELECT * FROM vv3;
            CREATE VIEW vv5 AS SELECT * FROM vv4;

            SELECT * FROM vv5;
        ]], {
            -- <view-18.1>
            1, 2, 3, 4, 5, 6
            -- </view-18.1>
        })
end
----Ticket #3308"]]=])
---- Make sure rowid columns in a view are named correctly.
---- we do not work with rowid#
--test:do_test(
--    "view-19.1",
--    function()
--        test:execsql [[
--            CREATE VIEW v3308a AS SELECT rowid, * FROM t1;
--        ]]
--        return test:execsql2 [[
--            SELECT * FROM v3308a
--        ]]
--    end, {
--        -- <view-19.1>
--        "rowid", 1, "a", 1, "b", 2, "c", 3, "rowid", 2, "a", 4, "b", 5, "c", 6
--        -- </view-19.1>
--    })
--
--test:do_test(
--    "view-19.2",
--    function()
--        test:execsql [[
--            CREATE VIEW v3308b AS SELECT t1.rowid, t1.a, t1.b+t1.c FROM t1;
--        ]]
--        return test:execsql2 [[
--            SELECT * FROM v3308b
--        ]]
--    end, {
--        -- <view-19.2>
--        "rowid", 1, "a", 1, "t1.b+t1.c", 5, "rowid", 2, "a", 4, "t1.b+t1.c", 11
--        -- </view-19.2>
--    })
--
--test:do_test(
--    "view-19.3",
--    function()
--        test:execsql [[
--            CREATE VIEW v3308c AS SELECT t1.oid, A, t1.b+t1.c AS x FROM t1;
--        ]]
--        return test:execsql2 [[
--            SELECT * FROM v3308c
--        ]]
--    end, {
--        -- <view-19.3>
--        "rowid", 1, "a", 1, "x", 5, "rowid", 2, "a", 4, "x", 11
--        -- </view-19.3>
--    })

-- Ticket #3539 had this crashing (see commit 5940)
test:do_execsql_test(
    "view-20.1",
    [[
        DROP VIEW v10;
        CREATE TABLE t10(c1 INT primary key);
        CREATE VIEW v10 AS SELECT c1 FROM (SELECT t10.c1 FROM t10);
    ]], {
        -- <view-20.1>

        -- </view-20.1>
    })

test:do_execsql_test(
    "view-20.1",
    [[
        DROP VIEW IF EXISTS v10;
        DROP TABLE IF EXISTS t10;
        CREATE TABLE t10(c1 INT primary key);
        CREATE VIEW v10 AS SELECT c1 FROM (SELECT t10.c1 FROM t10);
    ]], {
        -- <view-20.1>

        -- </view-20.1>
    })

-- MUST_WORK_TEST Table.nRef overflow
if (0 > 0)
 then
    -- Ticket #d58ccbb3f1b"]],":"],"Prevent","Table.nRef","overflow.
    test:execsql([[
        drop view v1;
        drop view v2;
        drop view v4;
        drop view v8;
        drop table   t1;
    ]])
    test:do_catchsql_test(
        "view-21.1",
        [[
            CREATE TABLE t1(x INT primary key);
            INSERT INTO t1 VALUES(5);
            CREATE VIEW v1 AS SELECT x*2 FROM t1;
            CREATE VIEW v2 AS SELECT * FROM v1 UNION SELECT * FROM v1;
            CREATE VIEW v4 AS SELECT * FROM v2 UNION SELECT * FROM v2;
            CREATE VIEW v8 AS SELECT * FROM v4 UNION SELECT * FROM v4;
            CREATE VIEW v16 AS SELECT * FROM v8 UNION SELECT * FROM v8;
            CREATE VIEW v32 AS SELECT * FROM v16 UNION SELECT * FROM v16;
            CREATE VIEW v64 AS SELECT * FROM v32 UNION SELECT * FROM v32;
            CREATE VIEW v128 AS SELECT * FROM v64 UNION SELECT * FROM v64;
            CREATE VIEW v256 AS SELECT * FROM v128 UNION SELECT * FROM v128;
            CREATE VIEW v512 AS SELECT * FROM v256 UNION SELECT * FROM v256;
            CREATE VIEW v1024 AS SELECT * FROM v512 UNION SELECT * FROM v512;
            CREATE VIEW v2048 AS SELECT * FROM v1024 UNION SELECT * FROM v1024;
            CREATE VIEW v4096 AS SELECT * FROM v2048 UNION SELECT * FROM v2048;
            CREATE VIEW v8192 AS SELECT * FROM v4096 UNION SELECT * FROM v4096;
            CREATE VIEW v16384 AS SELECT * FROM v8192 UNION SELECT * FROM v8192;
            CREATE VIEW v32768 AS SELECT * FROM v16384 UNION SELECT * FROM v16384;
            SELECT * FROM v32768 UNION SELECT * FROM v32768;
        ]], {
            -- <view-21.1>
            1, [[too many references to "v1": max 65535]]
            -- </view-21.1>
        })

    test:do_test(
        "view-21.2",
        function()
            --db("progress", 1000, "expr 1")
            return test:catchsql [[
                SELECT * FROM v32768;
            ]]
        end, {
            -- <view-21.2>
            1, "interrupted"
            -- </view-21.2>
        })



    test:do_execsql_test(
        "view-22.1",
        [[
            CREATE VIEW x1 AS SELECT 123 AS "", 234 AS "", 345 AS "";
            SELECT * FROM x1;
        ]], {
            -- <view-22.1>
            123, 234, 345
            -- </view-22.1>
        })

    -- Legacy from the original code. Must be replaced with analogue
    -- functions from box.
    local X = nil
    test:do_test(
        "view-22.2",
        function()
            -- ["unset","-nocomplain","x"]
            test:execsql("SELECT * FROM x1 x break")
            return X(797, "X!cmd", [=[["lsort",[["array","names","x"]]]]=])
        end, {
            -- <view-22.2>
            "", "*", ":1", ":2"
            -- </view-22.2>
        })

end

-- Make sure that VIEW with several internal selects works.
test:do_catchsql_test(
    "view-23.1",
    [[
        CREATE TABLE t11(a INT PRIMARY KEY);
        CREATE TABLE t12(b INT PRIMARY KEY);
        CREATE TABLE t13(c INT PRIMARY KEY);
        CREATE VIEW v11 AS SELECT * FROM
            (SELECT a FROM (SELECT a, b FROM t11, t12)),
            (SELECT * FROM (SELECT a, c FROM t11, t13));
    ]], {
        -- <view-23.1>
        0
        -- </view-23.1>
    })

test:do_catchsql_test(
    "view-23.2",
    [[
        DROP TABLE t11;
    ]], {
        -- <view-23.2>
        1, "Can't drop space 'T11': other views depend on this space"
        -- </view-23.2>
    })

test:do_catchsql_test(
    "view-23.3",
    [[
        DROP TABLE t12;
    ]], {
        -- <view-23.3>
        1, "Can't drop space 'T12': other views depend on this space"
        -- </view-23.3>
    })

test:do_catchsql_test(
    "view-23.4",
    [[
        DROP TABLE t13;
    ]], {
        -- <view-23.4>
        1, "Can't drop space 'T13': other views depend on this space"
        -- </view-23.4>
    })

test:do_catchsql_test(
    "view-23.5",
    [[
        DROP VIEW v11;
    ]], {
        -- <view-23.5>
        0
        -- </view-23.5>
    })

test:do_catchsql_test(
    "view-23.6",
    [[
        DROP TABLE t11;
    ]], {
        -- <view-23.6>
        0
        -- </view-23.6>
    })

test:do_catchsql_test(
    "view-23.7",
    [[
        DROP TABLE t12;
    ]], {
        -- <view-23.7>
        0
        -- </view-23.7>
    })

test:do_catchsql_test(
    "view-23.8",
    [[
        DROP TABLE t13;
    ]], {
        -- <view-23.8>
        0
        -- </view-23.8>
    })

-- gh-4149: Make sure that VIEW can be created as CTE.
test:do_execsql_test(
    "view-24.1",
    [[
        CREATE TABLE ts (s1 INT PRIMARY KEY);
        INSERT INTO ts VALUES (1);
    ]], {
        -- <view-24.1>
        -- </view-24.1>
    })

test:do_execsql_test(
    "view-24.2",
    [[
        CREATE VIEW v AS WITH w(id) AS (
            SELECT 1)
          SELECT * FROM ts, w;
        SELECT * FROM v;
    ]], {
        -- <view-24.2>
        1, 1
        -- </view-24.2>
    })

test:do_execsql_test(
    "view-24.3",
    [[
        DROP VIEW v;
        CREATE VIEW v AS WITH RECURSIVE w AS (
            SELECT s1 FROM ts
            UNION ALL
            SELECT s1+1 FROM w WHERE s1 < 4)
          SELECT * FROM w;
        SELECT * FROM v;
    ]], {
        -- <view-24.3>
        1,2,3,4
        -- </view-24.3>
    })

test:do_execsql_test(
    "view-24.4",
    [[
        DROP VIEW v;
        CREATE VIEW v AS SELECT * FROM
            (SELECT 1),
            (SELECT 2) JOIN
            (WITH RECURSIVE w AS (
                SELECT s1 FROM ts
                UNION ALL
                SELECT s1+1 FROM w WHERE s1 < 4)
              SELECT * FROM w);
          SELECT * FROM v;
    ]], {
        -- <view-24.4>
        1,2,1,1,2,2,1,2,3,1,2,4
        -- </view-24.4>
    })

test:do_execsql_test(
    "view-24.5",
    [[
        DROP VIEW v;
        DROP TABLE ts;
        CREATE VIEW v AS WITH RECURSIVE
            xaxis(x) AS (
                VALUES(-2.0)
                UNION ALL
                SELECT x+0.5 FROM xaxis WHERE x<1.2),
            yaxis(y) AS (
                VALUES(-1.0)
                UNION ALL
                SELECT y+0.5 FROM yaxis WHERE y<1.0),
            m(iter, cx, cy, x, y) AS (
                SELECT 0, x, y, 0.0, 0.0 FROM xaxis, yaxis
                UNION ALL
                SELECT iter+1, cx, cy, x*x-y*y + cx, 2.0*x*y + cy FROM m WHERE (x*x + y*y) < 4.0 AND iter<1
            ),
            m2(iter, cx, cy) AS (
            SELECT max(iter), cx, cy FROM m GROUP BY cx, cy
            ),
            a(t) AS (
                SELECT group_concat( substr('a', 1+least(iter/7,4), 1), '') FROM m2 GROUP BY cy
            )
          SELECT group_concat(trim(t),x'0a') FROM a;
        SELECT * FROM v;
    ]], {
        -- <view-24.5>
        "aaaaaaaa\naaaaaaaa\naaaaaaaa\naaaaaaaa\naaaaaaaa"
        -- </view-24.5>
    })

test:finish_test()
