#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(113)

--!./tcltestrunner.lua
-- 2001 November 6
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
-- focus of this file is testing the LIMIT ... OFFSET ... clause
--  of SELECT statements.
--
-- $Id: limit.test,v 1.32 2008/08/02 03:50:39 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Build some test data
--
test:execsql [[
    CREATE TABLE t1(id INT primary key, x int, y int);
    START TRANSACTION;
]]
for i=1,32 do
    --for _ in X(0, "X!for", [=[["set i 1","$i<=32","incr i"]]=]) do
    local j = 0
    while bit.lshift(1, j) < i do
    -- for _ in X(0, "X!for", [=[["set j 0","(1<<$j)<$i","incr j"]]=]) do
        j = j + 1
    end
    test:execsql(string.format("INSERT INTO t1 VALUES(%s, %s,%s)", i * 32 + j, (32 - i), (10 - j)))
end
test:execsql [[
    COMMIT;
]]
test:do_execsql_test(
    "limit-1.0",
    [[
        SELECT count(*) FROM t1
    ]], {
        -- <limit-1.0>
        32
        -- </limit-1.0>
    })

test:do_execsql_test(
    "limit-1.1",
    [[
        SELECT count(*) FROM t1 LIMIT  5
    ]], {
        -- <limit-1.1>
        32
        -- </limit-1.1>
    })

test:do_execsql_test(
    "limit-1.2.1",
    [[
        SELECT x FROM t1 ORDER BY x LIMIT 5
    ]], {
        -- <limit-1.2.1>
        0, 1, 2, 3, 4
        -- </limit-1.2.1>
    })

test:do_execsql_test(
    "limit-1.2.2",
    [[
        SELECT x FROM t1 ORDER BY x LIMIT 5 OFFSET 2
    ]], {
        -- <limit-1.2.2>
        2, 3, 4, 5, 6
        -- </limit-1.2.2>
    })

test:do_catchsql_test(
    "limit-1.2.3",
    [[
        SELECT x FROM t1 ORDER BY x+1 LIMIT 5 OFFSET -2
    ]], {
        -- <limit-1.2.13>
        1 ,"Failed to execute SQL statement: Only positive integers are allowed in the OFFSET clause"
        -- </limit-1.2.13>
    })

test:do_catchsql_test(
    "limit-1.2.4",
    [[
        SELECT x FROM t1 ORDER BY x+1 LIMIT 2, -5
    ]], {
        -- <limit-1.2.4>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
        -- </limit-1.2.4>
    })

test:do_execsql_test(
    "limit-1.2.5",
    [[
        SELECT x FROM t1 ORDER BY x+1 LIMIT 2, 1000
    ]], {
    -- <limit-1.2.5>
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    -- </limit-1.2.5>
})

test:do_catchsql_test(
    "limit-1.2.6",
    [[
        SELECT x FROM t1 ORDER BY x+1 LIMIT -2, 5
    ]], {
        -- <limit-1.2.6>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the OFFSET clause"
        -- </limit-1.2.6>
    })

test:do_execsql_test(
    "limit-1.2.7",
    [[
        SELECT x FROM t1 ORDER BY x+1 LIMIT 0, 5
    ]], {
    -- <limit-1.2.7>
    0, 1, 2, 3, 4
    -- </limit-1.2.7>
})

test:do_catchsql_test(
    "limit-1.2.8",
    [[
        SELECT x FROM t1 ORDER BY x+1 LIMIT -2, -5
    ]], {
        -- <limit-1.2.8>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
        -- </limit-1.2.8>
    })

test:do_execsql_test(
    "limit-1.2.9",
    [[
        SELECT x FROM t1 ORDER BY x LIMIT 2, 5
    ]], {
        -- <limit-1.2.9>
        2, 3, 4, 5, 6
        -- </limit-1.2.9>
    })

test:do_execsql_test(
    "limit-1.3",
    [[
        SELECT x FROM t1 ORDER BY x LIMIT 5 OFFSET 5
    ]], {
        -- <limit-1.3>
        5, 6, 7, 8, 9
        -- </limit-1.3>
    })

test:do_execsql_test(
    "limit-1.4.1",
    [[
        SELECT x FROM t1 ORDER BY x LIMIT 50 OFFSET 30
    ]], {
        -- <limit-1.4.1>
        30, 31
        -- </limit-1.4.1>
    })

