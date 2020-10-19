#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(12)

--!./tcltestrunner.lua
-- 2013-11-04
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
-- Test cases for partial indices in WITHOUT ROWID tables
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- load_static_extension db wholenumber;
-- do_test index7-1.1 {
--   # Able to parse and manage partial indices
--   execsql {
--     CREATE TABLE t1(a INT,b INT,c INT PRIMARY KEY) WITHOUT rowid;
--     CREATE INDEX t1a ON t1(a) WHERE a IS NOT NULL;
--     CREATE INDEX t1b ON t1(b) WHERE b>10;
--     CREATE VIRTUAL TABLE nums USING wholenumber;
--     INSERT INTO t1(a,b,c)
--        SELECT CASE WHEN value%3!=0 THEN value END, value, value
--          FROM nums WHERE value<=20;
--     SELECT count(a), count(b) FROM t1;
--     PRAGMA integrity_check;
--   }
-- } {14 20 ok}
-- # (The "partial" column of the PRAGMA index_list output is...)
-- # EVIDENCE-OF: R-34457-09668 "1" if the index is a partial index and "0"
-- # if not.
-- #
-- do_test index7-1.1a {
--   capture_pragma db out {PRAGMA index_list(t1)}
--   db eval {SELECT "name", "partial", '|' FROM out ORDER BY "name"}
-- } {sql_autoindex_t1_1 0 | t1a 1 | t1b 1 |}
-- # Make sure the count(*) optimization works correctly with
-- # partial indices.  Ticket [a5c8ed66cae16243be6] 2013-10-03.
-- #
-- do_execsql_test index7-1.1.1 {
--   SELECT count(*) FROM t1;
-- } {20}
-- # Error conditions during parsing...
-- #
-- do_test index7-1.2 {
--   catchsql {
--     CREATE INDEX bad1 ON t1(a,b) WHERE x IS NOT NULL;
--   }
-- } {1 {no such column: x}}
-- do_test index7-1.3 {
--   catchsql {
--     CREATE INDEX bad1 ON t1(a,b) WHERE EXISTS(SELECT * FROM t1);
--   }
-- } {1 {subqueries prohibited in partial index WHERE clauses}}
-- do_test index7-1.4 {
--   catchsql {
--     CREATE INDEX bad1 ON t1(a,b) WHERE a!=?1;
--   }
-- } {1 {parameters prohibited in partial index WHERE clauses}}
-- do_test index7-1.5 {
--   catchsql {
--     CREATE INDEX bad1 ON t1(a,b) WHERE a!=random();
--   }
-- } {1 {functions prohibited in partial index WHERE clauses}}
-- do_test index7-1.6 {
--   catchsql {
--     CREATE INDEX bad1 ON t1(a,b) WHERE a NOT LIKE 'abc%';
--   }
-- } {1 {functions prohibited in partial index WHERE clauses}}
-- do_test index7-1.10 {
--   execsql {
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--     PRAGMA integrity_check;
--   }
-- } {t1 {20 1} t1a {14 1} t1b {10 1} ok}
-- # STAT1 shows the partial indices have a reduced number of
-- # rows.
-- #
-- do_test index7-1.11 {
--   execsql {
--     UPDATE t1 SET a=b;
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--     PRAGMA integrity_check;
--   }
-- } {t1 {20 1} t1a {20 1} t1b {10 1} ok}
-- do_test index7-1.11b {
--   execsql {
--     UPDATE t1 SET a=NULL WHERE b%3!=0;
--     UPDATE t1 SET b=b+100;
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--     PRAGMA integrity_check;
--   }
-- } {t1 {20 1} t1a {6 1} t1b {20 1} ok}
-- do_test index7-1.12 {
--   execsql {
--     UPDATE t1 SET a=CASE WHEN b%3!=0 THEN b END;
--     UPDATE t1 SET b=b-100;
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--     PRAGMA integrity_check;
--   }
-- } {t1 {20 1} t1a {13 1} t1b {10 1} ok}
-- do_test index7-1.13 {
--   execsql {
--     DELETE FROM t1 WHERE b BETWEEN 8 AND 12;
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--     PRAGMA integrity_check;
--   }
-- } {t1 {15 1} t1a {10 1} t1b {8 1} ok}
-- do_test index7-1.15 {
--   execsql {
--     CREATE INDEX t1c ON t1(c);
--     ANALYZE;
--     SELECT idx, stat FROM sql_stat1 ORDER BY idx;
--     PRAGMA integrity_check;
--   }
-- } {t1 {15 1} t1a {10 1} t1b {8 1} t1c {15 1} ok}
-- # Queries use partial indices as appropriate times.
-- #
-- do_test index7-2.1 {
--   execsql {
--     CREATE TABLE t2(a INT,b INT PRIMARY KEY) without rowid;
--     INSERT INTO t2(a,b) SELECT value, value FROM nums WHERE value<1000;
--     UPDATE t2 SET a=NULL WHERE b%5==0;
--     CREATE INDEX t2a1 ON t2(a) WHERE a IS NOT NULL;
--     SELECT count(*) FROM t2 WHERE a IS NOT NULL;
--   }
-- } {800}
-- do_test index7-2.2 {
--   execsql {
--     EXPLAIN QUERY PLAN
--     SELECT * FROM t2 WHERE a=5;
--   }
-- } {/.* TABLE t2 USING COVERING INDEX t2a1 .*/}
-- ifcapable stat4||stat3 {
--   do_test index7-2.3stat4 {
--     execsql {
--       EXPLAIN QUERY PLAN
--       SELECT * FROM t2 WHERE a IS NOT NULL;
--     }
--   } {/.* TABLE t2 USING COVERING INDEX t2a1 .*/}
-- } else {
--   do_test index7-2.3stat4 {
--     execsql {
--       EXPLAIN QUERY PLAN
--       SELECT * FROM t2 WHERE a IS NOT NULL AND a>0;
--     }
--   } {/.* TABLE t2 USING COVERING INDEX t2a1 .*/}
-- }
-- do_test index7-2.4 {
--   execsql {
--     EXPLAIN QUERY PLAN
--     SELECT * FROM t2 WHERE a IS NULL;
--   }
-- } {~/.*INDEX t2a1.*/}
-- do_execsql_test index7-2.101 {
--   DROP INDEX t2a1;
--   UPDATE t2 SET a=b, b=b+10000;
--   SELECT b FROM t2 WHERE a=15;
-- } {10015}
-- do_execsql_test index7-2.102 {
--   CREATE INDEX t2a2 ON t2(a) WHERE a<100 OR a>200;
--   SELECT b FROM t2 WHERE a=15;
--   PRAGMA integrity_check;
-- } {10015 ok}
-- do_execsql_test index7-2.102eqp {
--   EXPLAIN QUERY PLAN
--   SELECT b FROM t2 WHERE a=15;
-- } {~/.*INDEX t2a2.*/}
-- do_execsql_test index7-2.103 {
--   SELECT b FROM t2 WHERE a=15 AND a<100;
-- } {10015}
-- do_execsql_test index7-2.103eqp {
--   EXPLAIN QUERY PLAN
--   SELECT b FROM t2 WHERE a=15 AND a<100;
-- } {/.*INDEX t2a2.*/}
-- do_execsql_test index7-2.104 {
--   SELECT b FROM t2 WHERE a=515 AND a>200;
-- } {10515}
-- do_execsql_test index7-2.104eqp {
--   EXPLAIN QUERY PLAN
--   SELECT b FROM t2 WHERE a=515 AND a>200;
-- } {/.*INDEX t2a2.*/}
-- # Partial UNIQUE indices
-- #
-- do_execsql_test index7-3.1 {
--   CREATE TABLE t3(a INT,b INT PRIMARY KEY) without rowid;
--   INSERT INTO t3 SELECT value, value FROM nums WHERE value<200;
--   UPDATE t3 SET a=999 WHERE b%5!=0;
--   CREATE UNIQUE INDEX t3a ON t3(a) WHERE a<>999;
-- } {}
-- do_test index7-3.2 {
--   # unable to insert a duplicate row a-value that is not 999.
--   catchsql {
--     INSERT INTO t3(a,b) VALUES(150, 'test1');
--   }
-- } {1 {UNIQUE constraint failed: t3.a}}
-- do_test index7-3.3 {
--   # can insert multiple rows with a==999 because such rows are not
--   # part of the unique index.
--   catchsql {
--     INSERT INTO t3(a,b) VALUES(999, 'test1'), (999, 'test2');
--   }
-- } {0 {}}
-- do_execsql_test index7-3.4 {
--   SELECT count(*) FROM t3 WHERE a=999;
-- } {162}
-- integrity_check index7-3.5
-- # Silently ignore database name qualifiers in partial indices.
-- #
-- do_execsql_test index7-5.0 {
--   CREATE INDEX t3b ON t3(b) WHERE xyzzy.t3.b BETWEEN 5 AND 10;
--                                /* ^^^^^-- ignored */
--   ANALYZE;
--   SELECT count(*) FROM t3 WHERE t3.b BETWEEN 5 AND 10;
--   SELECT stat+0 FROM sql_stat1 WHERE idx='t3b';
-- } {6 6}
-- Verify that the problem identified by ticket [98d973b8f5] has been fixed.
--
test:do_execsql_test(
    "index7-6.1",
    [[
        CREATE TABLE t5(id INT primary key, a INT, b TEXT);
        CREATE TABLE t4(id INT primary key, c TEXT, d TEXT);
        INSERT INTO t5 VALUES(1, 1, 'xyz');
        INSERT INTO t4 VALUES(1, 'abc', 'not xyz');
        SELECT a,b,c,d FROM (SELECT a,b FROM t5 WHERE a=1 AND b='xyz'), t4 WHERE c='abc';
    ]], {
        -- <index7-6.1>
        1, "xyz", "abc", "not xyz"
        -- </index7-6.1>
    })

