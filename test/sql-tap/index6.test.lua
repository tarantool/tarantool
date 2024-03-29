#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(14)

--!./tcltestrunner.lua
-- 2013-07-31
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
-- Test cases for partial indices
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


--X(26, "X!cmd", [=[["load_static_extension","db","wholenumber"]]=])
-- do_test index6-1.1 {
--   # Able to parse and manage partial indices
--   execsql {
--     CREATE TABLE t1(a INT ,b INT ,c INT );
--     CREATE INDEX t1a ON t1(a) WHERE a IS NOT NULL;
--     CREATE INDEX t1b ON t1(b) WHERE b>10;
--     CREATE VIRTUAL TABLE nums USING wholenumber;
--     INSERT INTO t1(a,b,c)
--        SELECT CASE WHEN value%3!=0 THEN value END, value, value
--          FROM nums WHERE value<=20;
--     SELECT count(a), count(b) FROM t1;
--   }
-- } {14 20 ok}
-- # Make sure the count(*) optimization works correctly with
-- # partial indices.  Ticket [a5c8ed66cae16243be6] 2013-10-03.
-- #
-- do_execsql_test index6-1.1.1 {
--   SELECT count(*) FROM t1;
-- } {20}
-- # Error conditions during parsing...
-- #
-- do_test index6-1.2 {
--   catchsql {
--     CREATE INDEX bad1 ON t1(a,b) WHERE x IS NOT NULL;
--   }
-- } {1 {no such column: x}}
-- do_test index6-1.3 {
--   catchsql {
--     CREATE INDEX bad1 ON t1(a,b) WHERE EXISTS(SELECT * FROM t1);
--   }
-- } {1 {subqueries prohibited in partial index WHERE clauses}}
-- do_test index6-1.4 {
--   catchsql {
--     CREATE INDEX bad1 ON t1(a,b) WHERE a!=?1;
--   }
-- } {1 {parameters prohibited in partial index WHERE clauses}}
-- do_test index6-1.5 {
--   catchsql {
--     CREATE INDEX bad1 ON t1(a,b) WHERE a!=random();
--   }
-- } {1 {functions prohibited in partial index WHERE clauses}}
-- do_test index6-1.6 {
--   catchsql {
--     CREATE INDEX bad1 ON t1(a,b) WHERE a NOT LIKE 'abc%';
--   }
-- } {1 {functions prohibited in partial index WHERE clauses}}
-- do_test index6-1.10 {
--   execsql {
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--   }
-- } {{} 20 t1a {14 1} t1b {10 1} ok}
-- # STAT1 shows the partial indices have a reduced number of
-- # rows.
-- #
-- do_test index6-1.11 {
--   execsql {
--     UPDATE t1 SET a=b;
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--   }
-- } {{} 20 t1a {20 1} t1b {10 1} ok}
-- do_test index6-1.11 {
--   execsql {
--     UPDATE t1 SET a=NULL WHERE b%3!=0;
--     UPDATE t1 SET b=b+100;
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--   }
-- } {{} 20 t1a {6 1} t1b {20 1} ok}
-- do_test index6-1.12 {
--   execsql {
--     UPDATE t1 SET a=CASE WHEN b%3!=0 THEN b END;
--     UPDATE t1 SET b=b-100;
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--   }
-- } {{} 20 t1a {13 1} t1b {10 1} ok}
-- do_test index6-1.13 {
--   execsql {
--     DELETE FROM t1 WHERE b BETWEEN 8 AND 12;
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--   }
-- } {{} 15 t1a {10 1} t1b {8 1} ok}
-- do_test index6-1.15 {
--   execsql {
--     CREATE INDEX t1c ON t1(c);
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--   }
-- } {t1a {10 1} t1b {8 1} t1c {15 1} ok}
-- # Queries use partial indices as appropriate times.
-- #
-- do_test index6-2.1 {
--   execsql {
--     CREATE TABLE t2(a INT ,b INT );
--     INSERT INTO t2(a,b) SELECT value, value FROM nums WHERE value<1000;
--     UPDATE t2 SET a=NULL WHERE b%2==0;
--     CREATE INDEX t2a1 ON t2(a) WHERE a IS NOT NULL;
--     SELECT count(*) FROM t2 WHERE a IS NOT NULL;
--   }
-- } {500}
-- do_test index6-2.2 {
--   execsql {
--     EXPLAIN QUERY PLAN
--     SELECT * FROM t2 WHERE a=5;
--   }
-- } {/.* TABLE t2 USING INDEX t2a1 .*/}
-- ifcapable stat4||stat3 {
--   execsql ANALYZE
--   do_test index6-2.3stat4 {
--     execsql {
--       EXPLAIN QUERY PLAN
--       SELECT * FROM t2 WHERE a IS NOT NULL;
--     }
--   } {/.* TABLE t2 USING INDEX t2a1 .*/}
-- } else {
--   do_test index6-2.3stat4 {
--     execsql {
--       EXPLAIN QUERY PLAN
--       SELECT * FROM t2 WHERE a IS NOT NULL AND a>0;
--     }
--   } {/.* TABLE t2 USING INDEX t2a1 .*/}
-- }
-- do_test index6-2.4 {
--   execsql {
--     EXPLAIN QUERY PLAN
--     SELECT * FROM t2 WHERE a IS NULL;
--   }
-- } {~/.*INDEX t2a1.*/}
-- do_execsql_test index6-2.101 {
--   DROP INDEX t2a1;
--   UPDATE t2 SET a=b, b=b+10000;
--   SELECT b FROM t2 WHERE a=15;
-- } {10015}
-- do_execsql_test index6-2.102 {
--   CREATE INDEX t2a2 ON t2(a) WHERE a<100 OR a>200;
--   SELECT b FROM t2 WHERE a=15;
-- } {10015 ok}
-- do_execsql_test index6-2.102eqp {
--   EXPLAIN QUERY PLAN
--   SELECT b FROM t2 WHERE a=15;
-- } {~/.*INDEX t2a2.*/}
-- do_execsql_test index6-2.103 {
--   SELECT b FROM t2 WHERE a=15 AND a<100;
-- } {10015}
-- do_execsql_test index6-2.103eqp {
--   EXPLAIN QUERY PLAN
--   SELECT b FROM t2 WHERE a=15 AND a<100;
-- } {/.*INDEX t2a2.*/}
-- do_execsql_test index6-2.104 {
--   SELECT b FROM t2 WHERE a=515 AND a>200;
-- } {10515}
-- do_execsql_test index6-2.104eqp {
--   EXPLAIN QUERY PLAN
--   SELECT b FROM t2 WHERE a=515 AND a>200;
-- } {/.*INDEX t2a2.*/}
-- # Partial UNIQUE indices
-- #
-- do_execsql_test index6-3.1 {
--   CREATE TABLE t3(a INT ,b INT );
--   INSERT INTO t3 SELECT value, value FROM nums WHERE value<200;
--   UPDATE t3 SET a=999 WHERE b%5!=0;
--   CREATE UNIQUE INDEX t3a ON t3(a) WHERE a<>999;
-- } {}
-- do_test index6-3.2 {
--   # unable to insert a duplicate row a-value that is not 999.
--   catchsql {
--     INSERT INTO t3(a,b) VALUES(150, 'test1');
--   }
-- } {1 {UNIQUE constraint failed: t3.a}}
-- do_test index6-3.3 {
--   # can insert multiple rows with a==999 because such rows are not
--   # part of the unique index.
--   catchsql {
--     INSERT INTO t3(a,b) VALUES(999, 'test1'), (999, 'test2');
--   }
-- } {0 {}}
-- # Silently ignore database name qualifiers in partial indices.
-- #
-- do_execsql_test index6-5.0 {
--   CREATE INDEX t3b ON t3(b) WHERE xyzzy.t3.b BETWEEN 5 AND 10;
--                                /* ^^^^^-- ignored */
--   ANALYZE;
--   SELECT count(*) FROM t3 WHERE t3.b BETWEEN 5 AND 10;
--   SELECT stat+0 FROM sql_stat1 WHERE idx='t3b';
-- } {6 6}
-- Test case for ticket [2ea3e9fe6379fc3f6ce7e090ce483c1a3a80d6c9] from
-- 2014-04-13: Partial index causes assertion fault on UPDATE OR REPLACE.
--
-- MUST_WORK_TEST #2311 partial index
if 0>0 then
test:do_execsql_test(
    "index6-6.0",
    [[
        CREATE TABLE t6(a,b, PRIMARY KEY (a,b));
        CREATE INDEX t6b ON t6(b);
        INSERT INTO t6(a,b) VALUES(123,456);
        SELECT * FROM t6;
    ]], {
        -- <index6-6.0>
        123, 456
        -- </index6-6.0>
    })