test:do_execsql_test(
    "limit-1.4.2",
    [[
        SELECT x FROM t1 ORDER BY x LIMIT 30, 50
    ]], {
        -- <limit-1.4.2>
        30, 31
        -- </limit-1.4.2>
    })

test:do_execsql_test(
    "limit-1.5",
    [[
        SELECT x FROM t1 ORDER BY x LIMIT 50 OFFSET 50
    ]], {
        -- <limit-1.5>
        
        -- </limit-1.5>
    })

test:do_execsql_test(
    "limit-1.6",
    [[
        SELECT a.x,a.y,b.x,b.y FROM t1 AS a, t1 AS b ORDER BY a.x, b.x LIMIT 5
    ]], {
        -- <limit-1.6>
        0, 5, 0, 5, 0, 5, 1, 5, 0, 5, 2, 5, 0, 5, 3, 5, 0, 5, 4, 5
        -- </limit-1.6>
    })

test:do_execsql_test(
    "limit-1.7",
    [[
        SELECT a.x,a.y,b.x,b.y FROM t1 AS a, t1 AS b ORDER BY a.x, b.x LIMIT 5 OFFSET 32
    ]], {
        -- <limit-1.7>
        1, 5, 0, 5, 1, 5, 1, 5, 1, 5, 2, 5, 1, 5, 3, 5, 1, 5, 4, 5
        -- </limit-1.7>
    })

test:do_execsql_test(
    "limit-2.1",
    [[
        CREATE VIEW v1 AS SELECT x,y FROM t1 LIMIT 2;
        SELECT count(*) FROM (SELECT * FROM v1);
    ]], {
        -- <limit-2.1>
        2
        -- </limit-2.1>
    })


-- ifcapable view
-- do_test limit-2.2 {
--   execsql {
--     CREATE TABLE t2 AS SELECT * FROM t1 LIMIT 2;
--     SELECT count(*) FROM t2;
--   }
-- } 2
-- ifcapable subquery {
--   do_test limit-2.3 {
--     execsql {
--       SELECT count(*) FROM t1 WHERE rowid IN (SELECT rowid FROM t1 LIMIT 2);
--     }
--   } 2
-- }
test:do_execsql_test(
    "limit-3.1",
    [[
        SELECT z FROM (SELECT y*10+x AS z FROM t1 ORDER BY x LIMIT 10)
        ORDER BY z LIMIT 5;
    ]], {
        -- <limit-3.1>
        50, 51, 52, 53, 54
        -- </limit-3.1>
    })



test:do_test(
    "limit-4.1",
    function()
        return test:execsql [[
            CREATE TABLE t3(x INT primary KEY);
            START TRANSACTION;
            INSERT INTO t3 SELECT x FROM t1 ORDER BY x LIMIT 10 OFFSET 1;
            INSERT INTO t3 SELECT x+(SELECT max(x) FROM t3) FROM t3;
            INSERT INTO t3 SELECT x+(SELECT max(x) FROM t3) FROM t3;
            INSERT INTO t3 SELECT x+(SELECT max(x) FROM t3) FROM t3;
            INSERT INTO t3 SELECT x+(SELECT max(x) FROM t3) FROM t3;
            INSERT INTO t3 SELECT x+(SELECT max(x) FROM t3) FROM t3;
            INSERT INTO t3 SELECT x+(SELECT max(x) FROM t3) FROM t3;
            INSERT INTO t3 SELECT x+(SELECT max(x) FROM t3) FROM t3;
            INSERT INTO t3 SELECT x+(SELECT max(x) FROM t3) FROM t3;
            INSERT INTO t3 SELECT x+(SELECT max(x) FROM t3) FROM t3;
            INSERT INTO t3 SELECT x+(SELECT max(x) FROM t3) FROM t3;
            COMMIT;
            SELECT count(*) FROM t3;
        ]]


    end, {
        -- <limit-4.1>
        10240
        -- </limit-4.1>
    })

test:do_execsql_test(
    "limit-4.2",
    [[
        SELECT x FROM t3 LIMIT 2 OFFSET 10000
    ]], {
        -- <limit-4.2>
        10001, 10002
        -- </limit-4.2>
    })

