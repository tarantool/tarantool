#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(23)

--!./tcltestrunner.lua
-- 2013-09-05
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
-- Test cases for query planning decisions and the likely(), unlikely(), and
-- likelihood() functions.
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "whereG"
test:do_execsql_test(
    "whereG-1.0",
    [[
        CREATE TABLE composer(
          cid INT PRIMARY KEY,
          cname TEXT
        );
        CREATE TABLE album(
          aid INT PRIMARY KEY,
          aname TEXT
        );
        CREATE TABLE track(
          tid INTEGER PRIMARY KEY,
          cid INTEGER REFERENCES composer,
          aid INTEGER REFERENCES album,
          title TEXT
        );
        CREATE INDEX track_i1 ON track(cid);
        CREATE INDEX track_i2 ON track(aid);
        INSERT INTO composer VALUES(1, 'W. A. Mozart');
        INSERT INTO composer VALUES(2, 'Beethoven');
        INSERT INTO composer VALUES(3, 'Thomas Tallis');
        INSERT INTO composer VALUES(4, 'Joseph Hayden');
        INSERT INTO composer VALUES(5, 'Thomas Weelkes');
        INSERT INTO composer VALUES(6, 'J. S. Bach');
        INSERT INTO composer VALUES(7, 'Orlando Gibbons');
        INSERT INTO composer VALUES(8, 'Josquin des Prés');
        INSERT INTO composer VALUES(9, 'Byrd');
        INSERT INTO composer VALUES(10, 'Francis Poulenc');
        INSERT INTO composer VALUES(11, 'Mendelsshon');
        INSERT INTO composer VALUES(12, 'Zoltán Kodály');
        INSERT INTO composer VALUES(13, 'Handel');
        INSERT INTO album VALUES(100, 'Kodály: Missa Brevis');
        INSERT INTO album VALUES(101, 'Messiah');
        INSERT INTO album VALUES(102, 'Missa Brevis in D-, K.65');
        INSERT INTO album VALUES(103, 'The complete English anthems');
        INSERT INTO album VALUES(104, 'Mass in B Minor, BWV 232');
        INSERT INTO track VALUES(10005, 12, 100, 'Sanctus');
        INSERT INTO track VALUES(10007, 12, 100, 'Agnus Dei');
        INSERT INTO track VALUES(10115, 13, 101, 'Surely He Hath Borne Our Griefs');
        INSERT INTO track VALUES(10129, 13, 101, 'Since By Man Came Death');
        INSERT INTO track VALUES(10206, 1, 102, 'Agnus Dei');
        INSERT INTO track VALUES(10301, 3, 103, 'If Ye Love Me');
        INSERT INTO track VALUES(10402, 6, 104, 'Domine Deus');
        INSERT INTO track VALUES(10403, 6, 104, 'Qui tollis');
    ]], {
        -- <whereG-1.0>
        
        -- </whereG-1.0>
    })

test:do_eqp_test(
    "whereG-1.1",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE unlikely(cname LIKE '%bach%')
           AND composer.cid=track.cid
           AND album.aid=track.aid;
    ]], {
        -- <whereG-1.1>
        "/.*composer.*track.*album.*/"
        -- </whereG-1.1>
    })

test:do_execsql_test(
    "whereG-1.2",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE unlikely(cname LIKE '%bach%' COLLATE "unicode_ci")
           AND composer.cid=track.cid
           AND album.aid=track.aid;
    ]], {
        -- <whereG-1.2>
        "Mass in B Minor, BWV 232"
        -- </whereG-1.2>
    })


test:do_eqp_test(
    "whereG-1.3",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE likelihood(cname LIKE '%bach%', 0.5)
           AND composer.cid=track.cid
           AND album.aid=track.aid;
    ]], {
        -- <whereG-1.3>
        "/.*track.*composer.*album.*/"
        -- </whereG-1.3>
    })

test:do_execsql_test(
    "whereG-1.4",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE likelihood(cname LIKE '%bach%' COLLATE "unicode_ci", 0.5)
           AND composer.cid=track.cid
           AND album.aid=track.aid;
    ]], {
        -- <whereG-1.4>
        "Mass in B Minor, BWV 232"
        -- </whereG-1.4>
    })

