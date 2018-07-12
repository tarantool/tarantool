#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(11)

--!./tcltestrunner.lua
-- 2009 December 16
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
-- This file implements tests to verify that ticket [31338dca7e] has been
-- fixed.  Ticket [31338dca7e] demonstrates problems with the OR-clause
-- optimization in joins where the WHERE clause is of the form
--
--     (x AND y) OR z
--
-- And the x and y subterms from from different tables of the join.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "tkt-31338-1.1",
    function()
        return test:execsql [[
            CREATE TABLE t1(x  INT primary key);
            CREATE TABLE t2(y  INT primary key);
            INSERT INTO t1 VALUES(111);
            INSERT INTO t1 VALUES(222);
            INSERT INTO t2 VALUES(333);
            INSERT INTO t2 VALUES(444);
            SELECT * FROM t1, t2
             WHERE (x=111 AND y!=444) OR x=222
             ORDER BY x, y;
        ]]
    end, {
        -- <tkt-31338-1.1>
        111, 333, 222, 333, 222, 444
        -- </tkt-31338-1.1>
    })

test:do_test(
    "tkt-31338-1.2",
    function()
        return test:execsql [[
            CREATE INDEX t1x ON t1(x);
            SELECT * FROM t1, t2
             WHERE (x=111 AND y!=444) OR x=222
             ORDER BY x, y;
        ]]
    end, {
        -- <tkt-31338-1.2>
        111, 333, 222, 333, 222, 444
        -- </tkt-31338-1.2>
    })

test:do_test(
    "tkt-31338-2.1",
    function()
        return test:execsql [[
            CREATE TABLE t3(v  INT primary key,w INT );
            CREATE TABLE t4(x  INT primary key,y INT );
            CREATE TABLE t5(z  INT primary key);
            INSERT INTO t3 VALUES(111,222);
            INSERT INTO t3 VALUES(333,444);
            INSERT INTO t4 VALUES(222,333);
            INSERT INTO t4 VALUES(444,555);
            INSERT INTO t5 VALUES(888);
            INSERT INTO t5 VALUES(999);

            SELECT * FROM t3, t4, t5
             WHERE (v=111 AND x=w AND z!=999) OR (v=333 AND x=444)
             ORDER BY v, w, x, y, z;
        ]]
    end, {
        -- <tkt-31338-2.1>
        111, 222, 222, 333, 888, 333, 444, 444, 555, 888, 333, 444, 444, 555, 999
        -- </tkt-31338-2.1>
    })

test:do_test(
    "tkt-31338-2.2",
    function()
        return test:execsql [[
            CREATE INDEX t3v ON t3(v);
            CREATE INDEX t4x ON t4(x);
             SELECT * FROM t3, t4, t5
              WHERE (v=111 AND x=w AND z!=999) OR (v=333 AND x=444)
              ORDER BY v, w, x, y, z;
        ]]
    end, {
        -- <tkt-31338-2.2>
        111, 222, 222, 333, 888, 333, 444, 444, 555, 888, 333, 444, 444, 555, 999
        -- </tkt-31338-2.2>
    })


test:execsql([[
    DROP TABLE t1;
    DROP TABLE t2;
    DROP TABLE t3;
    DROP TABLE t4;
    DROP TABLE t5;
]])
-- Ticket [2c2de252666662f5459904fc33a9f2956cbff23c]
--
test:do_test(
    "tkt-31338-3.1",
    function()
        -- foreach x [db eval {SELECT name FROM sqlite_master WHERE type='table'}] {
        --    db eval "DROP TABLE $x"
        -- }
        return test:execsql [[
            CREATE TABLE t1(a  INT primary key,b INT ,c INT ,d INT );
            CREATE TABLE t2(e  INT primary key,f INT );
            INSERT INTO t1 VALUES(1,2,3,4);
            INSERT INTO t2 VALUES(10,-8);
            CREATE INDEX t1a ON t1(a);
            CREATE INDEX t1b ON t1(b);
            CREATE TABLE t3(g  INT primary key);
            INSERT INTO t3 VALUES(4);
            CREATE TABLE t4(h  INT primary key);
            INSERT INTO t4 VALUES(5);

            SELECT * FROM t3 LEFT JOIN t1 ON d=g LEFT JOIN t4 ON c=h
             WHERE (a=1 AND h=4)
                 OR (b IN (
                       SELECT x FROM (SELECT e+f AS x, e FROM t2 ORDER BY 1 LIMIT 2)
                       GROUP BY e
                    ));
        ]]
    end, {
        -- <tkt-31338-3.1>
        4, 1, 2, 3, 4, ""
        -- </tkt-31338-3.1>
    })