-- do_test limit-4.3 {
--   execsql {
--     CREATE TABLE t4 AS SELECT x,
--        'abcdefghijklmnopqrstuvwyxz ABCDEFGHIJKLMNOPQRSTUVWYXZ' || x ||
--        'abcdefghijklmnopqrstuvwyxz ABCDEFGHIJKLMNOPQRSTUVWYXZ' || x ||
--        'abcdefghijklmnopqrstuvwyxz ABCDEFGHIJKLMNOPQRSTUVWYXZ' || x ||
--        'abcdefghijklmnopqrstuvwyxz ABCDEFGHIJKLMNOPQRSTUVWYXZ' || x ||
--        'abcdefghijklmnopqrstuvwyxz ABCDEFGHIJKLMNOPQRSTUVWYXZ' || x AS y
--     FROM t3 LIMIT 1000;
--     SELECT x FROM t4 ORDER BY y DESC LIMIT 1 OFFSET 999;
--   }
-- } {1000}
test:do_execsql_test(
    "limit-5.1",
    [[
        CREATE TABLE t5(id INT primary key, x INT, y INT);
        INSERT INTO t5 SELECT id, x-y, x+y FROM t1 WHERE x BETWEEN 10 AND 15
            ORDER BY x LIMIT 2;
        SELECT x, y FROM t5 ORDER BY x;
    ]], {
        -- <limit-5.1>
        5, 15, 6, 16
        -- </limit-5.1>
    })

test:do_execsql_test(
    "limit-5.2",
    [[
        DELETE FROM t5;
        INSERT INTO t5 SELECT id, x-y, x+y FROM t1 WHERE x BETWEEN 10 AND 15
            ORDER BY x DESC LIMIT 2;
        SELECT x, y FROM t5 ORDER BY x;
    ]], {
        -- <limit-5.2>
        9, 19, 10, 20
        -- </limit-5.2>
    })

test:do_execsql_test(
    "limit-5.3",
    [[
        DELETE FROM t5;
        INSERT INTO t5 SELECT id, x-y, x+y FROM t1 WHERE x <> 0 ORDER BY x DESC LIMIT 31;
        SELECT x, y FROM t5 ORDER BY x LIMIT 2;
    ]], {
        -- <limit-5.3>
        -4, 6, -3, 7
        -- </limit-5.3>
    })

test:do_execsql_test(
    "limit-5.4",
    [[
        SELECT x, y FROM t5 ORDER BY x DESC, y DESC LIMIT 2;
    ]], {
        -- <limit-5.4>
        21, 41, 21, 39
        -- </limit-5.4>
    })

test:do_execsql_test(
    "limit-5.5",
    [[
        DELETE FROM t5;
        INSERT INTO t5 SELECT a.id + 1000 * b.id, a.x*100+b.x, a.y*100+b.y FROM t1 AS a, t1 AS b
                       ORDER BY 2, 3 LIMIT 1000;
        SELECT count(*), sum(x), sum(y), min(x), max(x), min(y), max(y) FROM t5;
    ]], {
        -- <limit-5.5>
        1000, 1528204, 593161, 0, 3107, 505, 1005
        -- </limit-5.5>
    })

-- There is some contraversy about whether LIMIT 0 should be the same as
-- no limit at all or if LIMIT 0 should result in zero output rows.
--
test:do_execsql_test(
    "limit-6.1",
    [[
        CREATE TABLE t6(a INT primary key);
        START TRANSACTION;
        INSERT INTO t6 VALUES(1);
        INSERT INTO t6 VALUES(2);
        INSERT INTO t6 SELECT a+2 FROM t6;
        COMMIT;
        SELECT * FROM t6;
    ]], {
        -- <limit-6.1>
        1, 2, 3, 4
        -- </limit-6.1>
    })

test:do_catchsql_test(
    "limit-6.2",
    [[
        SELECT * FROM t6 LIMIT -1 OFFSET -1;
    ]], {
        -- <limit-6.2>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
        -- </limit-6.2>
    })

test:do_catchsql_test(
    "limit-6.3.1",
    [[
        SELECT * FROM t6 LIMIT 2 OFFSET -123;
    ]], {
        -- <limit-6.3>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the OFFSET clause"
        -- </limit-6.3>
    })

test:do_execsql_test(
    "limit-6.3.2",
    [[
        SELECT * FROM t6 LIMIT 2 OFFSET 0;
    ]], {
    -- <limit-6.3>
    1, 2
    -- </limit-6.3>
})

test:do_catchsql_test(
    "limit-6.4.1",
    [[
        SELECT * FROM t6 LIMIT -432 OFFSET 2;
    ]], {
        -- <limit-6.4>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
        -- </limit-6.4>
    })

test:do_execsql_test(
    "limit-6.4.2",
    [[
        SELECT * FROM t6 LIMIT 1000 OFFSET 2;
    ]], {
    -- <limit-6.4>
    3, 4
    -- </limit-6.4>
})