else
    test:execsql("CREATE TABLE t6(id INT PRIMARY KEY AUTOINCREMENT, a INT ,b INT);")
    test:execsql("CREATE UNIQUE INDEX t6i1 ON t6(a, b);")
    test:execsql("INSERT INTO t6(a,b) VALUES(123,456);")
end

test:do_execsql_test(
    "index6-6.1",
    [[
        UPDATE OR REPLACE t6 SET b=789;
        SELECT a,b FROM t6;
    ]], {
        -- <index6-6.1>
        123, 789
        -- </index6-6.1>
    })

-- do_execsql_test index6-6.2 {
-- } {ok}
-- Test case for ticket [2326c258d02ead33d69faa63de8f4686b9b1b9d9] on
-- 2015-02-24.  Any use of a partial index qualifying constraint inside
-- the ON clause of a LEFT JOIN was causing incorrect results for all
-- versions of sql 3.8.0 through 3.8.8.
--
test:do_execsql_test(
    "index6-7.0",
    [[
        CREATE TABLE t7a(id  INT primary key, x INT );
        CREATE TABLE t7b(id  INT primary key, y INT );
        INSERT INTO t7a VALUES(1, 1);
        CREATE INDEX t7ax ON t7a(x);
        SELECT x,y FROM t7a LEFT JOIN t7b ON (x=99) ORDER BY x;
    ]], {
        -- <index6-7.0>
        1, ""
        -- </index6-7.0>
    })