test:do_eqp_test(
    "whereG-1.5",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE cname LIKE '%bach%'
           AND composer.cid=track.cid
           AND album.aid=track.aid;
    ]], {
        -- <whereG-1.5>
        "/.*track.*(composer.*album|album.*composer).*/"
        -- </whereG-1.5>
    })

test:do_execsql_test(
    "whereG-1.6",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE cname LIKE '%bach%' COLLATE "unicode_ci"
           AND composer.cid=track.cid
           AND album.aid=track.aid;
    ]], {
        -- <whereG-1.6>
        "Mass in B Minor, BWV 232"
        -- </whereG-1.6>
    })

test:do_eqp_test(
    "whereG-1.7",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE cname LIKE '%bach%'
           AND unlikely(composer.cid=track.cid)
           AND unlikely(album.aid=track.aid);
    ]], {
        -- <whereG-1.7>
        "/.*track.*(composer.*album|album.*composer).*/"
        -- </whereG-1.7>
    })

test:do_execsql_test(
    "whereG-1.8",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE cname LIKE '%bach%' COLLATE "unicode_ci"
           AND unlikely(composer.cid=track.cid)
           AND unlikely(album.aid=track.aid);
    ]], {
        -- <whereG-1.8>
        "Mass in B Minor, BWV 232"
        -- </whereG-1.8>
    })

test:do_catchsql_test(
    "whereG-2.1",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE likelihood(cname LIKE '%bach%', -0.01)
           AND composer.cid=track.cid
           AND album.aid=track.aid;
    ]], {
        -- <whereG-2.1>
        1, "Illegal parameters, second argument to likelihood() must be a constant between 0.0 and 1.0"
        -- </whereG-2.1>
    })

test:do_catchsql_test(
    "whereG-2.2",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE likelihood(cname LIKE '%bach%', 1.01)
           AND composer.cid=track.cid
           AND album.aid=track.aid;
    ]], {
        -- <whereG-2.2>
        1, "Illegal parameters, second argument to likelihood() must be a constant between 0.0 and 1.0"
        -- </whereG-2.2>
    })

test:do_catchsql_test(
    "whereG-2.3",
    [[
        SELECT DISTINCT aname
          FROM album, composer, track
         WHERE likelihood(cname LIKE '%bach%', track.cid)
           AND composer.cid=track.cid
           AND album.aid=track.aid;
    ]], {
        -- <whereG-2.3>
        1, "Illegal parameters, second argument to likelihood() must be a constant between 0.0 and 1.0"
        -- </whereG-2.3>
    })

-- Commuting a term of the WHERE clause should not change the query plan
--
test:do_execsql_test(
    "whereG-3.0",
    [[
        CREATE TABLE a(a1  INT PRIMARY KEY, a2 INT );
        CREATE TABLE b(b1  INT PRIMARY KEY, b2 INT );
    ]], {
        -- <whereG-3.0>
        
        -- </whereG-3.0>
    })

-- do_eqp_test whereG-3.1 {
--   SELECT * FROM a, b WHERE b1=a1 AND a2=5;
-- } {/.*SCAN TABLE a.*SEARCH TABLE b USING INDEX .*b_1 .b1=..*/}
-- do_eqp_test whereG-3.2 {
--   SELECT * FROM a, b WHERE a1=b1 AND a2=5;
-- } {/.*SCAN TABLE a.*SEARCH TABLE b USING INDEX .*b_1 .b1=..*/}
-- do_eqp_test whereG-3.3 {
--   SELECT * FROM a, b WHERE a2=5 AND b1=a1;
-- } {/.*SCAN TABLE a.*SEARCH TABLE b USING INDEX .*b_1 .b1=..*/}
-- do_eqp_test whereG-3.4 {
--   SELECT * FROM a, b WHERE a2=5 AND a1=b1;
-- } {/.*SCAN TABLE a.*SEARCH TABLE b USING INDEX .*b_1 .b1=..*/}
-- Ticket [1e64dd782a126f48d78c43a664844a41d0e6334e]:
-- Incorrect result in a nested GROUP BY/DISTINCT due to the use of an OP_SCopy
-- where an OP_Copy was needed.
--
test:do_execsql_test(
    "whereG-4.0",
    [[
        CREATE TABLE t4(x TEXT PRIMARY key);
        INSERT INTO t4 VALUES('right'),('wrong');
        SELECT DISTINCT x
         FROM (SELECT x FROM t4 GROUP BY x)
         WHERE x='right'
         ORDER BY x;
    ]], {
        -- <whereG-4.0>
        "right"
        -- </whereG-4.0>
    })