test:do_catchsql_test(
    "limit-6.5.1",
    [[
        SELECT * FROM t6 LIMIT -1
    ]], {
        -- <limit-6.5>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
        -- </limit-6.5>
    })

test:do_execsql_test(
    "limit-6.5.2",
    [[
        SELECT * FROM t6 LIMIT 12
    ]], {
    -- <limit-6.5>
    1, 2, 3, 4
    -- </limit-6.5>
})

test:do_catchsql_test(
    "limit-6.6.1",
    [[
        SELECT * FROM t6 LIMIT -1 OFFSET 1
    ]], {
        -- <limit-6.6>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
        -- </limit-6.6>
    })

test:do_execsql_test(
    "limit-6.6.2",
    [[
        SELECT * FROM t6 LIMIT 111 OFFSET 1
    ]], {
    -- <limit-6.6>
    2, 3, 4
    -- </limit-6.6>
})

test:do_execsql_test(
    "limit-6.7",
    [[
        SELECT * FROM t6 LIMIT 0
    ]], {
        -- <limit-6.7>
        
        -- </limit-6.7>
    })

test:do_execsql_test(
    "limit-6.8",
    [[
        SELECT * FROM t6 LIMIT 0 OFFSET 1
    ]], {
        -- <limit-6.8>
        
        -- </limit-6.8>
    })

-- # Make sure LIMIT works well with compound SELECT statements.
-- # Ticket #393
-- #
-- # EVIDENCE-OF: R-13512-64012 In a compound SELECT, only the last or
-- # right-most simple SELECT may contain a LIMIT clause.
-- #
-- # EVIDENCE-OF: R-03782-50113 In a compound SELECT, the LIMIT clause
-- # applies to the entire compound, not just the final SELECT.
-- #
-- ifcapable compound {
-- do_test limit-7.1.1 {
--   catchsql {
--     SELECT x FROM t2 LIMIT 5 UNION ALL SELECT a FROM t6;
--   }
-- } {1 {LIMIT clause should come after UNION ALL not before}}
-- do_test limit-7.1.2 {
--   catchsql {
--     SELECT x FROM t2 LIMIT 5 UNION SELECT a FROM t6;
--   }
-- } {1 {LIMIT clause should come after UNION not before}}
-- do_test limit-7.1.3 {
--   catchsql {
--     SELECT x FROM t2 LIMIT 5 EXCEPT SELECT a FROM t6 LIMIT 3;
--   }
-- } {1 {LIMIT clause should come after EXCEPT not before}}
-- do_test limit-7.1.4 {
--   catchsql {
--     SELECT x FROM t2 LIMIT 0,5 INTERSECT SELECT a FROM t6;
--   }
-- } {1 {LIMIT clause should come after INTERSECT not before}}
-- do_test limit-7.2 {
--   execsql {
--     SELECT x FROM t2 UNION ALL SELECT a FROM t6 LIMIT 5;
--   }
-- } {31 30 1 2 3}
-- do_test limit-7.3 {
--   execsql {
--     SELECT x FROM t2 UNION ALL SELECT a FROM t6 LIMIT 3 OFFSET 1;
--   }
-- } {30 1 2}
-- do_test limit-7.4 {
--   execsql {
--     SELECT x FROM t2 UNION ALL SELECT a FROM t6 ORDER BY 1 LIMIT 3 OFFSET 1;
--   }
-- } {2 3 4}
-- do_test limit-7.5 {
--   execsql {
--     SELECT x FROM t2 UNION SELECT x+2 FROM t2 LIMIT 2 OFFSET 1;
--   }
-- } {31 32}
-- do_test limit-7.6 {
--   execsql {
--     SELECT x FROM t2 UNION SELECT x+2 FROM t2 ORDER BY 1 DESC LIMIT 2 OFFSET 1;
--   }
-- } {32 31}
-- do_test limit-7.7 {
--   execsql {
--     SELECT a+9 FROM t6 EXCEPT SELECT y FROM t2 LIMIT 2;
--   }
-- } {11 12}
-- do_test limit-7.8 {
--   execsql {
--     SELECT a+9 FROM t6 EXCEPT SELECT y FROM t2 ORDER BY 1 DESC LIMIT 2;
--   }
-- } {13 12}
-- do_test limit-7.9 {
--   execsql {
--     SELECT a+26 FROM t6 INTERSECT SELECT x FROM t2 LIMIT 1;
--   }
-- } {30}
-- do_test limit-7.10 {
--   execsql {
--     SELECT a+27 FROM t6 INTERSECT SELECT x FROM t2 LIMIT 1;
--   }
-- } {30}
-- do_test limit-7.11 {
--   execsql {
--     SELECT a+27 FROM t6 INTERSECT SELECT x FROM t2 LIMIT 1 OFFSET 1;
--   }
-- } {31}
-- do_test limit-7.12 {
--   execsql {
--     SELECT a+27 FROM t6 INTERSECT SELECT x FROM t2 
--        ORDER BY 1 DESC LIMIT 1 OFFSET 1;
--   }
-- } {30}
-- } ;# ifcapable compound
-- Tests for limit in conjunction with distinct.  The distinct should
-- occur before both the limit and the offset.  Ticket #749.
--
test:do_execsql_test(
    "limit-8.1",
    [[
        SELECT DISTINCT cast(round(x/100) as integer) FROM t3 LIMIT 5;
    ]], {
        -- <limit-8.1>
        0, 1, 2, 3, 4
        -- </limit-8.1>
    })