test:do_execsql_test(
    "index6-7.1",
    [[
        INSERT INTO t7b VALUES(1, 2);
        SELECT x,y FROM t7a JOIN t7b ON (x=99) ORDER BY x;
    ]], {
        -- <index6-7.1>

        -- </index6-7.1>
    })

test:do_execsql_test(
    "index6-7.2",
    [[
        INSERT INTO t7a VALUES(2, 99);
        SELECT x,y FROM t7a LEFT JOIN t7b ON (x=99) ORDER BY x;
    ]], {
        -- <index6-7.2>
        1, "", 99, 2
        -- </index6-7.2>
    })

test:do_execsql_test(
    "index6-7.3",
    [[
        SELECT x,y FROM t7a JOIN t7b ON (x=99) ORDER BY x;
    ]], {
        -- <index6-7.3>
        99, 2
        -- </index6-7.3>
    })

test:do_execsql_test(
    "index6-7.4",
    [[
        EXPLAIN QUERY PLAN
        SELECT x,y FROM t7a JOIN t7b ON (x=99) ORDER BY x;
    ]], {
        -- <index6-7.4>
        "/USING COVERING INDEX t7ax/"
        -- </index6-7.4>
    })

test:do_execsql_test(
    "index6-8.0",
    [[
        CREATE TABLE t8a(id INT primary key, a INT,b TEXT);
        CREATE TABLE t8b(id INT primary key, x TEXT,y INT);
        CREATE INDEX i8c ON t8b(y);

        INSERT INTO t8a VALUES(1, 1, 'one');
        INSERT INTO t8a VALUES(2, 2, 'two');
        INSERT INTO t8a VALUES(3, 3, 'three');

        INSERT INTO t8b VALUES(1, 'value', 1);
        INSERT INTO t8b VALUES(2, 'dummy', 2);
        INSERT INTO t8b VALUES(3, 'value', 3);
        INSERT INTO t8b VALUES(4, 'dummy', 4);
    ]], {
        -- <index6-8.0>

        -- </index6-8.0>
    })

test:do_eqp_test(
    "index6-8.1",
    [[
        SELECT * FROM t8a LEFT JOIN t8b ON (x = 'value' AND y = a)
    ]], {
        -- <index6-8.1>
    {0, 0, 0, "SCAN TABLE t8a (~1048576 rows)"},
    {0, 1, 1, "SEARCH TABLE t8b USING COVERING INDEX i8c (y=?) (~9 rows)"}
        -- </index6-8.1>
    })

