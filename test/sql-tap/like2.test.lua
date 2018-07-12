#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(282)

--!./tcltestrunner.lua
-- 2008 May 26
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.  The
-- focus of this file is testing the LIKE and GLOB operators and
-- in particular the optimizations that occur to help those operators
-- run faster.
--
-- $Id: like2.test,v 1.1 2008/05/26 18:33:41 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "like2-1.1",
    function()
        return test:execsql [=[
            CREATE TABLE t1(x INT PRIMARY KEY, y  TEXT COLLATE "unicode_ci");
            INSERT INTO t1(x,y) VALUES(1,CAST(x'01' AS TEXT));
            INSERT INTO t1(x,y) VALUES(2,CAST(x'02' AS TEXT));
            INSERT INTO t1(x,y) VALUES(3,CAST(x'03' AS TEXT));
            INSERT INTO t1(x,y) VALUES(4,CAST(x'04' AS TEXT));
            INSERT INTO t1(x,y) VALUES(5,CAST(x'05' AS TEXT));
            INSERT INTO t1(x,y) VALUES(6,CAST(x'06' AS TEXT));
            INSERT INTO t1(x,y) VALUES(7,CAST(x'07' AS TEXT));
            INSERT INTO t1(x,y) VALUES(8,CAST(x'08' AS TEXT));
            INSERT INTO t1(x,y) VALUES(9,CAST(x'09' AS TEXT));
            INSERT INTO t1(x,y) VALUES(10,CAST(x'0a' AS TEXT));
            INSERT INTO t1(x,y) VALUES(11,CAST(x'0b' AS TEXT));
            INSERT INTO t1(x,y) VALUES(12,CAST(x'0c' AS TEXT));
            INSERT INTO t1(x,y) VALUES(13,CAST(x'0d' AS TEXT));
            INSERT INTO t1(x,y) VALUES(14,CAST(x'0e' AS TEXT));
            INSERT INTO t1(x,y) VALUES(15,CAST(x'0f' AS TEXT));
            INSERT INTO t1(x,y) VALUES(16,CAST(x'10' AS TEXT));
            INSERT INTO t1(x,y) VALUES(17,CAST(x'11' AS TEXT));
            INSERT INTO t1(x,y) VALUES(18,CAST(x'12' AS TEXT));
            INSERT INTO t1(x,y) VALUES(19,CAST(x'13' AS TEXT));
            INSERT INTO t1(x,y) VALUES(20,CAST(x'14' AS TEXT));
            INSERT INTO t1(x,y) VALUES(21,CAST(x'15' AS TEXT));
            INSERT INTO t1(x,y) VALUES(22,CAST(x'16' AS TEXT));
            INSERT INTO t1(x,y) VALUES(23,CAST(x'17' AS TEXT));
            INSERT INTO t1(x,y) VALUES(24,CAST(x'18' AS TEXT));
            INSERT INTO t1(x,y) VALUES(25,CAST(x'19' AS TEXT));
            INSERT INTO t1(x,y) VALUES(26,CAST(x'1a' AS TEXT));
            INSERT INTO t1(x,y) VALUES(27,CAST(x'1b' AS TEXT));
            INSERT INTO t1(x,y) VALUES(28,CAST(x'1c' AS TEXT));
            INSERT INTO t1(x,y) VALUES(29,CAST(x'1d' AS TEXT));
            INSERT INTO t1(x,y) VALUES(30,CAST(x'1e' AS TEXT));
            INSERT INTO t1(x,y) VALUES(31,CAST(x'1f' AS TEXT));
            INSERT INTO t1(x,y) VALUES(32,' ');
            INSERT INTO t1(x,y) VALUES(33,'!');
            INSERT INTO t1(x,y) VALUES(34,'"');
            INSERT INTO t1(x,y) VALUES(35,'#');
            INSERT INTO t1(x,y) VALUES(36,'$');
            INSERT INTO t1(x,y) VALUES(37,'%');
            INSERT INTO t1(x,y) VALUES(38,'&');
            INSERT INTO t1(x,y) VALUES(39,'''');
            INSERT INTO t1(x,y) VALUES(40,'(');
            INSERT INTO t1(x,y) VALUES(41,')');
            INSERT INTO t1(x,y) VALUES(42,'*');
            INSERT INTO t1(x,y) VALUES(43,'+');
            INSERT INTO t1(x,y) VALUES(44,',');
            INSERT INTO t1(x,y) VALUES(45,'-');
            INSERT INTO t1(x,y) VALUES(46,'.');
            INSERT INTO t1(x,y) VALUES(47,'/');
            INSERT INTO t1(x,y) VALUES(48,'0');
            INSERT INTO t1(x,y) VALUES(49,'1');
            INSERT INTO t1(x,y) VALUES(50,'2');
            INSERT INTO t1(x,y) VALUES(51,'3');
            INSERT INTO t1(x,y) VALUES(52,'4');
            INSERT INTO t1(x,y) VALUES(53,'5');
            INSERT INTO t1(x,y) VALUES(54,'6');
            INSERT INTO t1(x,y) VALUES(55,'7');
            INSERT INTO t1(x,y) VALUES(56,'8');
            INSERT INTO t1(x,y) VALUES(57,'9');
            INSERT INTO t1(x,y) VALUES(58,':');
            INSERT INTO t1(x,y) VALUES(59,';');
            INSERT INTO t1(x,y) VALUES(60,'<');
            INSERT INTO t1(x,y) VALUES(61,'=');
            INSERT INTO t1(x,y) VALUES(62,'>');
            INSERT INTO t1(x,y) VALUES(63,'?');
            INSERT INTO t1(x,y) VALUES(64,'@');
            INSERT INTO t1(x,y) VALUES(65,'A');
            INSERT INTO t1(x,y) VALUES(66,'B');
            INSERT INTO t1(x,y) VALUES(67,'C');
            INSERT INTO t1(x,y) VALUES(68,'D');
            INSERT INTO t1(x,y) VALUES(69,'E');
            INSERT INTO t1(x,y) VALUES(70,'F');
            INSERT INTO t1(x,y) VALUES(71,'G');
            INSERT INTO t1(x,y) VALUES(72,'H');
            INSERT INTO t1(x,y) VALUES(73,'I');
            INSERT INTO t1(x,y) VALUES(74,'J');
            INSERT INTO t1(x,y) VALUES(75,'K');
            INSERT INTO t1(x,y) VALUES(76,'L');
            INSERT INTO t1(x,y) VALUES(77,'M');
            INSERT INTO t1(x,y) VALUES(78,'N');
            INSERT INTO t1(x,y) VALUES(79,'O');
            INSERT INTO t1(x,y) VALUES(80,'P');
            INSERT INTO t1(x,y) VALUES(81,'Q');
            INSERT INTO t1(x,y) VALUES(82,'R');
            INSERT INTO t1(x,y) VALUES(83,'S');
            INSERT INTO t1(x,y) VALUES(84,'T');
            INSERT INTO t1(x,y) VALUES(85,'U');
            INSERT INTO t1(x,y) VALUES(86,'V');
            INSERT INTO t1(x,y) VALUES(87,'W');
            INSERT INTO t1(x,y) VALUES(88,'X');
            INSERT INTO t1(x,y) VALUES(89,'Y');
            INSERT INTO t1(x,y) VALUES(90,'Z');
            INSERT INTO t1(x,y) VALUES(91,'[');
            INSERT INTO t1(x,y) VALUES(92,'\');
            INSERT INTO t1(x,y) VALUES(93,']');
            INSERT INTO t1(x,y) VALUES(94,'^');
            INSERT INTO t1(x,y) VALUES(95,'_');
            INSERT INTO t1(x,y) VALUES(96,'`');
            INSERT INTO t1(x,y) VALUES(97,'a');
            INSERT INTO t1(x,y) VALUES(98,'b');
            INSERT INTO t1(x,y) VALUES(99,'c');
            INSERT INTO t1(x,y) VALUES(100,'d');
            INSERT INTO t1(x,y) VALUES(101,'e');
            INSERT INTO t1(x,y) VALUES(102,'f');
            INSERT INTO t1(x,y) VALUES(103,'g');
            INSERT INTO t1(x,y) VALUES(104,'h');
            INSERT INTO t1(x,y) VALUES(105,'i');
            INSERT INTO t1(x,y) VALUES(106,'j');
            INSERT INTO t1(x,y) VALUES(107,'k');
            INSERT INTO t1(x,y) VALUES(108,'l');
            INSERT INTO t1(x,y) VALUES(109,'m');
            INSERT INTO t1(x,y) VALUES(110,'n');
            INSERT INTO t1(x,y) VALUES(111,'o');
            INSERT INTO t1(x,y) VALUES(112,'p');
            INSERT INTO t1(x,y) VALUES(113,'q');
            INSERT INTO t1(x,y) VALUES(114,'r');
            INSERT INTO t1(x,y) VALUES(115,'s');
            INSERT INTO t1(x,y) VALUES(116,'t');
            INSERT INTO t1(x,y) VALUES(117,'u');
            INSERT INTO t1(x,y) VALUES(118,'v');
            INSERT INTO t1(x,y) VALUES(119,'w');
            INSERT INTO t1(x,y) VALUES(120,'x');
            INSERT INTO t1(x,y) VALUES(121,'y');
            INSERT INTO t1(x,y) VALUES(122,'z');
            INSERT INTO t1(x,y) VALUES(123,'{');
            INSERT INTO t1(x,y) VALUES(124,'|');
            INSERT INTO t1(x,y) VALUES(125,'}');
            INSERT INTO t1(x,y) VALUES(126,'~');
            INSERT INTO t1(x,y) VALUES(127,CAST(x'7f' AS TEXT));
            SELECT count(*) FROM t1;
        ]=]
    end, {
        -- <like2-1.1>
        127
        -- </like2-1.1>
    })

test:do_test(
    "like2-1.2",
    function()
        return test:execsql [[
            CREATE TABLE t2(x INT PRIMARY KEY, y  TEXT COLLATE "unicode_ci");
            INSERT INTO t2 SELECT * FROM t1;
            CREATE INDEX i2 ON t2(y);
            SELECT count(*) FROM t2;
        ]]
    end, {
        -- <like2-1.2>
        127
        -- </like2-1.2>
    })

test:do_test(
    "like2-1.3",
    function()
        return test:execsql [[
            CREATE TABLE t3(x INT PRIMARY KEY, y  TEXT COLLATE "unicode_ci");
            INSERT INTO t3 SELECT x, 'abc' || y || 'xyz' FROM t1;
            CREATE INDEX i3 ON t3(y);
            SELECT count(*) FROM t2;
        ]]
    end, {
        -- <like2-1.3>
        127
        -- </like2-1.3>
    })

test:do_test(
    "like-2.32.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE ' %'"
    end, {
        -- <like-2.32.1>
        32
        -- </like-2.32.1>
    })

test:do_test(
    "like-2.32.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE ' %'"
    end, {
        -- <like-2.32.2>
        32
        -- </like-2.32.2>
    })

test:do_test(
    "like-2.32.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc %'"
    end, {
        -- <like-2.32.3>
        32
        -- </like-2.32.3>
    })

test:do_test(
    "like-2.33.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '!%'"
    end, {
        -- <like-2.33.1>
        33
        -- </like-2.33.1>
    })

test:do_test(
    "like-2.33.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '!%'"
    end, {
        -- <like-2.33.2>
        33
        -- </like-2.33.2>
    })

test:do_test(
    "like-2.33.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc!%'"
    end, {
        -- <like-2.33.3>
        33
        -- </like-2.33.3>
    })

test:do_test(
    "like-2.34.1",
    function()
        return test:execsql [[SELECT x FROM t1 WHERE y LIKE '"%']]
    end, {
        -- <like-2.34.1>
        34
        -- </like-2.34.1>
    })

test:do_test(
    "like-2.34.2",
    function()
        return test:execsql [[SELECT x FROM t2 WHERE y LIKE '"%']]
    end, {
        -- <like-2.34.2>
        34
        -- </like-2.34.2>
    })

test:do_test(
    "like-2.34.3",
    function()
        return test:execsql [[SELECT x FROM t3 WHERE y LIKE 'abc"%']]
    end, {
        -- <like-2.34.3>
        34
        -- </like-2.34.3>
    })

test:do_test(
    "like-2.35.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '#%'"
    end, {
        -- <like-2.35.1>
        35
        -- </like-2.35.1>
    })

test:do_test(
    "like-2.35.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '#%'"
    end, {
        -- <like-2.35.2>
        35
        -- </like-2.35.2>
    })

test:do_test(
    "like-2.35.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc#%'"
    end, {
        -- <like-2.35.3>
        35
        -- </like-2.35.3>
    })

test:do_test(
    "like-2.36.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '$%'"
    end, {
        -- <like-2.36.1>
        36
        -- </like-2.36.1>
    })

test:do_test(
    "like-2.36.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '$%'"
    end, {
        -- <like-2.36.2>
        36
        -- </like-2.36.2>
    })

test:do_test(
    "like-2.36.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc$%'"
    end, {
        -- <like-2.36.3>
        36
        -- </like-2.36.3>
    })

test:do_test(
    "like-2.38.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '&%'"
    end, {
        -- <like-2.38.1>
        38
        -- </like-2.38.1>
    })

test:do_test(
    "like-2.38.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '&%'"
    end, {
        -- <like-2.38.2>
        38
        -- </like-2.38.2>
    })

test:do_test(
    "like-2.38.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc&%'"
    end, {
        -- <like-2.38.3>
        38
        -- </like-2.38.3>
    })

test:do_test(
    "like-2.39.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '''%'"
    end, {
        -- <like-2.39.1>
        39
        -- </like-2.39.1>
    })

test:do_test(
    "like-2.39.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '''%'"
    end, {
        -- <like-2.39.2>
        39
        -- </like-2.39.2>
    })

test:do_test(
    "like-2.39.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc''%'"
    end, {
        -- <like-2.39.3>
        39
        -- </like-2.39.3>
    })

test:do_test(
    "like-2.40.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '(%'"
    end, {
        -- <like-2.40.1>
        40
        -- </like-2.40.1>
    })

test:do_test(
    "like-2.40.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '(%'"
    end, {
        -- <like-2.40.2>
        40
        -- </like-2.40.2>
    })

test:do_test(
    "like-2.40.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc(%'"
    end, {
        -- <like-2.40.3>
        40
        -- </like-2.40.3>
    })

test:do_test(
    "like-2.41.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE ')%'"
    end, {
        -- <like-2.41.1>
        41
        -- </like-2.41.1>
    })

test:do_test(
    "like-2.41.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE ')%'"
    end, {
        -- <like-2.41.2>
        41
        -- </like-2.41.2>
    })

test:do_test(
    "like-2.41.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc)%'"
    end, {
        -- <like-2.41.3>
        41
        -- </like-2.41.3>
    })

test:do_test(
    "like-2.42.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '*%'"
    end, {
        -- <like-2.42.1>
        42
        -- </like-2.42.1>
    })

test:do_test(
    "like-2.42.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '*%'"
    end, {
        -- <like-2.42.2>
        42
        -- </like-2.42.2>
    })

test:do_test(
    "like-2.42.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc*%'"
    end, {
        -- <like-2.42.3>
        42
        -- </like-2.42.3>
    })

test:do_test(
    "like-2.43.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '+%'"
    end, {
        -- <like-2.43.1>
        43
        -- </like-2.43.1>
    })

test:do_test(
    "like-2.43.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '+%'"
    end, {
        -- <like-2.43.2>
        43
        -- </like-2.43.2>
    })

test:do_test(
    "like-2.43.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc+%'"
    end, {
        -- <like-2.43.3>
        43
        -- </like-2.43.3>
    })

test:do_test(
    "like-2.44.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE ',%'"
    end, {
        -- <like-2.44.1>
        44
        -- </like-2.44.1>
    })

test:do_test(
    "like-2.44.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE ',%'"
    end, {
        -- <like-2.44.2>
        44
        -- </like-2.44.2>
    })

test:do_test(
    "like-2.44.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc,%'"
    end, {
        -- <like-2.44.3>
        44
        -- </like-2.44.3>
    })

test:do_test(
    "like-2.45.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '-%'"
    end, {
        -- <like-2.45.1>
        45
        -- </like-2.45.1>
    })

test:do_test(
    "like-2.45.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '-%'"
    end, {
        -- <like-2.45.2>
        45
        -- </like-2.45.2>
    })

test:do_test(
    "like-2.45.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc-%'"
    end, {
        -- <like-2.45.3>
        45
        -- </like-2.45.3>
    })

test:do_test(
    "like-2.46.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '.%'"
    end, {
        -- <like-2.46.1>
        46
        -- </like-2.46.1>
    })

test:do_test(
    "like-2.46.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '.%'"
    end, {
        -- <like-2.46.2>
        46
        -- </like-2.46.2>
    })

test:do_test(
    "like-2.46.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc.%'"
    end, {
        -- <like-2.46.3>
        46
        -- </like-2.46.3>
    })

test:do_test(
    "like-2.47.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '/%'"
    end, {
        -- <like-2.47.1>
        47
        -- </like-2.47.1>
    })

test:do_test(
    "like-2.47.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '/%'"
    end, {
        -- <like-2.47.2>
        47
        -- </like-2.47.2>
    })

test:do_test(
    "like-2.47.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc/%'"
    end, {
        -- <like-2.47.3>
        47
        -- </like-2.47.3>
    })

test:do_test(
    "like-2.48.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '0%'"
    end, {
        -- <like-2.48.1>
        48
        -- </like-2.48.1>
    })

test:do_test(
    "like-2.48.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '0%'"
    end, {
        -- <like-2.48.2>
        48
        -- </like-2.48.2>
    })

test:do_test(
    "like-2.48.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc0%'"
    end, {
        -- <like-2.48.3>
        48
        -- </like-2.48.3>
    })

test:do_test(
    "like-2.49.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '1%'"
    end, {
        -- <like-2.49.1>
        49
        -- </like-2.49.1>
    })

test:do_test(
    "like-2.49.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '1%'"
    end, {
        -- <like-2.49.2>
        49
        -- </like-2.49.2>
    })

test:do_test(
    "like-2.49.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc1%'"
    end, {
        -- <like-2.49.3>
        49
        -- </like-2.49.3>
    })

test:do_test(
    "like-2.50.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '2%'"
    end, {
        -- <like-2.50.1>
        50
        -- </like-2.50.1>
    })

test:do_test(
    "like-2.50.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '2%'"
    end, {
        -- <like-2.50.2>
        50
        -- </like-2.50.2>
    })

test:do_test(
    "like-2.50.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc2%'"
    end, {
        -- <like-2.50.3>
        50
        -- </like-2.50.3>
    })

test:do_test(
    "like-2.51.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '3%'"
    end, {
        -- <like-2.51.1>
        51
        -- </like-2.51.1>
    })

test:do_test(
    "like-2.51.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '3%'"
    end, {
        -- <like-2.51.2>
        51
        -- </like-2.51.2>
    })

test:do_test(
    "like-2.51.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc3%'"
    end, {
        -- <like-2.51.3>
        51
        -- </like-2.51.3>
    })

test:do_test(
    "like-2.52.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '4%'"
    end, {
        -- <like-2.52.1>
        52
        -- </like-2.52.1>
    })

test:do_test(
    "like-2.52.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '4%'"
    end, {
        -- <like-2.52.2>
        52
        -- </like-2.52.2>
    })

test:do_test(
    "like-2.52.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc4%'"
    end, {
        -- <like-2.52.3>
        52
        -- </like-2.52.3>
    })

test:do_test(
    "like-2.53.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '5%'"
    end, {
        -- <like-2.53.1>
        53
        -- </like-2.53.1>
    })

test:do_test(
    "like-2.53.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '5%'"
    end, {
        -- <like-2.53.2>
        53
        -- </like-2.53.2>
    })

test:do_test(
    "like-2.53.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc5%'"
    end, {
        -- <like-2.53.3>
        53
        -- </like-2.53.3>
    })

test:do_test(
    "like-2.54.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '6%'"
    end, {
        -- <like-2.54.1>
        54
        -- </like-2.54.1>
    })

test:do_test(
    "like-2.54.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '6%'"
    end, {
        -- <like-2.54.2>
        54
        -- </like-2.54.2>
    })

test:do_test(
    "like-2.54.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc6%'"
    end, {
        -- <like-2.54.3>
        54
        -- </like-2.54.3>
    })

test:do_test(
    "like-2.55.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '7%'"
    end, {
        -- <like-2.55.1>
        55
        -- </like-2.55.1>
    })

test:do_test(
    "like-2.55.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '7%'"
    end, {
        -- <like-2.55.2>
        55
        -- </like-2.55.2>
    })

test:do_test(
    "like-2.55.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc7%'"
    end, {
        -- <like-2.55.3>
        55
        -- </like-2.55.3>
    })

test:do_test(
    "like-2.56.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '8%'"
    end, {
        -- <like-2.56.1>
        56
        -- </like-2.56.1>
    })

test:do_test(
    "like-2.56.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '8%'"
    end, {
        -- <like-2.56.2>
        56
        -- </like-2.56.2>
    })

test:do_test(
    "like-2.56.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc8%'"
    end, {
        -- <like-2.56.3>
        56
        -- </like-2.56.3>
    })

test:do_test(
    "like-2.57.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '9%'"
    end, {
        -- <like-2.57.1>
        57
        -- </like-2.57.1>
    })

test:do_test(
    "like-2.57.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '9%'"
    end, {
        -- <like-2.57.2>
        57
        -- </like-2.57.2>
    })

test:do_test(
    "like-2.57.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc9%'"
    end, {
        -- <like-2.57.3>
        57
        -- </like-2.57.3>
    })

test:do_test(
    "like-2.58.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE ':%'"
    end, {
        -- <like-2.58.1>
        58
        -- </like-2.58.1>
    })

test:do_test(
    "like-2.58.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE ':%'"
    end, {
        -- <like-2.58.2>
        58
        -- </like-2.58.2>
    })

test:do_test(
    "like-2.58.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc:%'"
    end, {
        -- <like-2.58.3>
        58
        -- </like-2.58.3>
    })

test:do_test(
    "like-2.59.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE ';%'"
    end, {
        -- <like-2.59.1>
        59
        -- </like-2.59.1>
    })

test:do_test(
    "like-2.59.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE ';%'"
    end, {
        -- <like-2.59.2>
        59
        -- </like-2.59.2>
    })

test:do_test(
    "like-2.59.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc;%'"
    end, {
        -- <like-2.59.3>
        59
        -- </like-2.59.3>
    })

test:do_test(
    "like-2.60.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '<%'"
    end, {
        -- <like-2.60.1>
        60
        -- </like-2.60.1>
    })

test:do_test(
    "like-2.60.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '<%'"
    end, {
        -- <like-2.60.2>
        60
        -- </like-2.60.2>
    })

test:do_test(
    "like-2.60.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc<%'"
    end, {
        -- <like-2.60.3>
        60
        -- </like-2.60.3>
    })

test:do_test(
    "like-2.61.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '=%'"
    end, {
        -- <like-2.61.1>
        61
        -- </like-2.61.1>
    })

test:do_test(
    "like-2.61.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '=%'"
    end, {
        -- <like-2.61.2>
        61
        -- </like-2.61.2>
    })

test:do_test(
    "like-2.61.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc=%'"
    end, {
        -- <like-2.61.3>
        61
        -- </like-2.61.3>
    })

test:do_test(
    "like-2.62.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '>%'"
    end, {
        -- <like-2.62.1>
        62
        -- </like-2.62.1>
    })

test:do_test(
    "like-2.62.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '>%'"
    end, {
        -- <like-2.62.2>
        62
        -- </like-2.62.2>
    })

test:do_test(
    "like-2.62.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc>%'"
    end, {
        -- <like-2.62.3>
        62
        -- </like-2.62.3>
    })

test:do_test(
    "like-2.63.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '?%'"
    end, {
        -- <like-2.63.1>
        63
        -- </like-2.63.1>
    })

test:do_test(
    "like-2.63.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '?%'"
    end, {
        -- <like-2.63.2>
        63
        -- </like-2.63.2>
    })

test:do_test(
    "like-2.63.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc?%'"
    end, {
        -- <like-2.63.3>
        63
        -- </like-2.63.3>
    })

test:do_test(
    "like-2.64.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '@%'"
    end, {
        -- <like-2.64.1>
        64
        -- </like-2.64.1>
    })

test:do_test(
    "like-2.64.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '@%'"
    end, {
        -- <like-2.64.2>
        64
        -- </like-2.64.2>
    })

test:do_test(
    "like-2.64.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc@%'"
    end, {
        -- <like-2.64.3>
        64
        -- </like-2.64.3>
    })

test:do_test(
    "like-2.65.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'A%'"
    end, {
        -- <like-2.65.1>
        65, 97
        -- </like-2.65.1>
    })

test:do_test(
    "like-2.65.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'A%'"
    end, {
        -- <like-2.65.2>
        65, 97
        -- </like-2.65.2>
    })

test:do_test(
    "like-2.65.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcA%'"
    end, {
        -- <like-2.65.3>
        65, 97
        -- </like-2.65.3>
    })

test:do_test(
    "like-2.66.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'B%'"
    end, {
        -- <like-2.66.1>
        66, 98
        -- </like-2.66.1>
    })

test:do_test(
    "like-2.66.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'B%'"
    end, {
        -- <like-2.66.2>
        66, 98
        -- </like-2.66.2>
    })

test:do_test(
    "like-2.66.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcB%'"
    end, {
        -- <like-2.66.3>
        66, 98
        -- </like-2.66.3>
    })

test:do_test(
    "like-2.67.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'C%'"
    end, {
        -- <like-2.67.1>
        67, 99
        -- </like-2.67.1>
    })

test:do_test(
    "like-2.67.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'C%'"
    end, {
        -- <like-2.67.2>
        67, 99
        -- </like-2.67.2>
    })

test:do_test(
    "like-2.67.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcC%'"
    end, {
        -- <like-2.67.3>
        67, 99
        -- </like-2.67.3>
    })

test:do_test(
    "like-2.68.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'D%'"
    end, {
        -- <like-2.68.1>
        68, 100
        -- </like-2.68.1>
    })

test:do_test(
    "like-2.68.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'D%'"
    end, {
        -- <like-2.68.2>
        68, 100
        -- </like-2.68.2>
    })

test:do_test(
    "like-2.68.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcD%'"
    end, {
        -- <like-2.68.3>
        68, 100
        -- </like-2.68.3>
    })

test:do_test(
    "like-2.69.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'E%'"
    end, {
        -- <like-2.69.1>
        69, 101
        -- </like-2.69.1>
    })

test:do_test(
    "like-2.69.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'E%'"
    end, {
        -- <like-2.69.2>
        69, 101
        -- </like-2.69.2>
    })

test:do_test(
    "like-2.69.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcE%'"
    end, {
        -- <like-2.69.3>
        69, 101
        -- </like-2.69.3>
    })

test:do_test(
    "like-2.70.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'F%'"
    end, {
        -- <like-2.70.1>
        70, 102
        -- </like-2.70.1>
    })

test:do_test(
    "like-2.70.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'F%'"
    end, {
        -- <like-2.70.2>
        70, 102
        -- </like-2.70.2>
    })

test:do_test(
    "like-2.70.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcF%'"
    end, {
        -- <like-2.70.3>
        70, 102
        -- </like-2.70.3>
    })

test:do_test(
    "like-2.71.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'G%'"
    end, {
        -- <like-2.71.1>
        71, 103
        -- </like-2.71.1>
    })

test:do_test(
    "like-2.71.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'G%'"
    end, {
        -- <like-2.71.2>
        71, 103
        -- </like-2.71.2>
    })

test:do_test(
    "like-2.71.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcG%'"
    end, {
        -- <like-2.71.3>
        71, 103
        -- </like-2.71.3>
    })

test:do_test(
    "like-2.72.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'H%'"
    end, {
        -- <like-2.72.1>
        72, 104
        -- </like-2.72.1>
    })

test:do_test(
    "like-2.72.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'H%'"
    end, {
        -- <like-2.72.2>
        72, 104
        -- </like-2.72.2>
    })

test:do_test(
    "like-2.72.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcH%'"
    end, {
        -- <like-2.72.3>
        72, 104
        -- </like-2.72.3>
    })

test:do_test(
    "like-2.73.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'I%'"
    end, {
        -- <like-2.73.1>
        73, 105
        -- </like-2.73.1>
    })

test:do_test(
    "like-2.73.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'I%'"
    end, {
        -- <like-2.73.2>
        73, 105
        -- </like-2.73.2>
    })

test:do_test(
    "like-2.73.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcI%'"
    end, {
        -- <like-2.73.3>
        73, 105
        -- </like-2.73.3>
    })

test:do_test(
    "like-2.74.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'J%'"
    end, {
        -- <like-2.74.1>
        74, 106
        -- </like-2.74.1>
    })

test:do_test(
    "like-2.74.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'J%'"
    end, {
        -- <like-2.74.2>
        74, 106
        -- </like-2.74.2>
    })

test:do_test(
    "like-2.74.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcJ%'"
    end, {
        -- <like-2.74.3>
        74, 106
        -- </like-2.74.3>
    })

test:do_test(
    "like-2.75.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'K%'"
    end, {
        -- <like-2.75.1>
        75, 107
        -- </like-2.75.1>
    })

test:do_test(
    "like-2.75.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'K%'"
    end, {
        -- <like-2.75.2>
        75, 107
        -- </like-2.75.2>
    })

test:do_test(
    "like-2.75.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcK%'"
    end, {
        -- <like-2.75.3>
        75, 107
        -- </like-2.75.3>
    })

test:do_test(
    "like-2.76.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'L%'"
    end, {
        -- <like-2.76.1>
        76, 108
        -- </like-2.76.1>
    })

test:do_test(
    "like-2.76.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'L%'"
    end, {
        -- <like-2.76.2>
        76, 108
        -- </like-2.76.2>
    })

test:do_test(
    "like-2.76.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcL%'"
    end, {
        -- <like-2.76.3>
        76, 108
        -- </like-2.76.3>
    })

test:do_test(
    "like-2.77.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'M%'"
    end, {
        -- <like-2.77.1>
        77, 109
        -- </like-2.77.1>
    })

test:do_test(
    "like-2.77.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'M%'"
    end, {
        -- <like-2.77.2>
        77, 109
        -- </like-2.77.2>
    })

test:do_test(
    "like-2.77.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcM%'"
    end, {
        -- <like-2.77.3>
        77, 109
        -- </like-2.77.3>
    })

test:do_test(
    "like-2.78.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'N%'"
    end, {
        -- <like-2.78.1>
        78, 110
        -- </like-2.78.1>
    })

test:do_test(
    "like-2.78.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'N%'"
    end, {
        -- <like-2.78.2>
        78, 110
        -- </like-2.78.2>
    })

test:do_test(
    "like-2.78.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcN%'"
    end, {
        -- <like-2.78.3>
        78, 110
        -- </like-2.78.3>
    })

test:do_test(
    "like-2.79.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'O%'"
    end, {
        -- <like-2.79.1>
        79, 111
        -- </like-2.79.1>
    })

test:do_test(
    "like-2.79.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'O%'"
    end, {
        -- <like-2.79.2>
        79, 111
        -- </like-2.79.2>
    })

test:do_test(
    "like-2.79.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcO%'"
    end, {
        -- <like-2.79.3>
        79, 111
        -- </like-2.79.3>
    })

test:do_test(
    "like-2.80.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'P%'"
    end, {
        -- <like-2.80.1>
        80, 112
        -- </like-2.80.1>
    })

test:do_test(
    "like-2.80.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'P%'"
    end, {
        -- <like-2.80.2>
        80, 112
        -- </like-2.80.2>
    })

test:do_test(
    "like-2.80.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcP%'"
    end, {
        -- <like-2.80.3>
        80, 112
        -- </like-2.80.3>
    })

test:do_test(
    "like-2.81.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'Q%'"
    end, {
        -- <like-2.81.1>
        81, 113
        -- </like-2.81.1>
    })

test:do_test(
    "like-2.81.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'Q%'"
    end, {
        -- <like-2.81.2>
        81, 113
        -- </like-2.81.2>
    })

test:do_test(
    "like-2.81.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcQ%'"
    end, {
        -- <like-2.81.3>
        81, 113
        -- </like-2.81.3>
    })

test:do_test(
    "like-2.82.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'R%'"
    end, {
        -- <like-2.82.1>
        82, 114
        -- </like-2.82.1>
    })

test:do_test(
    "like-2.82.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'R%'"
    end, {
        -- <like-2.82.2>
        82, 114
        -- </like-2.82.2>
    })

test:do_test(
    "like-2.82.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcR%'"
    end, {
        -- <like-2.82.3>
        82, 114
        -- </like-2.82.3>
    })

test:do_test(
    "like-2.83.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'S%'"
    end, {
        -- <like-2.83.1>
        83, 115
        -- </like-2.83.1>
    })

test:do_test(
    "like-2.83.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'S%'"
    end, {
        -- <like-2.83.2>
        83, 115
        -- </like-2.83.2>
    })

test:do_test(
    "like-2.83.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcS%'"
    end, {
        -- <like-2.83.3>
        83, 115
        -- </like-2.83.3>
    })

test:do_test(
    "like-2.84.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'T%'"
    end, {
        -- <like-2.84.1>
        84, 116
        -- </like-2.84.1>
    })

test:do_test(
    "like-2.84.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'T%'"
    end, {
        -- <like-2.84.2>
        84, 116
        -- </like-2.84.2>
    })

test:do_test(
    "like-2.84.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcT%'"
    end, {
        -- <like-2.84.3>
        84, 116
        -- </like-2.84.3>
    })

test:do_test(
    "like-2.85.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'U%'"
    end, {
        -- <like-2.85.1>
        85, 117
        -- </like-2.85.1>
    })

test:do_test(
    "like-2.85.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'U%'"
    end, {
        -- <like-2.85.2>
        85, 117
        -- </like-2.85.2>
    })

test:do_test(
    "like-2.85.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcU%'"
    end, {
        -- <like-2.85.3>
        85, 117
        -- </like-2.85.3>
    })

test:do_test(
    "like-2.86.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'V%'"
    end, {
        -- <like-2.86.1>
        86, 118
        -- </like-2.86.1>
    })

test:do_test(
    "like-2.86.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'V%'"
    end, {
        -- <like-2.86.2>
        86, 118
        -- </like-2.86.2>
    })

test:do_test(
    "like-2.86.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcV%'"
    end, {
        -- <like-2.86.3>
        86, 118
        -- </like-2.86.3>
    })

test:do_test(
    "like-2.87.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'W%'"
    end, {
        -- <like-2.87.1>
        87, 119
        -- </like-2.87.1>
    })

test:do_test(
    "like-2.87.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'W%'"
    end, {
        -- <like-2.87.2>
        87, 119
        -- </like-2.87.2>
    })

test:do_test(
    "like-2.87.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcW%'"
    end, {
        -- <like-2.87.3>
        87, 119
        -- </like-2.87.3>
    })

test:do_test(
    "like-2.88.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'X%'"
    end, {
        -- <like-2.88.1>
        88, 120
        -- </like-2.88.1>
    })

test:do_test(
    "like-2.88.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'X%'"
    end, {
        -- <like-2.88.2>
        88, 120
        -- </like-2.88.2>
    })

test:do_test(
    "like-2.88.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcX%'"
    end, {
        -- <like-2.88.3>
        88, 120
        -- </like-2.88.3>
    })

test:do_test(
    "like-2.89.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'Y%'"
    end, {
        -- <like-2.89.1>
        89, 121
        -- </like-2.89.1>
    })

test:do_test(
    "like-2.89.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'Y%'"
    end, {
        -- <like-2.89.2>
        89, 121
        -- </like-2.89.2>
    })

test:do_test(
    "like-2.89.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcY%'"
    end, {
        -- <like-2.89.3>
        89, 121
        -- </like-2.89.3>
    })

test:do_test(
    "like-2.90.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'Z%'"
    end, {
        -- <like-2.90.1>
        90, 122
        -- </like-2.90.1>
    })

test:do_test(
    "like-2.90.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'Z%'"
    end, {
        -- <like-2.90.2>
        90, 122
        -- </like-2.90.2>
    })

test:do_test(
    "like-2.90.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcZ%'"
    end, {
        -- <like-2.90.3>
        90, 122
        -- </like-2.90.3>
    })

test:do_test(
    "like-2.91.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '[%'"
    end, {
        -- <like-2.91.1>
        91
        -- </like-2.91.1>
    })

test:do_test(
    "like-2.91.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '[%'"
    end, {
        -- <like-2.91.2>
        91
        -- </like-2.91.2>
    })

test:do_test(
    "like-2.91.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc[%'"
    end, {
        -- <like-2.91.3>
        91
        -- </like-2.91.3>
    })

test:do_test(
    "like-2.92.1",
    function()
        return test:execsql [[SELECT x FROM t1 WHERE y LIKE '\%']]
    end, {
        -- <like-2.92.1>
        92
        -- </like-2.92.1>
    })

test:do_test(
    "like-2.92.2",
    function()
        return test:execsql [[SELECT x FROM t2 WHERE y LIKE '\%']]
    end, {
        -- <like-2.92.2>
        92
        -- </like-2.92.2>
    })

test:do_test(
    "like-2.92.3",
    function()
        return test:execsql [[SELECT x FROM t3 WHERE y LIKE 'abc\%']]
    end, {
        -- <like-2.92.3>
        92
        -- </like-2.92.3>
    })

test:do_test(
    "like-2.93.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE ']%'"
    end, {
        -- <like-2.93.1>
        93
        -- </like-2.93.1>
    })

test:do_test(
    "like-2.93.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE ']%'"
    end, {
        -- <like-2.93.2>
        93
        -- </like-2.93.2>
    })

test:do_test(
    "like-2.93.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc]%'"
    end, {
        -- <like-2.93.3>
        93
        -- </like-2.93.3>
    })

test:do_test(
    "like-2.94.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '^%'"
    end, {
        -- <like-2.94.1>
        94
        -- </like-2.94.1>
    })

test:do_test(
    "like-2.94.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '^%'"
    end, {
        -- <like-2.94.2>
        94
        -- </like-2.94.2>
    })

test:do_test(
    "like-2.94.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc^%'"
    end, {
        -- <like-2.94.3>
        94
        -- </like-2.94.3>
    })

test:do_test(
    "like-2.96.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '`%'"
    end, {
        -- <like-2.96.1>
        96
        -- </like-2.96.1>
    })

test:do_test(
    "like-2.96.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '`%'"
    end, {
        -- <like-2.96.2>
        96
        -- </like-2.96.2>
    })

test:do_test(
    "like-2.96.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc`%'"
    end, {
        -- <like-2.96.3>
        96
        -- </like-2.96.3>
    })

test:do_test(
    "like-2.97.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'a%'"
    end, {
        -- <like-2.97.1>
        65, 97
        -- </like-2.97.1>
    })

test:do_test(
    "like-2.97.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'a%'"
    end, {
        -- <like-2.97.2>
        65, 97
        -- </like-2.97.2>
    })

test:do_test(
    "like-2.97.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abca%'"
    end, {
        -- <like-2.97.3>
        65, 97
        -- </like-2.97.3>
    })

test:do_test(
    "like-2.98.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'b%'"
    end, {
        -- <like-2.98.1>
        66, 98
        -- </like-2.98.1>
    })

test:do_test(
    "like-2.98.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'b%'"
    end, {
        -- <like-2.98.2>
        66, 98
        -- </like-2.98.2>
    })

test:do_test(
    "like-2.98.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcb%'"
    end, {
        -- <like-2.98.3>
        66, 98
        -- </like-2.98.3>
    })

test:do_test(
    "like-2.99.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'c%'"
    end, {
        -- <like-2.99.1>
        67, 99
        -- </like-2.99.1>
    })

test:do_test(
    "like-2.99.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'c%'"
    end, {
        -- <like-2.99.2>
        67, 99
        -- </like-2.99.2>
    })

test:do_test(
    "like-2.99.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcc%'"
    end, {
        -- <like-2.99.3>
        67, 99
        -- </like-2.99.3>
    })

test:do_test(
    "like-2.100.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'd%'"
    end, {
        -- <like-2.100.1>
        68, 100
        -- </like-2.100.1>
    })

test:do_test(
    "like-2.100.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'd%'"
    end, {
        -- <like-2.100.2>
        68, 100
        -- </like-2.100.2>
    })

test:do_test(
    "like-2.100.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcd%'"
    end, {
        -- <like-2.100.3>
        68, 100
        -- </like-2.100.3>
    })

test:do_test(
    "like-2.101.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'e%'"
    end, {
        -- <like-2.101.1>
        69, 101
        -- </like-2.101.1>
    })

test:do_test(
    "like-2.101.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'e%'"
    end, {
        -- <like-2.101.2>
        69, 101
        -- </like-2.101.2>
    })

test:do_test(
    "like-2.101.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abce%'"
    end, {
        -- <like-2.101.3>
        69, 101
        -- </like-2.101.3>
    })

test:do_test(
    "like-2.102.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'f%'"
    end, {
        -- <like-2.102.1>
        70, 102
        -- </like-2.102.1>
    })

test:do_test(
    "like-2.102.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'f%'"
    end, {
        -- <like-2.102.2>
        70, 102
        -- </like-2.102.2>
    })

test:do_test(
    "like-2.102.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcf%'"
    end, {
        -- <like-2.102.3>
        70, 102
        -- </like-2.102.3>
    })

test:do_test(
    "like-2.103.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'g%'"
    end, {
        -- <like-2.103.1>
        71, 103
        -- </like-2.103.1>
    })

test:do_test(
    "like-2.103.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'g%'"
    end, {
        -- <like-2.103.2>
        71, 103
        -- </like-2.103.2>
    })

test:do_test(
    "like-2.103.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcg%'"
    end, {
        -- <like-2.103.3>
        71, 103
        -- </like-2.103.3>
    })

test:do_test(
    "like-2.104.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'h%'"
    end, {
        -- <like-2.104.1>
        72, 104
        -- </like-2.104.1>
    })

test:do_test(
    "like-2.104.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'h%'"
    end, {
        -- <like-2.104.2>
        72, 104
        -- </like-2.104.2>
    })

test:do_test(
    "like-2.104.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abch%'"
    end, {
        -- <like-2.104.3>
        72, 104
        -- </like-2.104.3>
    })

test:do_test(
    "like-2.105.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'i%'"
    end, {
        -- <like-2.105.1>
        73, 105
        -- </like-2.105.1>
    })

test:do_test(
    "like-2.105.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'i%'"
    end, {
        -- <like-2.105.2>
        73, 105
        -- </like-2.105.2>
    })

test:do_test(
    "like-2.105.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abci%'"
    end, {
        -- <like-2.105.3>
        73, 105
        -- </like-2.105.3>
    })

test:do_test(
    "like-2.106.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'j%'"
    end, {
        -- <like-2.106.1>
        74, 106
        -- </like-2.106.1>
    })

test:do_test(
    "like-2.106.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'j%'"
    end, {
        -- <like-2.106.2>
        74, 106
        -- </like-2.106.2>
    })

test:do_test(
    "like-2.106.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcj%'"
    end, {
        -- <like-2.106.3>
        74, 106
        -- </like-2.106.3>
    })

test:do_test(
    "like-2.107.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'k%'"
    end, {
        -- <like-2.107.1>
        75, 107
        -- </like-2.107.1>
    })

test:do_test(
    "like-2.107.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'k%'"
    end, {
        -- <like-2.107.2>
        75, 107
        -- </like-2.107.2>
    })

test:do_test(
    "like-2.107.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abck%'"
    end, {
        -- <like-2.107.3>
        75, 107
        -- </like-2.107.3>
    })

test:do_test(
    "like-2.108.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'l%'"
    end, {
        -- <like-2.108.1>
        76, 108
        -- </like-2.108.1>
    })

test:do_test(
    "like-2.108.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'l%'"
    end, {
        -- <like-2.108.2>
        76, 108
        -- </like-2.108.2>
    })

test:do_test(
    "like-2.108.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcl%'"
    end, {
        -- <like-2.108.3>
        76, 108
        -- </like-2.108.3>
    })

test:do_test(
    "like-2.109.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'm%'"
    end, {
        -- <like-2.109.1>
        77, 109
        -- </like-2.109.1>
    })

test:do_test(
    "like-2.109.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'm%'"
    end, {
        -- <like-2.109.2>
        77, 109
        -- </like-2.109.2>
    })

test:do_test(
    "like-2.109.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcm%'"
    end, {
        -- <like-2.109.3>
        77, 109
        -- </like-2.109.3>
    })

test:do_test(
    "like-2.110.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'n%'"
    end, {
        -- <like-2.110.1>
        78, 110
        -- </like-2.110.1>
    })

test:do_test(
    "like-2.110.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'n%'"
    end, {
        -- <like-2.110.2>
        78, 110
        -- </like-2.110.2>
    })

test:do_test(
    "like-2.110.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcn%'"
    end, {
        -- <like-2.110.3>
        78, 110
        -- </like-2.110.3>
    })

test:do_test(
    "like-2.111.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'o%'"
    end, {
        -- <like-2.111.1>
        79, 111
        -- </like-2.111.1>
    })

test:do_test(
    "like-2.111.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'o%'"
    end, {
        -- <like-2.111.2>
        79, 111
        -- </like-2.111.2>
    })

test:do_test(
    "like-2.111.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abco%'"
    end, {
        -- <like-2.111.3>
        79, 111
        -- </like-2.111.3>
    })

test:do_test(
    "like-2.112.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'p%'"
    end, {
        -- <like-2.112.1>
        80, 112
        -- </like-2.112.1>
    })

test:do_test(
    "like-2.112.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'p%'"
    end, {
        -- <like-2.112.2>
        80, 112
        -- </like-2.112.2>
    })

test:do_test(
    "like-2.112.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcp%'"
    end, {
        -- <like-2.112.3>
        80, 112
        -- </like-2.112.3>
    })

test:do_test(
    "like-2.113.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'q%'"
    end, {
        -- <like-2.113.1>
        81, 113
        -- </like-2.113.1>
    })

test:do_test(
    "like-2.113.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'q%'"
    end, {
        -- <like-2.113.2>
        81, 113
        -- </like-2.113.2>
    })

test:do_test(
    "like-2.113.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcq%'"
    end, {
        -- <like-2.113.3>
        81, 113
        -- </like-2.113.3>
    })

test:do_test(
    "like-2.114.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'r%'"
    end, {
        -- <like-2.114.1>
        82, 114
        -- </like-2.114.1>
    })

test:do_test(
    "like-2.114.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'r%'"
    end, {
        -- <like-2.114.2>
        82, 114
        -- </like-2.114.2>
    })

test:do_test(
    "like-2.114.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcr%'"
    end, {
        -- <like-2.114.3>
        82, 114
        -- </like-2.114.3>
    })

test:do_test(
    "like-2.115.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 's%'"
    end, {
        -- <like-2.115.1>
        83, 115
        -- </like-2.115.1>
    })

test:do_test(
    "like-2.115.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 's%'"
    end, {
        -- <like-2.115.2>
        83, 115
        -- </like-2.115.2>
    })

test:do_test(
    "like-2.115.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcs%'"
    end, {
        -- <like-2.115.3>
        83, 115
        -- </like-2.115.3>
    })

test:do_test(
    "like-2.116.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 't%'"
    end, {
        -- <like-2.116.1>
        84, 116
        -- </like-2.116.1>
    })

test:do_test(
    "like-2.116.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 't%'"
    end, {
        -- <like-2.116.2>
        84, 116
        -- </like-2.116.2>
    })

test:do_test(
    "like-2.116.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abct%'"
    end, {
        -- <like-2.116.3>
        84, 116
        -- </like-2.116.3>
    })

test:do_test(
    "like-2.117.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'u%'"
    end, {
        -- <like-2.117.1>
        85, 117
        -- </like-2.117.1>
    })

test:do_test(
    "like-2.117.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'u%'"
    end, {
        -- <like-2.117.2>
        85, 117
        -- </like-2.117.2>
    })

test:do_test(
    "like-2.117.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcu%'"
    end, {
        -- <like-2.117.3>
        85, 117
        -- </like-2.117.3>
    })

test:do_test(
    "like-2.118.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'v%'"
    end, {
        -- <like-2.118.1>
        86, 118
        -- </like-2.118.1>
    })

test:do_test(
    "like-2.118.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'v%'"
    end, {
        -- <like-2.118.2>
        86, 118
        -- </like-2.118.2>
    })

test:do_test(
    "like-2.118.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcv%'"
    end, {
        -- <like-2.118.3>
        86, 118
        -- </like-2.118.3>
    })

test:do_test(
    "like-2.119.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'w%'"
    end, {
        -- <like-2.119.1>
        87, 119
        -- </like-2.119.1>
    })

test:do_test(
    "like-2.119.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'w%'"
    end, {
        -- <like-2.119.2>
        87, 119
        -- </like-2.119.2>
    })

test:do_test(
    "like-2.119.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcw%'"
    end, {
        -- <like-2.119.3>
        87, 119
        -- </like-2.119.3>
    })

test:do_test(
    "like-2.120.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'x%'"
    end, {
        -- <like-2.120.1>
        88, 120
        -- </like-2.120.1>
    })

test:do_test(
    "like-2.120.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'x%'"
    end, {
        -- <like-2.120.2>
        88, 120
        -- </like-2.120.2>
    })

test:do_test(
    "like-2.120.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcx%'"
    end, {
        -- <like-2.120.3>
        88, 120
        -- </like-2.120.3>
    })

test:do_test(
    "like-2.121.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'y%'"
    end, {
        -- <like-2.121.1>
        89, 121
        -- </like-2.121.1>
    })

test:do_test(
    "like-2.121.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'y%'"
    end, {
        -- <like-2.121.2>
        89, 121
        -- </like-2.121.2>
    })

test:do_test(
    "like-2.121.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcy%'"
    end, {
        -- <like-2.121.3>
        89, 121
        -- </like-2.121.3>
    })

test:do_test(
    "like-2.122.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE 'z%'"
    end, {
        -- <like-2.122.1>
        90, 122
        -- </like-2.122.1>
    })

test:do_test(
    "like-2.122.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE 'z%'"
    end, {
        -- <like-2.122.2>
        90, 122
        -- </like-2.122.2>
    })

test:do_test(
    "like-2.122.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abcz%'"
    end, {
        -- <like-2.122.3>
        90, 122
        -- </like-2.122.3>
    })

test:do_test(
    "like-2.123.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '{%'"
    end, {
        -- <like-2.123.1>
        123
        -- </like-2.123.1>
    })

test:do_test(
    "like-2.123.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '{%'"
    end, {
        -- <like-2.123.2>
        123
        -- </like-2.123.2>
    })

test:do_test(
    "like-2.123.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc{%'"
    end, {
        -- <like-2.123.3>
        123
        -- </like-2.123.3>
    })

test:do_test(
    "like-2.124.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '|%'"
    end, {
        -- <like-2.124.1>
        124
        -- </like-2.124.1>
    })

test:do_test(
    "like-2.124.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '|%'"
    end, {
        -- <like-2.124.2>
        124
        -- </like-2.124.2>
    })

test:do_test(
    "like-2.124.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc|%'"
    end, {
        -- <like-2.124.3>
        124
        -- </like-2.124.3>
    })

test:do_test(
    "like-2.125.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '}%'"
    end, {
        -- <like-2.125.1>
        125
        -- </like-2.125.1>
    })

test:do_test(
    "like-2.125.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '}%'"
    end, {
        -- <like-2.125.2>
        125
        -- </like-2.125.2>
    })

test:do_test(
    "like-2.125.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc}%'"
    end, {
        -- <like-2.125.3>
        125
        -- </like-2.125.3>
    })

test:do_test(
    "like-2.126.1",
    function()
        return test:execsql "SELECT x FROM t1 WHERE y LIKE '~%'"
    end, {
        -- <like-2.126.1>
        126
        -- </like-2.126.1>
    })

test:do_test(
    "like-2.126.2",
    function()
        return test:execsql "SELECT x FROM t2 WHERE y LIKE '~%'"
    end, {
        -- <like-2.126.2>
        126
        -- </like-2.126.2>
    })

test:do_test(
    "like-2.126.3",
    function()
        return test:execsql "SELECT x FROM t3 WHERE y LIKE 'abc~%'"
    end, {
        -- <like-2.126.3>
        126
        -- </like-2.126.3>
    })



test:finish_test()