test:do_execsql_test(
    "limit-8.2",
    [[
        SELECT DISTINCT cast(round(x/100) as integer) FROM t3 LIMIT 5 OFFSET 5;
    ]], {
        -- <limit-8.2>
        5, 6, 7, 8, 9
        -- </limit-8.2>
    })

test:do_execsql_test(
    "limit-8.3",
    [[
        SELECT DISTINCT cast(round(x/100) as integer) FROM t3 LIMIT 5 OFFSET 25;
    ]], {
        -- <limit-8.3>
        25, 26, 27, 28, 29
        -- </limit-8.3>
    })

-- Make sure limits on multiple subqueries work correctly.
-- Ticket #1035
--
test:do_execsql_test(
    "limit-9.1",
    [[
        SELECT * FROM (SELECT * FROM t6 LIMIT 3);
    ]], {
        -- <limit-9.1>
        1, 2, 3
        -- </limit-9.1>
    })



test:do_execsql_test(
    "limit-9.2.1",
    [[
        --CREATE TABLE t7 AS SELECT * FROM t6;
        CREATE TABLE t7 (a INT primary key);
        INSERT INTO t7 SELECT * FROM t6;
    ]], {
        -- <limit-9.2.1>
        
        -- </limit-9.2.1>
    })

test:do_execsql_test(
    "limit-9.2.2",
    [[
        SELECT * FROM (SELECT * FROM t7 LIMIT 3);
    ]], {
        -- <limit-9.2.2>
        1, 2, 3
        -- </limit-9.2.2>
    })



test:do_execsql_test(
    "limit-9.3",
    [[
        SELECT * FROM (SELECT * FROM t6 LIMIT 3)
        UNION
        SELECT * FROM (SELECT * FROM t7 LIMIT 3)
        ORDER BY 1
    ]], {
        -- <limit-9.3>
        1, 2, 3
        -- </limit-9.3>
    })

test:do_execsql_test(
    "limit-9.4",
    [[
        SELECT * FROM (SELECT * FROM t6 LIMIT 3)
        UNION
        SELECT * FROM (SELECT * FROM t7 LIMIT 3)
        ORDER BY 1
        LIMIT 2
    ]], {
        -- <limit-9.4>
        1, 2
        -- </limit-9.4>
    })



test:do_catchsql_test(
    "limit-9.5",
    [[
        SELECT * FROM t6 LIMIT 3
        UNION
        SELECT * FROM t7 LIMIT 3
    ]], {
        -- <limit-9.5>
        1, "LIMIT clause should come after UNION not before"
        -- </limit-9.5>
    })



-- Test LIMIT and OFFSET using SQL variables.
test:do_test(
    "limit-10.1",
    function()
        local limit = 10
        return test:execsql [[
            SELECT x FROM t1 LIMIT 10;
        ]]
    end, {
        -- <limit-10.1>
        31, 30, 29, 28, 27, 26, 25, 24, 23, 22
        -- </limit-10.1>
    })

test:do_test(
    "limit-10.2",
    function()
        local limit = 5
        local offset = 5
        return test:execsql("SELECT x FROM t1 LIMIT "..limit.." OFFSET "..offset)
    end, {
        -- <limit-10.2>
        26, 25, 24, 23, 22
        -- </limit-10.2>
    })