---------------------------------------------------------------------------
-- Test that likelihood() specifications on indexed terms are taken into 
-- account by various forms of loops.
--
--   5.1.*: open ended range scans
--   5.2.*: skip-scans
--
-- reset_db
test:do_execsql_test(
    "5.1",
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a INT , b INT , c INT , PRIMARY KEY (a,b));
    ]])

-- do_eqp_test 5.1.2 {
--   SELECT * FROM t1 WHERE a>?
-- } {0 0 0 {SEARCH TABLE t1 USING INDEX i1 (a>?)}}
-- do_eqp_test 5.1.3 {
--   SELECT * FROM t1 WHERE likelihood(a>?, 0.9)
-- } {0 0 0 {SCAN TABLE t1}}
-- do_eqp_test 5.1.4 {
--   SELECT * FROM t1 WHERE likely(a>?)
-- } {0 0 0 {SCAN TABLE t1}}
-- do_test 5.2 {
--   for {set i 0} {$i < 100} {incr i} {
--     execsql { INSERT INTO t1 VALUES('abc', $i, $i); }
--   }
--   execsql { INSERT INTO t1 SELECT 'def', b, c FROM t1; }
--   execsql { ANALYZE }
-- } {}
-- do_eqp_test 5.2.2 {
--   SELECT * FROM t1 WHERE likelihood(b>?, 0.01)
-- } {0 0 0 {SEARCH TABLE t1 USING INDEX i1 (ANY(a) AND b>?)}}
-- do_eqp_test 5.2.3 {
--   SELECT * FROM t1 WHERE likelihood(b>?, 0.9)
-- } {0 0 0 {SCAN TABLE t1}}
-- do_eqp_test 5.2.4 {
--   SELECT * FROM t1 WHERE likely(b>?)
-- } {0 0 0 {SCAN TABLE t1}}
-- do_eqp_test 5.3.1 {
--   SELECT * FROM t1 WHERE a=?
-- } {0 0 0 {SEARCH TABLE t1 USING INDEX i1 (a=?)}}
-- do_eqp_test 5.3.2 {
--   SELECT * FROM t1 WHERE likelihood(a=?, 0.9)
-- } {0 0 0 {SCAN TABLE t1}}
-- do_eqp_test 5.3.3 {
--   SELECT * FROM t1 WHERE likely(a=?)
-- } {0 0 0 {SCAN TABLE t1}}
-- 2015-06-18
-- Ticket [https://www.sql.org/see/tktview/472f0742a1868fb58862bc588ed70]
--

test:do_execsql_test(
    "6.0",
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(i int PRIMARY KEY, x INT , y INT , z INT );
        INSERT INTO t1 VALUES (1,1,1,1), (2,2,2,2), (3,3,3,3), (4,4,4,4);
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t2(i int PRIMARY KEY, b TEXT);
        INSERT INTO t2 VALUES(1,'T'), (2,'F');
        SELECT count(*) FROM t1 LEFT JOIN t2 ON t1.i=t2.i AND b='T' union all
        SELECT count(*) FROM t1 LEFT JOIN t2 ON likely(t1.i=t2.i) AND b='T';
    ]], {
        -- <6.0>
        4, 4
        -- </6.0>
    })

-- 2015-06-20
-- Crash discovered by AFL
-- MUST_WORK_TEST
test:do_execsql_test(
    "7.0",
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a INT , b INT , PRIMARY KEY(a,b));
        INSERT INTO t1 VALUES(9,1),(1,2);
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t2(x INT , y INT , PRIMARY KEY(x,y));
        INSERT INTO t2 VALUES(3,3),(4,4);
        SELECT likely(a), x FROM t1, t2 ORDER BY 1, 2;
    ]], {
        -- <7.0>
        1, 3, 1, 4, 9, 3, 9, 4
        -- </7.0>
    })