test:do_test(
    "tkt-31338-3.2",
    function()
        return test:execsql [[
            SELECT * FROM t3 LEFT JOIN t1 ON d=g LEFT JOIN t4 ON c=h
             WHERE (a=1 AND h=4)
                 OR (b=2 AND b NOT IN (
                       SELECT x+1 FROM (SELECT e+f AS x, e FROM t2 ORDER BY 1 LIMIT 2)
                       GROUP BY e
                    ));
        ]]
    end, {
        -- <tkt-31338-3.2>
        4, 1, 2, 3, 4, ""
        -- </tkt-31338-3.2>
    })

test:do_test(
    "tkt-31338-3.3",
    function()
        return test:execsql [[
            SELECT * FROM t3 LEFT JOIN t1 ON d=g LEFT JOIN t4 ON c=h
             WHERE (+a=1 AND h=4)
                 OR (b IN (
                       SELECT x FROM (SELECT e+f AS x, e FROM t2 ORDER BY 1 LIMIT 2)
                       GROUP BY e
                    ));
        ]]
    end, {
        -- <tkt-31338-3.3>
        4, 1, 2, 3, 4, ""
        -- </tkt-31338-3.3>
    })

test:do_test(
    "tkt-31338-3.4",
    function()
        return test:execsql [[
            SELECT * FROM t3 LEFT JOIN t1 ON d=g LEFT JOIN t4 ON c=h
             WHERE (a=1 AND h=4)
                 OR (+b IN (
                       SELECT x FROM (SELECT e+f AS x, e FROM t2 ORDER BY 1 LIMIT 2)
                       GROUP BY e
                    ));
        ]]
    end, {
        -- <tkt-31338-3.4>
        4, 1, 2, 3, 4, ""
        -- </tkt-31338-3.4>
    })

-- MUST_WORK_TEST
if (1 > 0)
 then
    test:do_test(
        "tkt-31338-3.5",
        function()
            return test:execsql [[
                CREATE TABLE t5(a  INT primary key,b INT ,c INT ,d INT ,e INT ,f INT );
                CREATE TABLE t6(g  INT primary key,h INT );
                CREATE TRIGGER t6r AFTER INSERT ON t6 BEGIN
                  INSERT INTO t5    
                    SELECT * FROM t3 LEFT JOIN t1 ON d=g LEFT JOIN t4 ON c=h
                     WHERE (a=1 AND h=4)
                        OR (b IN (
                           SELECT x FROM (SELECT e+f AS x, e FROM t2 ORDER BY 1 LIMIT 2)
                           GROUP BY e
                        ));
                END;
                INSERT INTO t6 VALUES(88,99);
                SELECT * FROM t5;
            ]]
        end, {
            -- <tkt-31338-3.5>
            4, 1, 2, 3, 4, ""
            -- </tkt-31338-3.5>
        })

end
test:do_test(
    "tkt-31338-3.6",
    function()
        return test:execsql [[
            INSERT INTO t1 VALUES(2,4,3,4);
            INSERT INTO t1 VALUES(99,101,3,4);
            INSERT INTO t1 VALUES(98,97,3,4);
            SELECT * FROM t3 LEFT JOIN t1 ON d=g LEFT JOIN t4 ON c=h
             WHERE (a=1 AND h=4)
                 OR (b IN (
                       SELECT x+a FROM (SELECT e+f AS x, e FROM t2 ORDER BY 1 LIMIT 2)
                       GROUP BY e
                    ));
        ]]
    end, {
        -- <tkt-31338-3.6>
        4, 2, 4, 3, 4, "", 4, 99, 101, 3, 4, ""
        -- </tkt-31338-3.6>
    })

test:do_test(
    "tkt-31338-3.7",
    function()
        return test:execsql [[
            SELECT * FROM t3 LEFT JOIN t1 ON d=g LEFT JOIN t4 ON c=h
             WHERE (a=1 AND h=4)
                 OR (b IN (
                       SELECT x FROM (SELECT e+f+a AS x, e FROM t2 ORDER BY 1 LIMIT 2)
                       GROUP BY e
                    ));
        ]]
    end, {
        -- <tkt-31338-3.7>
        4, 2, 4, 3, 4, "", 4, 99, 101, 3, 4, ""
        -- </tkt-31338-3.7>
    })

test:finish_test()