test:do_test(
    "limit-10.3",
    function()
        local limit = 111
        return test:execsql("SELECT x FROM t1 WHERE x<10 LIMIT "..limit)
    end, {
    -- <limit-10.3.2>
    9, 8, 7, 6, 5, 4, 3, 2, 1, 0
    -- </limit-10.3.2>
})

test:do_test(
    "limit-10.4",
    function()
        local limit = 1.5
        return test:catchsql("SELECT x FROM t1 WHERE x<10 LIMIT "..limit)
    end, {
        -- <limit-10.4>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
        -- </limit-10.4>
    })

test:do_test(
    "limit-10.5",
    function()
        local limit = "'hello world'"
        return test:catchsql("SELECT x FROM t1 WHERE x<10 LIMIT "..limit)
    end, {
        -- <limit-10.5>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
        -- </limit-10.5>
    })

test:do_test(
    "limit-11.1",
    function()
        return test:execsql [[
            SELECT x FROM (SELECT x FROM t1 ORDER BY x LIMIT 0) ORDER BY x
        ]]
    end, {
        -- <limit-11.1>
        
        -- </limit-11.1>
    })


-- ifcapable subquery
-- Test error processing.
--
test:do_catchsql_test(
    "limit-12.1",
    [[
        SELECT * FROM t1 LIMIT replace(1)
    ]], {
        -- <limit-12.1>
        1, "Wrong number of arguments is passed to REPLACE(): expected 3, got 1"
        -- </limit-12.1>
    })

test:do_catchsql_test(
    "limit-12.2",
    [[
        SELECT * FROM t1 LIMIT 5 OFFSET replace(1)
    ]], {
        -- <limit-12.2>
        1, 'Wrong number of arguments is passed to REPLACE(): expected 3, got 1'
        -- </limit-12.2>
    })

test:do_catchsql_test(
    "limit-12.3",
    [[
        SELECT * FROM t1 LIMIT x
    ]], {
        -- <limit-12.3>
        1, "Can’t resolve field 'X'"
        -- </limit-12.3>
    })

test:do_catchsql_test(
    "limit-12.4",
    [[
        SELECT * FROM t1 LIMIT 1 OFFSET x
    ]], {
        -- <limit-12.4>
        1, "Can’t resolve field 'X'"
        -- </limit-12.4>
    })

-- Ticket [db4d96798da8b]
-- LIMIT does not work with nested views containing UNION ALL 
--
test:do_test(
    "limit-13.1",
    function()
        return test:execsql [[
            CREATE TABLE t13(x INT primary key);
            INSERT INTO t13 VALUES(1),(2);
            CREATE VIEW v13a AS SELECT x AS y FROM t13;
            CREATE VIEW v13b AS SELECT y AS z FROM v13a UNION ALL SELECT y+10 FROM v13a;
            CREATE VIEW v13c AS SELECT z FROM v13b UNION ALL SELECT z+20 FROM v13b;
        ]]
    end, {
        -- <limit-13.1>
        
        -- </limit-13.1>
    })

test:do_test(
    "limit-13.2",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 1"
    end, {
        -- <limit-13.2>
        1
        -- </limit-13.2>
    })

test:do_test(
    "limit-13.3",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 2"
    end, {
        -- <limit-13.3>
        1, 2
        -- </limit-13.3>
    })

test:do_test(
    "limit-13.4",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 3"
    end, {
        -- <limit-13.4>
        1, 2, 11
        -- </limit-13.4>
    })

test:do_test(
    "limit-13.5",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 4"
    end, {
        -- <limit-13.5>
        1, 2, 11, 12
        -- </limit-13.5>
    })

test:do_test(
    "limit-13.6",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 5"
    end, {
        -- <limit-13.6>
        1, 2, 11, 12, 21
        -- </limit-13.6>
    })

test:do_test(
    "limit-13.7",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 6"
    end, {
        -- <limit-13.7>
        1, 2, 11, 12, 21, 22
        -- </limit-13.7>
    })

test:do_test(
    "limit-13.8",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 7"
    end, {
        -- <limit-13.8>
        1, 2, 11, 12, 21, 22, 31
        -- </limit-13.8>
    })

test:do_test(
    "limit-13.9",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 8"
    end, {
        -- <limit-13.9>
        1, 2, 11, 12, 21, 22, 31, 32
        -- </limit-13.9>
    })

test:do_test(
    "limit-13.10",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 9"
    end, {
        -- <limit-13.10>
        1, 2, 11, 12, 21, 22, 31, 32
        -- </limit-13.10>
    })