test:do_execsql_test(
    "index7-6.2",
    [[
        CREATE INDEX i4 ON t4(c);
        SELECT a,b,c,d FROM (SELECT a,b FROM t5 WHERE a=1 AND b='xyz'), t4 WHERE c='abc';
    ]], {
        -- <index7-6.2>
        1, "xyz", "abc", "not xyz"
        -- </index7-6.2>
    })

test:do_execsql_test(
    "index7-6.3",
    [[
        CREATE VIEW v4 AS SELECT c,d FROM t4;
        INSERT INTO t4 VALUES(2, 'def', 'xyz');
        SELECT * FROM v4 WHERE d='xyz' AND c='def'
    ]], {
        -- <index7-6.3>
        "def", "xyz"
        -- </index7-6.3>
    })

test:do_eqp_test(
    "index7-6.4",
    [[
        SELECT * FROM v4 WHERE d='xyz' AND c='def'
    ]], {
        -- <index7-6.4>
    {0, 0, 0, "SEARCH TABLE T4 USING COVERING INDEX I4 (C=?) (~9 rows)"}
        -- </index7-6.4>
    })

-- gh-2165 Currently, Tarantool lacks support of partial indexes,
-- so temporary we removed processing of their syntax from parser.
--
test:do_execsql_test(
    "index7-7.1",
    [[
        CREATE TABLE t1 (a INTEGER PRIMARY KEY, b INTEGER);
    ]])