test:do_execsql_test(
    "index6-8.2",
    [[
        SELECT a,b,x,y FROM t8a LEFT JOIN t8b ON (x = 'value' AND y = a)
    ]], {
        -- <index6-8.2>
        1, "one", "value", 1, 2, "two", "", "", 3, "three", "value", 3
        -- </index6-8.2>
    })

-- MUST_WORK_TEST
if (0 > 0)
 then
    -- 2015-06-11.  Assertion fault found by AFL
    --
    test:do_execsql_test(
        "index6-9.1",
        [[
            CREATE TABLE t9(a int, b int, c int);
            CREATE INDEX t9ca ON t9(c,a);
            INSERT INTO t9 VALUES(1,1,9),(10,2,35),(11,15,82),(20,19,5),(NULL,7,3);
            UPDATE t9 SET b=c WHERE a in (10,12,20);
            SELECT a,b,c,'|' FROM t9 ORDER BY a;
        ]], {
            -- <index6-9.1>
            "", 7, 3, "|", 1, 1, 9, "|", 10, 35, 35, "|", 11, 15, 82, "|", 20, 5, 5, "|"
            -- </index6-9.1>
        })

test:do_execsql_test(
    "index6-9.2",
    [[
        --DROP TABLE t9;
        CREATE TABLE t9(a int, b int, c int, PRIMARY KEY(a));
        CREATE INDEX t9ca ON t9(c,a);
        INSERT INTO t9 VALUES(1,1,9),(10,2,35),(11,15,82),(20,19,5);
        UPDATE t9 SET b=c WHERE a in (10,12,20);
        SELECT a,b,c,'|' FROM t9 ORDER BY a;
    ]], {
        -- <index6-9.2>
        1, 1, 9, "|", 10, 35, 35, "|", 11, 15, 82, "|", 20, 5, 5, "|"
        -- </index6-9.2>
    })
end
-- AND-connected terms in the WHERE clause of a partial index
--
test:do_execsql_test(
    "index6-10.1",
    [[
        CREATE TABLE t10(a INT ,b INT ,c INT ,d INT ,e INTEGER PRIMARY KEY);
        INSERT INTO t10 VALUES
          (1,2,3,4,5),
          (2,3,4,5,6),
          (3,4,5,6,7),
          (1,2,3,8,9);
        CREATE INDEX t10x ON t10(d);
        SELECT e FROM t10 WHERE a=1 AND b=2 AND c=3 ORDER BY d;
    ]], {
        -- <index6-10.1>
        5, 9
        -- </index6-10.1>
    })

test:do_execsql_test(
    "index6-10.1eqp",
    [[
        EXPLAIN QUERY PLAN
        SELECT e FROM t10 WHERE a=1 AND b=2 AND c=3 ORDER BY d;
    ]], {
        -- <index6-10.1eqp>
        "/USING COVERING INDEX t10x/"
        -- </index6-10.1eqp>
    })

test:do_execsql_test(
    "index6-10.2",
    [[
        SELECT e FROM t10 WHERE c=3 AND 2=b AND a=1 ORDER BY d DESC;
    ]], {
        -- <index6-10.2>
        9, 5
        -- </index6-10.2>
    })

test:do_execsql_test(
    "index6-10.2eqp",
    [[
        EXPLAIN QUERY PLAN
        SELECT e FROM t10 WHERE c=3 AND 2=b AND a=1 ORDER BY d DESC;
    ]], {
        -- <index6-10.2eqp>
        "/USING COVERING INDEX t10x/"
        -- </index6-10.2eqp>
    })

test:do_execsql_test(
    "index6-10.3",
    [[
        SELECT e FROM t10 WHERE a=1 AND b=2 ORDER BY d DESC;
    ]], {
        -- <index6-10.3>
        9, 5
        -- </index6-10.3>
    })

-- MUST_WORK_TEST why did plan change?
if 0>0 then
test:do_execsql_test(
    "index6-10.3eqp",
    [[
        EXPLAIN QUERY PLAN
        SELECT e FROM t10 WHERE a=1 AND b=2 ORDER BY d DESC;
    ]], {
        -- <index6-10.3eqp>
        "~/USING", "INDEX", "t10x/"
        -- </index6-10.3eqp>
    })
end

test:finish_test()