test:do_test(
    "limit-13.11",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 1 OFFSET 1"
    end, {
        -- <limit-13.11>
        2
        -- </limit-13.11>
    })

test:do_test(
    "limit-13.12",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 2 OFFSET 1"
    end, {
        -- <limit-13.12>
        2, 11
        -- </limit-13.12>
    })

test:do_test(
    "limit-13.13",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 3 OFFSET 1"
    end, {
        -- <limit-13.13>
        2, 11, 12
        -- </limit-13.13>
    })

test:do_test(
    "limit-13.14",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 4 OFFSET 1"
    end, {
        -- <limit-13.14>
        2, 11, 12, 21
        -- </limit-13.14>
    })

test:do_test(
    "limit-13.15",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 5 OFFSET 1"
    end, {
        -- <limit-13.15>
        2, 11, 12, 21, 22
        -- </limit-13.15>
    })

test:do_test(
    "limit-13.16",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 6 OFFSET 1"
    end, {
        -- <limit-13.16>
        2, 11, 12, 21, 22, 31
        -- </limit-13.16>
    })

test:do_test(
    "limit-13.17",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 7 OFFSET 1"
    end, {
        -- <limit-13.17>
        2, 11, 12, 21, 22, 31, 32
        -- </limit-13.17>
    })

test:do_test(
    "limit-13.18",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 8 OFFSET 1"
    end, {
        -- <limit-13.18>
        2, 11, 12, 21, 22, 31, 32
        -- </limit-13.18>
    })

test:do_test(
    "limit-13.21",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 1 OFFSET 2"
    end, {
        -- <limit-13.21>
        11
        -- </limit-13.21>
    })

test:do_test(
    "limit-13.22",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 2 OFFSET 2"
    end, {
        -- <limit-13.22>
        11, 12
        -- </limit-13.22>
    })

test:do_test(
    "limit-13.23",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 3 OFFSET 2"
    end, {
        -- <limit-13.23>
        11, 12, 21
        -- </limit-13.23>
    })

test:do_test(
    "limit-13.24",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 4 OFFSET 2"
    end, {
        -- <limit-13.24>
        11, 12, 21, 22
        -- </limit-13.24>
    })

test:do_test(
    "limit-13.25",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 5 OFFSET 2"
    end, {
        -- <limit-13.25>
        11, 12, 21, 22, 31
        -- </limit-13.25>
    })

test:do_test(
    "limit-13.26",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 6 OFFSET 2"
    end, {
        -- <limit-13.26>
        11, 12, 21, 22, 31, 32
        -- </limit-13.26>
    })

test:do_test(
    "limit-13.27",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 7 OFFSET 2"
    end, {
        -- <limit-13.27>
        11, 12, 21, 22, 31, 32
        -- </limit-13.27>
    })

test:do_test(
    "limit-13.31",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 1 OFFSET 3"
    end, {
        -- <limit-13.31>
        12
        -- </limit-13.31>
    })

test:do_test(
    "limit-13.32",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 2 OFFSET 3"
    end, {
        -- <limit-13.32>
        12, 21
        -- </limit-13.32>
    })

test:do_test(
    "limit-13.33",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 3 OFFSET 3"
    end, {
        -- <limit-13.33>
        12, 21, 22
        -- </limit-13.33>
    })

test:do_test(
    "limit-13.34",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 4 OFFSET 3"
    end, {
        -- <limit-13.34>
        12, 21, 22, 31
        -- </limit-13.34>
    })

test:do_test(
    "limit-13.35",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 5 OFFSET 3"
    end, {
        -- <limit-13.35>
        12, 21, 22, 31, 32
        -- </limit-13.35>
    })

test:do_test(
    "limit-13.36",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 6 OFFSET 3"
    end, {
        -- <limit-13.36>
        12, 21, 22, 31, 32
        -- </limit-13.36>
    })

test:do_test(
    "limit-13.41",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 1 OFFSET 4"
    end, {
        -- <limit-13.41>
        21
        -- </limit-13.41>
    })

test:do_test(
    "limit-13.42",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 2 OFFSET 4"
    end, {
        -- <limit-13.42>
        21, 22
        -- </limit-13.42>
    })

test:do_test(
    "limit-13.43",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 3 OFFSET 4"
    end, {
        -- <limit-13.43>
        21, 22, 31
        -- </limit-13.43>
    })