test:do_catchsql_test(
    "index7-7.1",
    [[
        CREATE UNIQUE INDEX i ON t1 (a) WHERE a = 3;
    ]], {
        1, "At line 1 at or near position 41: keyword 'WHERE' is reserved. Please use double quotes if 'WHERE' is an identifier."
    })

-- Currently, when a user tries to create index (or primary key,
-- since we implement them as indexes underhood) with duplicated
-- fields (like 'CREATE INDEX i1 ON t(a, a, a, a, b, c, b)')
-- tarantool would silently remove duplicated fields and
-- execute 'CREATE INDEX i1 ON t(a, b, c)'.
-- This test checks that duplicates are removed correctly.
--
test:do_catchsql_test(
        "index7-8.1",
        [[
            CREATE TABLE t(a INT,b INT,c INT, PRIMARY KEY(a));
            CREATE INDEX i1 ON t(a, a, b, c, c, b, b, b, c, b, c);
            pragma index_info(t.i1);
        ]],
        {0, {
            0, 0, 'A', 0, 'BINARY', 'integer',
            1, 1, 'B', 0, 'BINARY', 'integer',
            2, 2, 'C', 0, 'BINARY', 'integer',
        }}
)

-- There was the following bug:
-- > CREATE TABLE t1(a,b,c,d, PRIMARY KEY(a,a,a,b,c));
-- ...
-- > CREATE INDEX i1 ON t1(b,c,a,c)
-- ...
-- But index 'i1' was not actually created and no error was raised.
-- This test checks that this does not happen anymore (and index is
-- created successfully).
--
test:do_catchsql_test(
        "index7-8.2",
        [[
            CREATE TABLE test4(a INT,b INT, c INT, d INT, PRIMARY KEY(a,a,a,b,c));
            CREATE INDEX index1 on test4(b,c,a,c);
            SELECT "_index"."name"
            FROM "_index" JOIN "_space" WHERE
                "_index"."id" = "_space"."id" AND
                "_space"."name"='TEST4'       AND
                "_index"."name"='INDEX1';
        ]],
        {0, {'INDEX1'}})