test:do_execsql_test(
    "7.1",
    [[
        SELECT unlikely(a), x FROM t1, t2 ORDER BY 1, 2;
    ]], {
        -- <7.1>
        1, 3, 1, 4, 9, 3, 9, 4
        -- </7.1>
    })

test:do_execsql_test(
    "7.2",
    [[
        SELECT likelihood(a,0.5), x FROM t1, t2 ORDER BY 1, 2;
    ]], {
        -- <7.2>
        1, 3, 1, 4, 9, 3, 9, 4
        -- </7.2>
    })

test:do_execsql_test(
    "7.3",
    [[
        SELECT coalesce(a,a), x FROM t1, t2 ORDER BY 1, 2;
    ]], {
        -- <7.3>
        1, 3, 1, 4, 9, 3, 9, 4
        -- </7.3>
    })

-- gh-2860 skip-scan plan
test:execsql([[
    CREATE TABLE people(name TEXT PRIMARY KEY, role TEXT NOT NULL,
        height INT NOT NULL, CHECK(role IN ('student','teacher')));
    CREATE INDEX people_idx1 ON people(role, height);
    INSERT INTO people VALUES('Alice','student',156);
    INSERT INTO people VALUES('Bob','student',161);
    INSERT INTO people VALUES('Cindy','student',155);
    INSERT INTO people VALUES('David','student',181);
    INSERT INTO people VALUES('Emily','teacher',158);
    INSERT INTO people VALUES('Fred','student',163);
    INSERT INTO people VALUES('Ginny','student',169);
    INSERT INTO people VALUES('Harold','student',172);
    INSERT INTO people VALUES('Imma','student',179);
    INSERT INTO people VALUES('Jack','student',181);
    INSERT INTO people VALUES('Karen','student',163);
    INSERT INTO people VALUES('Logan','student',177);
    INSERT INTO people VALUES('Megan','teacher',159);
    INSERT INTO people VALUES('Nathan','student',163);
    INSERT INTO people VALUES('Olivia','student',161);
    INSERT INTO people VALUES('Patrick','teacher',180);
    INSERT INTO people VALUES('Quiana','student',182);
    INSERT INTO people VALUES('Robert','student',159);
    INSERT INTO people VALUES('Sally','student',166);
    INSERT INTO people VALUES('Tom','student',171);
    INSERT INTO people VALUES('Ursula','student',170);
    INSERT INTO people VALUES('Vance','student',179);
    INSERT INTO people VALUES('Willma','student',175);
    INSERT INTO people VALUES('Xavier','teacher',185);
    INSERT INTO people VALUES('Yvonne','student',149);
    INSERT INTO people VALUES('Zach','student',170);
    INSERT INTO people VALUES('Angie','student',166);
    INSERT INTO people VALUES('Brad','student',176);
    INSERT INTO people VALUES('Claire','student',168);
    INSERT INTO people VALUES('Donald','student',162);
    INSERT INTO people VALUES('Elaine','student',177);
    INSERT INTO people VALUES('Frazier','student',159);
    INSERT INTO people VALUES('Grace','student',179);
    INSERT INTO people VALUES('Horace','student',166);
    INSERT INTO people VALUES('Ingrad','student',155);
    INSERT INTO people VALUES('Jacob','student',179);
    INSERT INTO people VALUES('John', 'student', 154);
]])

test:do_execsql_test(
    "7.1",
    [[
        SELECT name FROM people WHERE height>=180 order by name;
    ]],
    {"David","Jack","Patrick","Quiana","Xavier"})

test:do_execsql_test(
    "7.2",
    [[
        EXPLAIN QUERY PLAN SELECT name FROM people WHERE height>=180;
    ]],
    {0,0,0,"SCAN TABLE PEOPLE (~983040 rows)"})

test:do_execsql_test(
    "7.3",
    [[
        -- ANALYZE;
        EXPLAIN QUERY PLAN SELECT name FROM people WHERE height>=180;
    ]],
    -- {0,0,0,"SEARCH TABLE PEOPLE USING COVERING INDEX PEOPLE_IDX1" ..
    --     " (ANY(ROLE) AND HEIGHT>?)"}
    {0,0,0,"SCAN TABLE PEOPLE (~983040 rows)" }
    )

test:finish_test()