test:do_test(
    "limit-13.44",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 4 OFFSET 4"
    end, {
        -- <limit-13.44>
        21, 22, 31, 32
        -- </limit-13.44>
    })

test:do_test(
    "limit-13.45",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 5 OFFSET 4"
    end, {
        -- <limit-13.45>
        21, 22, 31, 32
        -- </limit-13.45>
    })

test:do_test(
    "limit-13.51",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 1 OFFSET 5"
    end, {
        -- <limit-13.51>
        22
        -- </limit-13.51>
    })

test:do_test(
    "limit-13.52",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 2 OFFSET 5"
    end, {
        -- <limit-13.52>
        22, 31
        -- </limit-13.52>
    })

test:do_test(
    "limit-13.53",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 3 OFFSET 5"
    end, {
        -- <limit-13.53>
        22, 31, 32
        -- </limit-13.53>
    })

test:do_test(
    "limit-13.54",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 4 OFFSET 5"
    end, {
        -- <limit-13.54>
        22, 31, 32
        -- </limit-13.54>
    })

test:do_test(
    "limit-13.61",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 1 OFFSET 6"
    end, {
        -- <limit-13.61>
        31
        -- </limit-13.61>
    })

test:do_test(
    "limit-13.62",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 2 OFFSET 6"
    end, {
        -- <limit-13.62>
        31, 32
        -- </limit-13.62>
    })

test:do_test(
    "limit-13.63",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 3 OFFSET 6"
    end, {
        -- <limit-13.63>
        31, 32
        -- </limit-13.63>
    })

test:do_test(
    "limit-13.71",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 1 OFFSET 7"
    end, {
        -- <limit-13.71>
        32
        -- </limit-13.71>
    })

test:do_test(
    "limit-13.72",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 2 OFFSET 7"
    end, {
        -- <limit-13.72>
        32
        -- </limit-13.72>
    })

test:do_test(
    "limit-13.81",
    function()
        return test:execsql "SELECT z FROM v13c LIMIT 1 OFFSET 8"
    end, {
        -- <limit-13.81>
        
        -- </limit-13.81>
    })

test:do_execsql_test(
    "limit-14.1",
    [[
        SELECT 123 LIMIT 1 OFFSET 0
    ]], {
        -- <limit-14.1>
        123
        -- </limit-14.1>
    })

test:do_execsql_test(
    "limit-14.2",
    [[
        SELECT 123 LIMIT 1 OFFSET 1
    ]], {
        -- <limit-14.2>
        
        -- </limit-14.2>
    })

test:do_execsql_test(
    "limit-14.3",
    [[
        SELECT 123 LIMIT 0 OFFSET 0
    ]], {
        -- <limit-14.3>
        
        -- </limit-14.3>
    })

test:do_execsql_test(
    "limit-14.4",
    [[
        SELECT 123 LIMIT 0 OFFSET 1
    ]], {
        -- <limit-14.4>
        
        -- </limit-14.4>
    })

test:do_catchsql_test(
    "limit-14.6.1",
    [[
        SELECT 123 LIMIT -1 OFFSET 0
    ]], {
        -- <limit-14.6.1>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
        -- </limit-14.6.1>
    })

test:do_execsql_test(
    "limit-14.6.2",
    [[
        SELECT 123 LIMIT 21 OFFSET 0
    ]], {
    -- <limit-14.6.2>
    123
    -- </limit-14.6.2>
})

test:do_catchsql_test(
    "limit-14.7.1",
    [[
        SELECT 123 LIMIT -1 OFFSET 1
    ]], {
        -- <limit-14.7.1>
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
        -- </limit-14.7.1>
    })

test:do_execsql_test(
    "limit-14.7.2",
    [[
        SELECT 123 LIMIT 111 OFFSET 1
    ]], {
    -- <limit-14.7.2>

    -- </limit-14.7.2>
})

-- Sum of LIMIT and OFFSET values should not cause integer overflow.
--
test:do_catchsql_test(
    "limit-15.1",
    [[
        SELECT 1 LIMIT 18446744073709551615 OFFSET 1;
    ]], { 1, "Failed to execute SQL statement: sum of LIMIT and OFFSET values should not result in integer overflow" } )

test:do_catchsql_test(
    "limit-15.2",
    [[
        SELECT 1 LIMIT 1 OFFSET 18446744073709551615;
    ]], { 1, "Failed to execute SQL statement: sum of LIMIT and OFFSET values should not result in integer overflow" } )

test:finish_test()