-- This test checks that CREATE TABLE statement with PK constraint
-- and NON-NAMED UNIQUE constraint (declared on THE SAME COLUMNS)
-- creates only one index - for PK constraint.
--
test:do_catchsql_test(
        "index7-8.3",
        [[
            CREATE TABLE test5(a INT,b INT,c INT,d INT, PRIMARY KEY(a), UNIQUE(a));
            SELECT "_index"."name", "_index"."iid"
            FROM "_index" JOIN "_space" WHERE
                "_index"."id" = "_space"."id" AND
                "_space"."name"='TEST5';
        ]],
        {0, {"pk_unnamed_TEST5_1",0}})

-- This test checks that CREATE TABLE statement with PK constraint
-- and NAMED UNIQUE constraint (declared on THE SAME COLUMNS)
-- creates two indexes - for PK constraint and for UNIQUE
-- constraint.
--
test:do_catchsql_test(
        "index7-8.4",
        [[
            CREATE TABLE test6(a INT,b INT,c INT,d INT, PRIMARY KEY(a), CONSTRAINT c1 UNIQUE(a));
            SELECT "_index"."name", "_index"."iid"
            FROM "_index" JOIN "_space" WHERE
                "_index"."id" = "_space"."id" AND
                "_space"."name"='TEST6';
        ]],
        {0, {"pk_unnamed_TEST6_1",0,"C1",1}})

-- This test checks that CREATE TABLE statement with PK constraint
-- and UNIQUE constraint is executed correctly
-- (no matter if UNIQUE precedes PK or not).
--
test:do_catchsql_test(
        "index7-8.5",
        [[
            CREATE TABLE test7(a INT,b INT,c INT,d INT, UNIQUE(a), PRIMARY KEY(a));
            SELECT "_index"."name", "_index"."iid"
            FROM "_index" JOIN "_space" WHERE
                "_index"."id" = "_space"."id" AND
                "_space"."name"='TEST7';
        ]],
        {0, {"unique_unnamed_TEST7_1",0}})


-- This test is the same as previous, but with named UNIQUE
-- constraint.
--
test:do_catchsql_test(
        "index7-8.6",
        [[
            CREATE TABLE test8(a INT,b INT,c INT,d INT, CONSTRAINT c1 UNIQUE(a), PRIMARY KEY(a));
            SELECT "_index"."name", "_index"."iid"
            FROM "_index" JOIN "_space" WHERE
                "_index"."id" = "_space"."id" AND
                "_space"."name"='TEST8';
        ]],
        {0, {"pk_unnamed_TEST8_2",0,"C1",1}})

test:finish_test()
