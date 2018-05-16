#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(2022)

--!./tcltestrunner.lua
-- 2008 December 23
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
-- focus of this file is testing the multi-index OR clause optimizer.
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- Evaluate SQL.  Return the result set followed by the
-- and the number of full-scan steps.
--
local function count_steps(sql)
    local result = test:execsql(sql)
    return result
end

local function count_steps_sort(sql)
    local r = test:execsql(sql)
    table.sort(r)
    table.insert(r,"scan")
    table.insert(r,0)
    table.insert(r,"sort")
    table.insert(r,0)
    return r
end

-- Build some test data
--
test:do_execsql_test(
    "where7-1.1",
    [[
        CREATE TABLE t1(a INTEGER PRIMARY KEY,b,c,d);
        INSERT INTO t1 VALUES(1,2,3,4);
        INSERT INTO t1 VALUES(2,3,4,5);
        INSERT INTO t1 VALUES(3,4,6,8);
        INSERT INTO t1 VALUES(4,5,10,15);
        INSERT INTO t1 VALUES(5,10,100,1000);
        CREATE INDEX t1b ON t1(b);
        CREATE INDEX t1c ON t1(c);
        SELECT * FROM t1;
    ]], {
        -- <where7-1.1>
        1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 6, 8, 4, 5, 10, 15, 5, 10, 100, 1000
        -- </where7-1.1>
    })

test:do_test(
    "where7-1.2",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE b=3 OR c=6 ORDER BY a
  ]])
    end, {
        -- <where7-1.2>
        2, 3
        -- </where7-1.2>
    })

test:do_test(
    "where7-1.3",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE b=3 OR +c=6 ORDER BY a
  ]])
    end, {
        -- <where7-1.3>
        2, 3
        -- </where7-1.3>
    })

test:do_test(
    "where7-1.4",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE +b=3 OR c=6 ORDER BY 1
  ]])
    end, {
        -- <where7-1.4>
        2, 3
        -- </where7-1.4>
    })

test:do_test(
    "where7-1.5",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE 3=b OR c=6 ORDER BY a
  ]])
    end, {
        -- <where7-1.5>
        2, 3
        -- </where7-1.5>
    })

test:do_test(
    "where7-1.6",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE (3=b OR c=6) AND +a>0 ORDER BY a
  ]])
    end, {
        -- <where7-1.6>
        2, 3
        -- </where7-1.6>
    })

test:do_test(
    "where7-1.7",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE (b=3 OR c>10)
  ]])
    end, {
        -- <where7-1.7>
        2, 5
        -- </where7-1.7>
    })

test:do_test(
    "where7-1.8",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE (b=3 OR c>=10);
  ]])
    end, {
        -- <where7-1.8>
        2, 4, 5
        -- </where7-1.8>
    })

test:do_test(
    "where7-1.9",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE (b=3 OR c>=10 OR c=4)
  ]])
    end, {
        -- <where7-1.9>
        2, 4, 5
        -- </where7-1.9>
    })

test:do_test(
    "where7-1.10",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE (b=3 OR c>=10 OR c=4 OR b>10)
  ]])
    end, {
        -- <where7-1.10>
        2, 4, 5
        -- </where7-1.10>
    })

test:do_test(
    "where7-1.11",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE (d=5 AND b=3) OR c==100 ORDER BY a;
  ]])
    end, {
        -- <where7-1.11>
        2, 5
        -- </where7-1.11>
    })

test:do_test(
    "where7-1.12",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE (b BETWEEN 2 AND 4) OR c=100 ORDER BY a
  ]])
    end, {
        -- <where7-1.12>
        1, 2, 3, 5
        -- </where7-1.12>
    })

test:do_test(
    "where7-1.13",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE (b BETWEEN 0 AND 2) OR (c BETWEEN 9 AND 999)
    ORDER BY +a DESC
  ]])
    end, {
        -- <where7-1.13>
        5, 4, 1
        -- </where7-1.13>
    })

test:do_test(
    "where7-1.14",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE (d=8 OR c=6 OR b=4) AND +a>0
  ]])
    end, {
        -- <where7-1.14>
        3
        -- </where7-1.14>
    })

test:do_test(
    "where7-1.15",
    function()
        return count_steps([[
    SELECT a FROM t1 WHERE +a>=0 AND (d=8 OR c=6 OR b=4)
  ]])
    end, {
        -- <where7-1.15>
        3
        -- </where7-1.15>
    })

test:do_test(
    "where7-1.20",
    function()
        sql = "SELECT a FROM t1 WHERE a=11 OR b=11"
        for i = 12, 400 - 1, 1 do
            sql = sql .. string.format(" OR a=%s OR b=%s", i, i)
        end
        sql = sql .. " ORDER BY a"
        return count_steps(sql)
    end, {
        -- <where7-1.20>
        
        -- </where7-1.20>
    })

test:do_test(
    "where7-1.21",
    function()
        sql = "SELECT a FROM t1 WHERE b=11 OR c=11"
        for i = 12, 400 - 1, 1 do
            sql = sql .. string.format(" OR b=%s OR c=%s", i, i)
        end
        sql = sql .. " ORDER BY a"
        return count_steps(sql)
    end, {
        -- <where7-1.21>
        5
        -- </where7-1.21>
    })

test:do_test(
    "where7-1.22",
    function()
        sql = "SELECT a FROM t1 WHERE (b=11 OR c=11"
        for i = 12, 400 - 1, 1 do
            sql = sql .. string.format(" OR b=%s OR c=%s", i, i)
        end
        sql = sql .. ") AND d>=0 AND d<9999 ORDER BY a"
        return count_steps(sql)
    end, {
        -- <where7-1.22>
        5
        -- </where7-1.22>
    })

test:do_test(
    "where7-1.23",
    function()
        sql = "SELECT a FROM t1 WHERE (b=11 OR c=11"
        for i = 12, 400 - 1, 1 do
            sql = sql .. string.format(" OR (b=%s AND d!=0) OR (c=%s AND d IS NOT NULL)", i, i)
        end
        sql = sql .. ") AND d>=0 AND d<9999 ORDER BY a"
        return count_steps(sql)
    end, {
        -- <where7-1.23>
        5
        -- </where7-1.23>
    })

test:do_test(
    "where7-1.31",
    function()
        sql = "SELECT a FROM t1 WHERE (a=11 AND b=11)"
        for i = 12, 400 - 1, 1 do
            sql = sql .. string.format(" OR (a=%s AND b=%s)", i, i)
        end
        sql = sql .. " ORDER BY a"
        return count_steps(sql)
    end, {
        -- <where7-1.31>
        
        -- </where7-1.31>
    })

test:do_test(
    "where7-1.32",
    function()
        sql = "SELECT a FROM t1 WHERE (b=11 AND c=11)"
        for i = 12, 400 - 1, 1 do
            sql = sql .. string.format(" OR (b=%s AND c=%s)", i, i)
        end
        sql = sql .. " ORDER BY a"
        return count_steps(sql)
    end, {
        -- <where7-1.32>
        
        -- </where7-1.32>
    })

test:do_test(
    "where7-2.1",
    function()
        return test:execsql [[
            CREATE TABLE t2(a INTEGER PRIMARY KEY,b,c,d,e,f TEXT,g);
            INSERT INTO t2 VALUES(1,11,1001,1.001,100.1,'bcdefghij','yxwvuts');
            INSERT INTO t2 VALUES(2,22,1001,2.002,100.1,'cdefghijk','yxwvuts');
            INSERT INTO t2 VALUES(3,33,1001,3.0029999999999997,100.1,'defghijkl','xwvutsr');
            INSERT INTO t2 VALUES(4,44,2002,4.004,200.2,'efghijklm','xwvutsr');
            INSERT INTO t2 VALUES(5,55,2002,5.004999999999999,200.2,'fghijklmn','xwvutsr');
            INSERT INTO t2 VALUES(6,66,2002,6.005999999999999,200.2,'ghijklmno','xwvutsr');
            INSERT INTO t2 VALUES(7,77,3003,7.007,300.29999999999995,'hijklmnop','xwvutsr');
            INSERT INTO t2 VALUES(8,88,3003,8.008,300.29999999999995,'ijklmnopq','wvutsrq');
            INSERT INTO t2 VALUES(9,99,3003,9.008999999999999,300.29999999999995,'jklmnopqr','wvutsrq');
            INSERT INTO t2 VALUES(10,110,4004,10.009999999999998,400.4,'klmnopqrs','wvutsrq');
            INSERT INTO t2 VALUES(11,121,4004,11.011,400.4,'lmnopqrst','wvutsrq');
            INSERT INTO t2 VALUES(12,132,4004,12.011999999999999,400.4,'mnopqrstu','wvutsrq');
            INSERT INTO t2 VALUES(13,143,5005,13.012999999999998,500.5,'nopqrstuv','vutsrqp');
            INSERT INTO t2 VALUES(14,154,5005,14.014,500.5,'opqrstuvw','vutsrqp');
            INSERT INTO t2 VALUES(15,165,5005,15.014999999999999,500.5,'pqrstuvwx','vutsrqp');
            INSERT INTO t2 VALUES(16,176,6006,16.016,600.5999999999999,'qrstuvwxy','vutsrqp');
            INSERT INTO t2 VALUES(17,187,6006,17.017,600.5999999999999,'rstuvwxyz','vutsrqp');
            INSERT INTO t2 VALUES(18,198,6006,18.017999999999997,600.5999999999999,'stuvwxyza','utsrqpo');
            INSERT INTO t2 VALUES(19,209,7007,19.019,700.6999999999999,'tuvwxyzab','utsrqpo');
            INSERT INTO t2 VALUES(20,220,7007,20.019999999999996,700.6999999999999,'uvwxyzabc','utsrqpo');
            INSERT INTO t2 VALUES(21,231,7007,21.020999999999997,700.6999999999999,'vwxyzabcd','utsrqpo');
            INSERT INTO t2 VALUES(22,242,8008,22.022,800.8,'wxyzabcde','utsrqpo');
            INSERT INTO t2 VALUES(23,253,8008,23.022999999999996,800.8,'xyzabcdef','tsrqpon');
            INSERT INTO t2 VALUES(24,264,8008,24.023999999999997,800.8,'yzabcdefg','tsrqpon');
            INSERT INTO t2 VALUES(25,275,9009,25.025,900.9,'zabcdefgh','tsrqpon');
            INSERT INTO t2 VALUES(26,286,9009,26.025999999999996,900.9,'abcdefghi','tsrqpon');
            INSERT INTO t2 VALUES(27,297,9009,27.026999999999997,900.9,'bcdefghij','tsrqpon');
            INSERT INTO t2 VALUES(28,308,10010,28.028,1001.0,'cdefghijk','srqponm');
            INSERT INTO t2 VALUES(29,319,10010,29.028999999999996,1001.0,'defghijkl','srqponm');
            INSERT INTO t2 VALUES(30,330,10010,30.029999999999998,1001.0,'efghijklm','srqponm');
            INSERT INTO t2 VALUES(31,341,11011,31.030999999999995,1101.1,'fghijklmn','srqponm');
            INSERT INTO t2 VALUES(32,352,11011,32.032,1101.1,'ghijklmno','srqponm');
            INSERT INTO t2 VALUES(33,363,11011,33.032999999999994,1101.1,'hijklmnop','rqponml');
            INSERT INTO t2 VALUES(34,374,12012,34.034,1201.1999999999998,'ijklmnopq','rqponml');
            INSERT INTO t2 VALUES(35,385,12012,35.035,1201.1999999999998,'jklmnopqr','rqponml');
            INSERT INTO t2 VALUES(36,396,12012,36.035999999999994,1201.1999999999998,'klmnopqrs','rqponml');
            INSERT INTO t2 VALUES(37,407,13013,37.037,1301.3,'lmnopqrst','rqponml');
            INSERT INTO t2 VALUES(38,418,13013,38.038,1301.3,'mnopqrstu','qponmlk');
            INSERT INTO t2 VALUES(39,429,13013,39.038999999999994,1301.3,'nopqrstuv','qponmlk');
            INSERT INTO t2 VALUES(40,440,14014,40.03999999999999,1401.3999999999999,'opqrstuvw','qponmlk');
            INSERT INTO t2 VALUES(41,451,14014,41.041,1401.3999999999999,'pqrstuvwx','qponmlk');
            INSERT INTO t2 VALUES(42,462,14014,42.041999999999994,1401.3999999999999,'qrstuvwxy','qponmlk');
            INSERT INTO t2 VALUES(43,473,15015,43.04299999999999,1501.5,'rstuvwxyz','ponmlkj');
            INSERT INTO t2 VALUES(44,484,15015,44.044,1501.5,'stuvwxyza','ponmlkj');
            INSERT INTO t2 VALUES(45,495,15015,45.044999999999995,1501.5,'tuvwxyzab','ponmlkj');
            INSERT INTO t2 VALUES(46,506,16016,46.04599999999999,1601.6,'uvwxyzabc','ponmlkj');
            INSERT INTO t2 VALUES(47,517,16016,47.047,1601.6,'vwxyzabcd','ponmlkj');
            INSERT INTO t2 VALUES(48,528,16016,48.047999999999995,1601.6,'wxyzabcde','onmlkji');
            INSERT INTO t2 VALUES(49,539,17017,49.04899999999999,1701.6999999999998,'xyzabcdef','onmlkji');
            INSERT INTO t2 VALUES(50,550,17017,50.05,1701.6999999999998,'yzabcdefg','onmlkji');
            INSERT INTO t2 VALUES(51,561,17017,51.050999999999995,1701.6999999999998,'zabcdefgh','onmlkji');
            INSERT INTO t2 VALUES(52,572,18018,52.05199999999999,1801.8,'abcdefghi','onmlkji');
            INSERT INTO t2 VALUES(53,583,18018,53.053,1801.8,'bcdefghij','nmlkjih');
            INSERT INTO t2 VALUES(54,594,18018,54.053999999999995,1801.8,'cdefghijk','nmlkjih');
            INSERT INTO t2 VALUES(55,605,19019,55.05499999999999,1901.8999999999999,'defghijkl','nmlkjih');
            INSERT INTO t2 VALUES(56,616,19019,56.056,1901.8999999999999,'efghijklm','nmlkjih');
            INSERT INTO t2 VALUES(57,627,19019,57.056999999999995,1901.8999999999999,'fghijklmn','nmlkjih');
            INSERT INTO t2 VALUES(58,638,20020,58.05799999999999,2002.0,'ghijklmno','mlkjihg');
            INSERT INTO t2 VALUES(59,649,20020,59.05899999999999,2002.0,'hijklmnop','mlkjihg');
            INSERT INTO t2 VALUES(60,660,20020,60.059999999999995,2002.0,'ijklmnopq','mlkjihg');
            INSERT INTO t2 VALUES(61,671,21021,61.06099999999999,2102.1,'jklmnopqr','mlkjihg');
            INSERT INTO t2 VALUES(62,682,21021,62.06199999999999,2102.1,'klmnopqrs','mlkjihg');
            INSERT INTO t2 VALUES(63,693,21021,63.062999999999995,2102.1,'lmnopqrst','lkjihgf');
            INSERT INTO t2 VALUES(64,704,22022,64.064,2202.2,'mnopqrstu','lkjihgf');
            INSERT INTO t2 VALUES(65,715,22022,65.065,2202.2,'nopqrstuv','lkjihgf');
            INSERT INTO t2 VALUES(66,726,22022,66.06599999999999,2202.2,'opqrstuvw','lkjihgf');
            INSERT INTO t2 VALUES(67,737,23023,67.067,2302.2999999999997,'pqrstuvwx','lkjihgf');
            INSERT INTO t2 VALUES(68,748,23023,68.068,2302.2999999999997,'qrstuvwxy','kjihgfe');
            INSERT INTO t2 VALUES(69,759,23023,69.06899999999999,2302.2999999999997,'rstuvwxyz','kjihgfe');
            INSERT INTO t2 VALUES(70,770,24024,70.07,2402.3999999999996,'stuvwxyza','kjihgfe');
            INSERT INTO t2 VALUES(71,781,24024,71.071,2402.3999999999996,'tuvwxyzab','kjihgfe');
            INSERT INTO t2 VALUES(72,792,24024,72.07199999999999,2402.3999999999996,'uvwxyzabc','kjihgfe');
            INSERT INTO t2 VALUES(73,803,25025,73.073,2502.5,'vwxyzabcd','jihgfed');
            INSERT INTO t2 VALUES(74,814,25025,74.074,2502.5,'wxyzabcde','jihgfed');
            INSERT INTO t2 VALUES(75,825,25025,75.07499999999999,2502.5,'xyzabcdef','jihgfed');
            INSERT INTO t2 VALUES(76,836,26026,76.076,2602.6,'yzabcdefg','jihgfed');
            INSERT INTO t2 VALUES(77,847,26026,77.077,2602.6,'zabcdefgh','jihgfed');
            INSERT INTO t2 VALUES(78,858,26026,78.07799999999999,2602.6,'abcdefghi','ihgfedc');
            INSERT INTO t2 VALUES(79,869,27027,79.079,2702.7,'bcdefghij','ihgfedc');
            INSERT INTO t2 VALUES(80,880,27027,80.07999999999998,2702.7,'cdefghijk','ihgfedc');
            INSERT INTO t2 VALUES(81,891,27027,81.08099999999999,2702.7,'defghijkl','ihgfedc');
            INSERT INTO t2 VALUES(82,902,28028,82.082,2802.7999999999997,'efghijklm','ihgfedc');
            INSERT INTO t2 VALUES(83,913,28028,83.08299999999998,2802.7999999999997,'fghijklmn','hgfedcb');
            INSERT INTO t2 VALUES(84,924,28028,84.08399999999999,2802.7999999999997,'ghijklmno','hgfedcb');
            INSERT INTO t2 VALUES(85,935,29029,85.085,2902.8999999999996,'hijklmnop','hgfedcb');
            INSERT INTO t2 VALUES(86,946,29029,86.08599999999998,2902.8999999999996,'ijklmnopq','hgfedcb');
            INSERT INTO t2 VALUES(87,957,29029,87.08699999999999,2902.8999999999996,'jklmnopqr','hgfedcb');
            INSERT INTO t2 VALUES(88,968,30030,88.088,3003.0,'klmnopqrs','gfedcba');
            INSERT INTO t2 VALUES(89,979,30030,89.08899999999998,3003.0,'lmnopqrst','gfedcba');
            INSERT INTO t2 VALUES(90,990,30030,90.08999999999999,3003.0,'mnopqrstu','gfedcba');
            INSERT INTO t2 VALUES(91,1001,31031,91.091,3103.1,'nopqrstuv','gfedcba');
            INSERT INTO t2 VALUES(92,1012,31031,92.09199999999998,3103.1,'opqrstuvw','gfedcba');
            INSERT INTO t2 VALUES(93,1023,31031,93.09299999999999,3103.1,'pqrstuvwx','fedcbaz');
            INSERT INTO t2 VALUES(94,1034,32032,94.094,3203.2,'qrstuvwxy','fedcbaz');
            INSERT INTO t2 VALUES(95,1045,32032,95.09499999999998,3203.2,'rstuvwxyz','fedcbaz');
            INSERT INTO t2 VALUES(96,1056,32032,96.09599999999999,3203.2,'stuvwxyza','fedcbaz');
            INSERT INTO t2 VALUES(97,1067,33033,97.097,3303.2999999999997,'tuvwxyzab','fedcbaz');
            INSERT INTO t2 VALUES(98,1078,33033,98.09799999999998,3303.2999999999997,'uvwxyzabc','edcbazy');
            INSERT INTO t2 VALUES(99,1089,33033,99.09899999999999,3303.2999999999997,'vwxyzabcd','edcbazy');
            INSERT INTO t2 VALUES(100,1100,34034,100.1,3403.3999999999996,'wxyzabcde','edcbazy');
            CREATE INDEX t2b ON t2(b);
            CREATE INDEX t2c ON t2(c);
            CREATE INDEX t2d ON t2(d);
            CREATE INDEX t2e ON t2(e);
            CREATE INDEX t2f ON t2(f);
            CREATE INDEX t2g ON t2(g);
            CREATE TABLE t3(a INTEGER PRIMARY KEY,b,c,d,e,f TEXT,g);
            INSERT INTO t3 SELECT * FROM t2;
            CREATE INDEX t3b ON t3(b,c);
            CREATE INDEX t3c ON t3(c,e);
            CREATE INDEX t3d ON t3(d,g);
            CREATE INDEX t3e ON t3(e,f,g);
            CREATE INDEX t3f ON t3(f,b,d,c);
            CREATE INDEX t3g ON t3(g,f);
        ]]
    end, {
        -- <where7-2.1>
        
        -- </where7-2.1>
    })

test:do_test(
    "where7-2.2.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1070
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
  ]])
    end, {
        -- <where7-2.2.1>
        6, 18, 20, 32, 39, 58, 84, 89, 96, 100, "scan", 0, "sort", 0
        -- </where7-2.2.1>
    })

test:do_test(
    "where7-2.2.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1070
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
  ]])
    end, {
        -- <where7-2.2.2>
        6, 18, 20, 32, 39, 58, 84, 89, 96, 100, "scan", 0, "sort", 0
        -- </where7-2.2.2>
    })

test:do_test(
    "where7-2.3.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
         OR b=220
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR (g='qponmlk' AND f GLOB 'pqrst*')
  ]])
    end, {
        -- <where7-2.3.1>
        20, 33, 35, 41, 47, 67, 69, 70, 98, "scan", 0, "sort", 0
        -- </where7-2.3.1>
    })

test:do_test(
    "where7-2.3.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
         OR b=220
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR (g='qponmlk' AND f GLOB 'pqrst*')
  ]])
    end, {
        -- <where7-2.3.2>
        20, 33, 35, 41, 47, 67, 69, 70, 98, "scan", 0, "sort", 0
        -- </where7-2.3.2>
    })

test:do_test(
    "where7-2.4.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=190
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR (g='rqponml' AND f GLOB 'hijkl*')
         OR b=407
  ]])
    end, {
        -- <where7-2.4.1>
        33, 37, 49, 51, "scan", 0, "sort", 0
        -- </where7-2.4.1>
    })

test:do_test(
    "where7-2.4.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=190
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR (g='rqponml' AND f GLOB 'hijkl*')
         OR b=407
  ]])
    end, {
        -- <where7-2.4.2>
        33, 37, 49, 51, "scan", 0, "sort", 0
        -- </where7-2.4.2>
    })

test:do_test(
    "where7-2.5.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR b=795
         OR b=1103
         OR b=583
  ]])
    end, {
        -- <where7-2.5.1>
        13, 39, 53, 65, 91, "scan", 0, "sort", 0
        -- </where7-2.5.1>
    })

test:do_test(
    "where7-2.5.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR b=795
         OR b=1103
         OR b=583
  ]])
    end, {
        -- <where7-2.5.2>
        13, 39, 53, 65, 91, "scan", 0, "sort", 0
        -- </where7-2.5.2>
    })

test:do_test(
    "where7-2.6.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=74
         OR a=50
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR ((a BETWEEN 16 AND 18) AND a!=17)
         OR c=21021
         OR ((a BETWEEN 82 AND 84) AND a!=83)
  ]])
    end, {
        -- <where7-2.6.1>
        16, 18, 50, 61, 62, 63, 74, 82, 84, 85, "scan", 0, "sort", 0
        -- </where7-2.6.1>
    })

test:do_test(
    "where7-2.6.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=74
         OR a=50
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR ((a BETWEEN 16 AND 18) AND a!=17)
         OR c=21021
         OR ((a BETWEEN 82 AND 84) AND a!=83)
  ]])
    end, {
        -- <where7-2.6.2>
        16, 18, 50, 61, 62, 63, 74, 82, 84, 85, "scan", 0, "sort", 0
        -- </where7-2.6.2>
    })

test:do_test(
    "where7-2.7.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 8 AND 10) AND a!=9)
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR ((a BETWEEN 34 AND 36) AND a!=35)
         OR c=14014
         OR b=828
  ]])
    end, {
        -- <where7-2.7.1>
        8, 10, 34, 36, 40, 41, 42, 94, "scan", 0, "sort", 0
        -- </where7-2.7.1>
    })

test:do_test(
    "where7-2.7.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 8 AND 10) AND a!=9)
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR ((a BETWEEN 34 AND 36) AND a!=35)
         OR c=14014
         OR b=828
  ]])
    end, {
        -- <where7-2.7.2>
        8, 10, 34, 36, 40, 41, 42, 94, "scan", 0, "sort", 0
        -- </where7-2.7.2>
    })

test:do_test(
    "where7-2.8.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE 1000000<b
         OR b=308
  ]])
    end, {
        -- <where7-2.8.1>
        28, "scan", 0, "sort", 0
        -- </where7-2.8.1>
    })

test:do_test(
    "where7-2.8.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE 1000000<b
         OR b=308
  ]])
    end, {
        -- <where7-2.8.2>
        28, "scan", 0, "sort", 0
        -- </where7-2.8.2>
    })

test:do_test(
    "where7-2.9.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=949
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR a=63
  ]])
    end, {
        -- <where7-2.9.1>
        22, 24, 63, "scan", 0, "sort", 0
        -- </where7-2.9.1>
    })

test:do_test(
    "where7-2.9.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=949
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR a=63
  ]])
    end, {
        -- <where7-2.9.2>
        22, 24, 63, "scan", 0, "sort", 0
        -- </where7-2.9.2>
    })

test:do_test(
    "where7-2.10.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 49 AND 51) AND a!=50)
         OR b=396
         OR ((a BETWEEN 68 AND 70) AND a!=69)
  ]])
    end, {
        -- <where7-2.10.1>
        36, 49, 51, 68, 70, "scan", 0, "sort", 0
        -- </where7-2.10.1>
    })

test:do_test(
    "where7-2.10.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 49 AND 51) AND a!=50)
         OR b=396
         OR ((a BETWEEN 68 AND 70) AND a!=69)
  ]])
    end, {
        -- <where7-2.10.2>
        36, 49, 51, 68, 70, "scan", 0, "sort", 0
        -- </where7-2.10.2>
    })

test:do_test(
    "where7-2.11.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR c=11011
         OR c=20020
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.11.1>
        18, 31, 32, 33, 58, 59, 60, 72, 74, "scan", 0, "sort", 0
        -- </where7-2.11.1>
    })

test:do_test(
    "where7-2.11.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR c=11011
         OR c=20020
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.11.2>
        18, 31, 32, 33, 58, 59, 60, 72, 74, "scan", 0, "sort", 0
        -- </where7-2.11.2>
    })

test:do_test(
    "where7-2.12.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR (d>=83.0 AND d<84.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR b=792
         OR a=97
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR b=916
         OR a=69
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR c=6006
  ]])
    end, {
        -- <where7-2.12.1>
        16, 17, 18, 31, 50, 69, 72, 81, 83, 87, 97, "scan", 0, "sort", 0
        -- </where7-2.12.1>
    })

test:do_test(
    "where7-2.12.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR (d>=83.0 AND d<84.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR b=792
         OR a=97
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR b=916
         OR a=69
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR c=6006
  ]])
    end, {
        -- <where7-2.12.2>
        16, 17, 18, 31, 50, 69, 72, 81, 83, 87, 97, "scan", 0, "sort", 0
        -- </where7-2.12.2>
    })

test:do_test(
    "where7-2.13.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 50 AND 52) AND a!=51)
         OR c=9009
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR b=539
         OR b=297
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR b=957
         OR f='xyzabcdef'
         OR b=619
  ]])
    end, {
        -- <where7-2.13.1>
        10, 15, 21, 23, 25, 26, 27, 49, 50, 52, 75, 87, "scan", 0, "sort", 0
        -- </where7-2.13.1>
    })

test:do_test(
    "where7-2.13.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 50 AND 52) AND a!=51)
         OR c=9009
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR b=539
         OR b=297
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR b=957
         OR f='xyzabcdef'
         OR b=619
  ]])
    end, {
        -- <where7-2.13.2>
        10, 15, 21, 23, 25, 26, 27, 49, 50, 52, 75, 87, "scan", 0, "sort", 0
        -- </where7-2.13.2>
    })

test:do_test(
    "where7-2.14.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 47 AND 49) AND a!=48)
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.14.1>
        47, 48, 49, "scan", 0, "sort", 0
        -- </where7-2.14.1>
    })

test:do_test(
    "where7-2.14.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 47 AND 49) AND a!=48)
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.14.2>
        47, 48, 49, "scan", 0, "sort", 0
        -- </where7-2.14.2>
    })

test:do_test(
    "where7-2.15.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=20
         OR a=67
         OR b=58
         OR ((a BETWEEN 19 AND 21) AND a!=20)
  ]])
    end, {
        -- <where7-2.15.1>
        19, 20, 21, 67, "scan", 0, "sort", 0
        -- </where7-2.15.1>
    })

test:do_test(
    "where7-2.15.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=20
         OR a=67
         OR b=58
         OR ((a BETWEEN 19 AND 21) AND a!=20)
  ]])
    end, {
        -- <where7-2.15.2>
        19, 20, 21, 67, "scan", 0, "sort", 0
        -- </where7-2.15.2>
    })

test:do_test(
    "where7-2.16.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=938
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.16.1>
        17, 67, "scan", 0, "sort", 0
        -- </where7-2.16.1>
    })

test:do_test(
    "where7-2.16.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=938
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.16.2>
        17, 67, "scan", 0, "sort", 0
        -- </where7-2.16.2>
    })

test:do_test(
    "where7-2.17.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=68.0 AND d<69.0 AND d IS NOT NULL)
         OR f='zabcdefgh'
         OR b=308
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR ((a BETWEEN 15 AND 17) AND a!=16)
         OR b=443
         OR ((a BETWEEN 12 AND 14) AND a!=13)
         OR f='uvwxyzabc'
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
  ]])
    end, {
        -- <where7-2.17.1>
        12, 14, 15, 16, 17, 20, 24, 25, 28, 29, 46, 50, 51, 68, 72, 76, 77, 98, "scan", 0, "sort", 0
        -- </where7-2.17.1>
    })

test:do_test(
    "where7-2.17.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=68.0 AND d<69.0 AND d IS NOT NULL)
         OR f='zabcdefgh'
         OR b=308
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR ((a BETWEEN 15 AND 17) AND a!=16)
         OR b=443
         OR ((a BETWEEN 12 AND 14) AND a!=13)
         OR f='uvwxyzabc'
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
  ]])
    end, {
        -- <where7-2.17.2>
        12, 14, 15, 16, 17, 20, 24, 25, 28, 29, 46, 50, 51, 68, 72, 76, 77, 98, "scan", 0, "sort", 0
        -- </where7-2.17.2>
    })

test:do_test(
    "where7-2.18.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR b=762
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR a=19
  ]])
    end, {
        -- <where7-2.18.1>
        19, 46, 56, "scan", 0, "sort", 0
        -- </where7-2.18.1>
    })

test:do_test(
    "where7-2.18.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR b=762
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR a=19
  ]])
    end, {
        -- <where7-2.18.2>
        19, 46, 56, "scan", 0, "sort", 0
        -- </where7-2.18.2>
    })

test:do_test(
    "where7-2.19.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR a=46
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR a=73
         OR c=20020
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR b=267
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.19.1>
        2, 4, 20, 46, 58, 59, 60, 63, 68, 70, 73, "scan", 0, "sort", 0
        -- </where7-2.19.1>
    })

test:do_test(
    "where7-2.19.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR a=46
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR a=73
         OR c=20020
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR b=267
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.19.2>
        2, 4, 20, 46, 58, 59, 60, 63, 68, 70, 73, "scan", 0, "sort", 0
        -- </where7-2.19.2>
    })

test:do_test(
    "where7-2.20.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 27 AND 29) AND a!=28)
         OR (g='gfedcba' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.20.1>
        27, 29, 91, "scan", 0, "sort", 0
        -- </where7-2.20.1>
    })

test:do_test(
    "where7-2.20.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 27 AND 29) AND a!=28)
         OR (g='gfedcba' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.20.2>
        27, 29, 91, "scan", 0, "sort", 0
        -- </where7-2.20.2>
    })

test:do_test(
    "where7-2.21.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=13013
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR f='bcdefghij'
         OR b=586
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR (d>=6.0 AND d<7.0 AND d IS NOT NULL)
         OR a=9
  ]])
    end, {
        -- <where7-2.21.1>
        1, 6, 9, 27, 37, 38, 39, 53, 55, 58, 59, 61, 75, 79, 87, 89, 98, "scan", 0, "sort", 0
        -- </where7-2.21.1>
    })

test:do_test(
    "where7-2.21.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=13013
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR f='bcdefghij'
         OR b=586
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR (d>=6.0 AND d<7.0 AND d IS NOT NULL)
         OR a=9
  ]])
    end, {
        -- <where7-2.21.2>
        1, 6, 9, 27, 37, 38, 39, 53, 55, 58, 59, 61, 75, 79, 87, 89, 98, "scan", 0, "sort", 0
        -- </where7-2.21.2>
    })

test:do_test(
    "where7-2.22.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=399
         OR c=28028
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.22.1>
        42, 82, 83, 84, 98, "scan", 0, "sort", 0
        -- </where7-2.22.1>
    })

test:do_test(
    "where7-2.22.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=399
         OR c=28028
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.22.2>
        42, 82, 83, 84, 98, "scan", 0, "sort", 0
        -- </where7-2.22.2>
    })

test:do_test(
    "where7-2.23.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='fedcbaz' AND f GLOB 'rstuv*')
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR c=14014
         OR c=33033
         OR a=89
         OR b=770
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR a=35
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR b=253
         OR c=14014
  ]])
    end, {
        -- <where7-2.23.1>
        4, 19, 23, 30, 35, 40, 41, 42, 56, 70, 82, 89, 95, 96, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.23.1>
    })

test:do_test(
    "where7-2.23.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='fedcbaz' AND f GLOB 'rstuv*')
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR c=14014
         OR c=33033
         OR a=89
         OR b=770
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR a=35
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR b=253
         OR c=14014
  ]])
    end, {
        -- <where7-2.23.2>
        4, 19, 23, 30, 35, 40, 41, 42, 56, 70, 82, 89, 95, 96, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.23.2>
    })

test:do_test(
    "where7-2.24.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=330
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR a=16
  ]])
    end, {
        -- <where7-2.24.1>
        6, 16, 21, 30, 32, 34, "scan", 0, "sort", 0
        -- </where7-2.24.1>
    })

test:do_test(
    "where7-2.24.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=330
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR a=16
  ]])
    end, {
        -- <where7-2.24.2>
        6, 16, 21, 30, 32, 34, "scan", 0, "sort", 0
        -- </where7-2.24.2>
    })

test:do_test(
    "where7-2.25.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=5005
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.25.1>
        2, 13, 14, 15, 36, 38, 47, "scan", 0, "sort", 0
        -- </where7-2.25.1>
    })

test:do_test(
    "where7-2.25.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=5005
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.25.2>
        2, 13, 14, 15, 36, 38, 47, "scan", 0, "sort", 0
        -- </where7-2.25.2>
    })

test:do_test(
    "where7-2.26.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=30.0 AND d<31.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR a=33
  ]])
    end, {
        -- <where7-2.26.1>
        30, 33, 58, 64, 66, 68, "scan", 0, "sort", 0
        -- </where7-2.26.1>
    })

test:do_test(
    "where7-2.26.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=30.0 AND d<31.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR a=33
  ]])
    end, {
        -- <where7-2.26.2>
        30, 33, 58, 64, 66, 68, "scan", 0, "sort", 0
        -- </where7-2.26.2>
    })

test:do_test(
    "where7-2.27.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1026
         OR b=410
  ]])
    end, {
        -- <where7-2.27.1>
        "scan", 0, "sort", 0
        -- </where7-2.27.1>
    })

test:do_test(
    "where7-2.27.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1026
         OR b=410
  ]])
    end, {
        -- <where7-2.27.2>
        "scan", 0, "sort", 0
        -- </where7-2.27.2>
    })

test:do_test(
    "where7-2.28.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=18018
         OR a=94
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR b=1012
         OR a=3
         OR d>1e10
         OR b=905
         OR b=1089
  ]])
    end, {
        -- <where7-2.28.1>
        3, 15, 26, 41, 52, 53, 54, 67, 92, 93, 94, 99, "scan", 0, "sort", 0
        -- </where7-2.28.1>
    })

test:do_test(
    "where7-2.28.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=18018
         OR a=94
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR b=1012
         OR a=3
         OR d>1e10
         OR b=905
         OR b=1089
  ]])
    end, {
        -- <where7-2.28.2>
        3, 15, 26, 41, 52, 53, 54, 67, 92, 93, 94, 99, "scan", 0, "sort", 0
        -- </where7-2.28.2>
    })

test:do_test(
    "where7-2.29.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=100
         OR c=11011
         OR b=297
         OR a=63
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR a=76
         OR b=1026
         OR a=26
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR c=30030
  ]])
    end, {
        -- <where7-2.29.1>
        24, 26, 27, 31, 32, 33, 50, 63, 76, 84, 88, 89, 90, 100, "scan", 0, "sort", 0
        -- </where7-2.29.1>
    })

test:do_test(
    "where7-2.29.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=100
         OR c=11011
         OR b=297
         OR a=63
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR a=76
         OR b=1026
         OR a=26
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR c=30030
  ]])
    end, {
        -- <where7-2.29.2>
        24, 26, 27, 31, 32, 33, 50, 63, 76, 84, 88, 89, 90, 100, "scan", 0, "sort", 0
        -- </where7-2.29.2>
    })

test:do_test(
    "where7-2.30.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=561
         OR b=1070
         OR a=59
         OR b=715
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
  ]])
    end, {
        -- <where7-2.30.1>
        23, 49, 51, 59, 65, 75, "scan", 0, "sort", 0
        -- </where7-2.30.1>
    })

test:do_test(
    "where7-2.30.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=561
         OR b=1070
         OR a=59
         OR b=715
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
  ]])
    end, {
        -- <where7-2.30.2>
        23, 49, 51, 59, 65, 75, "scan", 0, "sort", 0
        -- </where7-2.30.2>
    })

test:do_test(
    "where7-2.31.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=1056
         OR b=1012
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
  ]])
    end, {
        -- <where7-2.31.1>
        19, 26, 52, 57, 59, 67, 69, 78, 92, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.31.1>
    })

test:do_test(
    "where7-2.31.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=1056
         OR b=1012
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
  ]])
    end, {
        -- <where7-2.31.2>
        19, 26, 52, 57, 59, 67, 69, 78, 92, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.31.2>
    })

test:do_test(
    "where7-2.32.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='rstuvwxyz'
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.32.1>
        17, 43, 69, 74, 90, 92, 95, 98, "scan", 0, "sort", 0
        -- </where7-2.32.1>
    })

test:do_test(
    "where7-2.32.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='rstuvwxyz'
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.32.2>
        17, 43, 69, 74, 90, 92, 95, 98, "scan", 0, "sort", 0
        -- </where7-2.32.2>
    })

test:do_test(
    "where7-2.33.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR c=12012
         OR a=18
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
  ]])
    end, {
        -- <where7-2.33.1>
        9, 15, 17, 18, 26, 34, 35, 36, 41, 43, 52, 61, 67, 69, 76, 78, 87, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.33.1>
    })

test:do_test(
    "where7-2.33.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR c=12012
         OR a=18
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
  ]])
    end, {
        -- <where7-2.33.2>
        9, 15, 17, 18, 26, 34, 35, 36, 41, 43, 52, 61, 67, 69, 76, 78, 87, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.33.2>
    })

test:do_test(
    "where7-2.34.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=77
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.34.1>
        58, 77, "scan", 0, "sort", 0
        -- </where7-2.34.1>
    })

test:do_test(
    "where7-2.34.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=77
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.34.2>
        58, 77, "scan", 0, "sort", 0
        -- </where7-2.34.2>
    })

test:do_test(
    "where7-2.35.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=498
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR c=33033
         OR b=11
         OR (g='wvutsrq' AND f GLOB 'lmnop*')
         OR ((a BETWEEN 7 AND 9) AND a!=8)
  ]])
    end, {
        -- <where7-2.35.1>
        1, 7, 9, 11, 27, 67, 69, 88, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.35.1>
    })

test:do_test(
    "where7-2.35.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=498
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR c=33033
         OR b=11
         OR (g='wvutsrq' AND f GLOB 'lmnop*')
         OR ((a BETWEEN 7 AND 9) AND a!=8)
  ]])
    end, {
        -- <where7-2.35.2>
        1, 7, 9, 11, 27, 67, 69, 88, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.35.2>
    })

test:do_test(
    "where7-2.36.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=6.0 AND d<7.0 AND d IS NOT NULL)
         OR ((a BETWEEN 58 AND 60) AND a!=59)
  ]])
    end, {
        -- <where7-2.36.1>
        6, 58, 60, "scan", 0, "sort", 0
        -- </where7-2.36.1>
    })

test:do_test(
    "where7-2.36.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=6.0 AND d<7.0 AND d IS NOT NULL)
         OR ((a BETWEEN 58 AND 60) AND a!=59)
  ]])
    end, {
        -- <where7-2.36.2>
        6, 58, 60, "scan", 0, "sort", 0
        -- </where7-2.36.2>
    })

test:do_test(
    "where7-2.37.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1059
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR c=4004
         OR b=806
  ]])
    end, {
        -- <where7-2.37.1>
        10, 11, 12, 43, "scan", 0, "sort", 0
        -- </where7-2.37.1>
    })

test:do_test(
    "where7-2.37.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1059
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR c=4004
         OR b=806
  ]])
    end, {
        -- <where7-2.37.2>
        10, 11, 12, 43, "scan", 0, "sort", 0
        -- </where7-2.37.2>
    })

test:do_test(
    "where7-2.38.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=165
         OR b=201
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR a=32
  ]])
    end, {
        -- <where7-2.38.1>
        15, 32, 99, "scan", 0, "sort", 0
        -- </where7-2.38.1>
    })

test:do_test(
    "where7-2.38.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=165
         OR b=201
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR a=32
  ]])
    end, {
        -- <where7-2.38.2>
        15, 32, 99, "scan", 0, "sort", 0
        -- </where7-2.38.2>
    })

test:do_test(
    "where7-2.39.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'rstuv*')
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
  ]])
    end, {
        -- <where7-2.39.1>
        22, 48, 69, 74, 100, "scan", 0, "sort", 0
        -- </where7-2.39.1>
    })

test:do_test(
    "where7-2.39.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'rstuv*')
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
  ]])
    end, {
        -- <where7-2.39.2>
        22, 48, 69, 74, 100, "scan", 0, "sort", 0
        -- </where7-2.39.2>
    })

test:do_test(
    "where7-2.40.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=352
         OR b=278
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR b=660
         OR a=18
         OR a=34
         OR b=132
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR c=18018
  ]])
    end, {
        -- <where7-2.40.1>
        2, 12, 18, 28, 30, 32, 34, 52, 53, 54, 60, 80, 89, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.40.1>
    })

test:do_test(
    "where7-2.40.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=352
         OR b=278
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR b=660
         OR a=18
         OR a=34
         OR b=132
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR c=18018
  ]])
    end, {
        -- <where7-2.40.2>
        2, 12, 18, 28, 30, 32, 34, 52, 53, 54, 60, 80, 89, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.40.2>
    })

test:do_test(
    "where7-2.41.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR ((a BETWEEN 5 AND 7) AND a!=6)
  ]])
    end, {
        -- <where7-2.41.1>
        5, 7, 73, "scan", 0, "sort", 0
        -- </where7-2.41.1>
    })

test:do_test(
    "where7-2.41.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR ((a BETWEEN 5 AND 7) AND a!=6)
  ]])
    end, {
        -- <where7-2.41.2>
        5, 7, 73, "scan", 0, "sort", 0
        -- </where7-2.41.2>
    })

test:do_test(
    "where7-2.42.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'fghij*')
         OR b=297
         OR b=113
         OR b=176
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR a=67
         OR c=26026
  ]])
    end, {
        -- <where7-2.42.1>
        3, 14, 16, 21, 27, 29, 55, 67, 75, 76, 77, 78, 81, 83, "scan", 0, "sort", 0
        -- </where7-2.42.1>
    })

test:do_test(
    "where7-2.42.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'fghij*')
         OR b=297
         OR b=113
         OR b=176
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR a=67
         OR c=26026
  ]])
    end, {
        -- <where7-2.42.2>
        3, 14, 16, 21, 27, 29, 55, 67, 75, 76, 77, 78, 81, 83, "scan", 0, "sort", 0
        -- </where7-2.42.2>
    })

test:do_test(
    "where7-2.43.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR a=83
         OR b=44
         OR b=1023
         OR ((a BETWEEN 11 AND 13) AND a!=12)
         OR b=1023
         OR f='ijklmnopq'
  ]])
    end, {
        -- <where7-2.43.1>
        4, 6, 8, 11, 13, 34, 60, 78, 83, 86, 93, "scan", 0, "sort", 0
        -- </where7-2.43.1>
    })

test:do_test(
    "where7-2.43.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR a=83
         OR b=44
         OR b=1023
         OR ((a BETWEEN 11 AND 13) AND a!=12)
         OR b=1023
         OR f='ijklmnopq'
  ]])
    end, {
        -- <where7-2.43.2>
        4, 6, 8, 11, 13, 34, 60, 78, 83, 86, 93, "scan", 0, "sort", 0
        -- </where7-2.43.2>
    })

test:do_test(
    "where7-2.44.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR b=935
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=487
         OR b=619
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.44.1>
        17, 32, 34, 39, 42, 85, "scan", 0, "sort", 0
        -- </where7-2.44.1>
    })

test:do_test(
    "where7-2.44.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR b=935
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=487
         OR b=619
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.44.2>
        17, 32, 34, 39, 42, 85, "scan", 0, "sort", 0
        -- </where7-2.44.2>
    })

test:do_test(
    "where7-2.45.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=68.0 AND d<69.0 AND d IS NOT NULL)
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR b=938
         OR b=641
         OR c=17017
         OR a=82
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.45.1>
        37, 39, 49, 50, 51, 56, 58, 65, 68, 82, 94, "scan", 0, "sort", 0
        -- </where7-2.45.1>
    })

test:do_test(
    "where7-2.45.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=68.0 AND d<69.0 AND d IS NOT NULL)
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR b=938
         OR b=641
         OR c=17017
         OR a=82
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.45.2>
        37, 39, 49, 50, 51, 56, 58, 65, 68, 82, 94, "scan", 0, "sort", 0
        -- </where7-2.45.2>
    })

test:do_test(
    "where7-2.46.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ihgfedc' AND f GLOB 'bcdef*')
         OR c=22022
  ]])
    end, {
        -- <where7-2.46.1>
        64, 65, 66, 79, "scan", 0, "sort", 0
        -- </where7-2.46.1>
    })

test:do_test(
    "where7-2.46.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ihgfedc' AND f GLOB 'bcdef*')
         OR c=22022
  ]])
    end, {
        -- <where7-2.46.2>
        64, 65, 66, 79, "scan", 0, "sort", 0
        -- </where7-2.46.2>
    })

test:do_test(
    "where7-2.47.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=7007
         OR b=91
         OR b=212
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR c=28028
         OR (d>=83.0 AND d<84.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.47.1>
        19, 20, 21, 65, 82, 83, 84, "scan", 0, "sort", 0
        -- </where7-2.47.1>
    })

test:do_test(
    "where7-2.47.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=7007
         OR b=91
         OR b=212
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR c=28028
         OR (d>=83.0 AND d<84.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.47.2>
        19, 20, 21, 65, 82, 83, 84, "scan", 0, "sort", 0
        -- </where7-2.47.2>
    })

test:do_test(
    "where7-2.48.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR a=51
         OR ((a BETWEEN 28 AND 30) AND a!=29)
  ]])
    end, {
        -- <where7-2.48.1>
        12, 28, 30, 51, "scan", 0, "sort", 0
        -- </where7-2.48.1>
    })

test:do_test(
    "where7-2.48.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR a=51
         OR ((a BETWEEN 28 AND 30) AND a!=29)
  ]])
    end, {
        -- <where7-2.48.2>
        12, 28, 30, 51, "scan", 0, "sort", 0
        -- </where7-2.48.2>
    })

test:do_test(
    "where7-2.49.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='wvutsrq' AND f GLOB 'mnopq*')
         OR (g='wvutsrq' AND f GLOB 'jklmn*')
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR ((a BETWEEN 0 AND 2) AND a!=1)
         OR c=4004
         OR b=322
         OR c=13013
         OR a=6
  ]])
    end, {
        -- <where7-2.49.1>
        2, 6, 9, 10, 11, 12, 23, 37, 38, 39, "scan", 0, "sort", 0
        -- </where7-2.49.1>
    })

test:do_test(
    "where7-2.49.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='wvutsrq' AND f GLOB 'mnopq*')
         OR (g='wvutsrq' AND f GLOB 'jklmn*')
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR ((a BETWEEN 0 AND 2) AND a!=1)
         OR c=4004
         OR b=322
         OR c=13013
         OR a=6
  ]])
    end, {
        -- <where7-2.49.2>
        2, 6, 9, 10, 11, 12, 23, 37, 38, 39, "scan", 0, "sort", 0
        -- </where7-2.49.2>
    })

test:do_test(
    "where7-2.50.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=297
         OR b=143
         OR a=46
         OR b=660
         OR (d>=41.0 AND d<42.0 AND d IS NOT NULL)
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR b=355
         OR a=93
         OR b=297
  ]])
    end, {
        -- <where7-2.50.1>
        13, 17, 23, 27, 41, 46, 49, 60, 75, 93, "scan", 0, "sort", 0
        -- </where7-2.50.1>
    })

test:do_test(
    "where7-2.50.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=297
         OR b=143
         OR a=46
         OR b=660
         OR (d>=41.0 AND d<42.0 AND d IS NOT NULL)
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR b=355
         OR a=93
         OR b=297
  ]])
    end, {
        -- <where7-2.50.2>
        13, 17, 23, 27, 41, 46, 49, 60, 75, 93, "scan", 0, "sort", 0
        -- </where7-2.50.2>
    })

test:do_test(
    "where7-2.51.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=190
         OR a=62
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.51.1>
        62, 99, "scan", 0, "sort", 0
        -- </where7-2.51.1>
    })

test:do_test(
    "where7-2.51.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=190
         OR a=62
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.51.2>
        62, 99, "scan", 0, "sort", 0
        -- </where7-2.51.2>
    })

test:do_test(
    "where7-2.52.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1089
         OR b=102
         OR a=6
         OR b=608
  ]])
    end, {
        -- <where7-2.52.1>
        6, 99, "scan", 0, "sort", 0
        -- </where7-2.52.1>
    })

test:do_test(
    "where7-2.52.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1089
         OR b=102
         OR a=6
         OR b=608
  ]])
    end, {
        -- <where7-2.52.2>
        6, 99, "scan", 0, "sort", 0
        -- </where7-2.52.2>
    })

test:do_test(
    "where7-2.53.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=473
         OR b=1100
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR a=20
         OR b=1089
         OR b=330
         OR b=124
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.53.1>
        15, 20, 30, 43, 53, 56, 58, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.53.1>
    })

test:do_test(
    "where7-2.53.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=473
         OR b=1100
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR a=20
         OR b=1089
         OR b=330
         OR b=124
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.53.2>
        15, 20, 30, 43, 53, 56, 58, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.53.2>
    })

test:do_test(
    "where7-2.54.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 68 AND 70) AND a!=69)
         OR b=223
         OR a=12
         OR b=1048
         OR b=256
         OR a=72
         OR c>=34035
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR b=674
         OR a=22
  ]])
    end, {
        -- <where7-2.54.1>
        12, 22, 35, 68, 70, 72, "scan", 0, "sort", 0
        -- </where7-2.54.1>
    })

test:do_test(
    "where7-2.54.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 68 AND 70) AND a!=69)
         OR b=223
         OR a=12
         OR b=1048
         OR b=256
         OR a=72
         OR c>=34035
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR b=674
         OR a=22
  ]])
    end, {
        -- <where7-2.54.2>
        12, 22, 35, 68, 70, 72, "scan", 0, "sort", 0
        -- </where7-2.54.2>
    })

test:do_test(
    "where7-2.55.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 76 AND 78) AND a!=77)
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR f='yzabcdefg'
         OR c=14014
         OR a=1
         OR a=9
         OR b=960
  ]])
    end, {
        -- <where7-2.55.1>
        1, 9, 24, 40, 41, 42, 50, 76, 78, "scan", 0, "sort", 0
        -- </where7-2.55.1>
    })

test:do_test(
    "where7-2.55.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 76 AND 78) AND a!=77)
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR f='yzabcdefg'
         OR c=14014
         OR a=1
         OR a=9
         OR b=960
  ]])
    end, {
        -- <where7-2.55.2>
        1, 9, 24, 40, 41, 42, 50, 76, 78, "scan", 0, "sort", 0
        -- </where7-2.55.2>
    })

test:do_test(
    "where7-2.56.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR (g='onmlkji' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.56.1>
        19, 49, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.56.1>
    })

test:do_test(
    "where7-2.56.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR (g='onmlkji' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.56.2>
        19, 49, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.56.2>
    })

test:do_test(
    "where7-2.57.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=748
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR b=630
  ]])
    end, {
        -- <where7-2.57.1>
        9, 20, 67, 68, "scan", 0, "sort", 0
        -- </where7-2.57.1>
    })

test:do_test(
    "where7-2.57.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=748
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR b=630
  ]])
    end, {
        -- <where7-2.57.2>
        9, 20, 67, 68, "scan", 0, "sort", 0
        -- </where7-2.57.2>
    })

test:do_test(
    "where7-2.58.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=223
         OR b=267
         OR a=40
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR c<=10
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR b=528
  ]])
    end, {
        -- <where7-2.58.1>
        40, 48, 55, 57, 69, 71, "scan", 0, "sort", 0
        -- </where7-2.58.1>
    })

test:do_test(
    "where7-2.58.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=223
         OR b=267
         OR a=40
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR c<=10
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR b=528
  ]])
    end, {
        -- <where7-2.58.2>
        40, 48, 55, 57, 69, 71, "scan", 0, "sort", 0
        -- </where7-2.58.2>
    })

test:do_test(
    "where7-2.59.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='rstuvwxyz'
         OR a=41
         OR b=462
         OR a=68
         OR a=84
         OR a=69
  ]])
    end, {
        -- <where7-2.59.1>
        17, 41, 42, 43, 68, 69, 84, 95, "scan", 0, "sort", 0
        -- </where7-2.59.1>
    })

test:do_test(
    "where7-2.59.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='rstuvwxyz'
         OR a=41
         OR b=462
         OR a=68
         OR a=84
         OR a=69
  ]])
    end, {
        -- <where7-2.59.2>
        17, 41, 42, 43, 68, 69, 84, 95, "scan", 0, "sort", 0
        -- </where7-2.59.2>
    })

test:do_test(
    "where7-2.60.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=979
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.60.1>
        3, 5, 13, 89, "scan", 0, "sort", 0
        -- </where7-2.60.1>
    })

test:do_test(
    "where7-2.60.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=979
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.60.2>
        3, 5, 13, 89, "scan", 0, "sort", 0
        -- </where7-2.60.2>
    })

test:do_test(
    "where7-2.61.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR a=8
         OR a=62
         OR b=726
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
  ]])
    end, {
        -- <where7-2.61.1>
        8, 9, 10, 14, 50, 52, 59, 61, 62, 66, "scan", 0, "sort", 0
        -- </where7-2.61.1>
    })

test:do_test(
    "where7-2.61.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR a=8
         OR a=62
         OR b=726
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
  ]])
    end, {
        -- <where7-2.61.2>
        8, 9, 10, 14, 50, 52, 59, 61, 62, 66, "scan", 0, "sort", 0
        -- </where7-2.61.2>
    })

test:do_test(
    "where7-2.62.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=495
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR b=924
         OR c=11011
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR b=231
         OR b=872
         OR (g='jihgfed' AND f GLOB 'yzabc*')
  ]])
    end, {
        -- <where7-2.62.1>
        18, 20, 21, 31, 32, 33, 45, 47, 73, 76, 84, 99, "scan", 0, "sort", 0
        -- </where7-2.62.1>
    })

test:do_test(
    "where7-2.62.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=495
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR b=924
         OR c=11011
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR b=231
         OR b=872
         OR (g='jihgfed' AND f GLOB 'yzabc*')
  ]])
    end, {
        -- <where7-2.62.2>
        18, 20, 21, 31, 32, 33, 45, 47, 73, 76, 84, 99, "scan", 0, "sort", 0
        -- </where7-2.62.2>
    })

test:do_test(
    "where7-2.63.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=24
         OR b=473
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR b=509
         OR b=924
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.63.1>
        21, 24, 43, 84, 86, 96, "scan", 0, "sort", 0
        -- </where7-2.63.1>
    })

test:do_test(
    "where7-2.63.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=24
         OR b=473
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR b=509
         OR b=924
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.63.2>
        21, 24, 43, 84, 86, 96, "scan", 0, "sort", 0
        -- </where7-2.63.2>
    })

test:do_test(
    "where7-2.64.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR b=363
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.64.1>
        2, 5, 8, 23, 25, 28, 33, 34, 54, 56, 58, 60, 80, 86, 93, 100, "scan", 0, "sort", 0
        -- </where7-2.64.1>
    })

test:do_test(
    "where7-2.64.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR b=363
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.64.2>
        2, 5, 8, 23, 25, 28, 33, 34, 54, 56, 58, 60, 80, 86, 93, 100, "scan", 0, "sort", 0
        -- </where7-2.64.2>
    })

test:do_test(
    "where7-2.65.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=42
         OR e IS NULL
         OR b=495
         OR 1000000<b
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
         OR a=45
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR a=85
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.65.1>
        20, 42, 45, 46, 65, 69, 72, 85, 98, "scan", 0, "sort", 0
        -- </where7-2.65.1>
    })

test:do_test(
    "where7-2.65.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=42
         OR e IS NULL
         OR b=495
         OR 1000000<b
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
         OR a=45
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR a=85
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.65.2>
        20, 42, 45, 46, 65, 69, 72, 85, 98, "scan", 0, "sort", 0
        -- </where7-2.65.2>
    })

test:do_test(
    "where7-2.66.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=17017
         OR f='ijklmnopq'
         OR a=39
  ]])
    end, {
        -- <where7-2.66.1>
        8, 34, 39, 49, 50, 51, 60, 86, "scan", 0, "sort", 0
        -- </where7-2.66.1>
    })

test:do_test(
    "where7-2.66.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=17017
         OR f='ijklmnopq'
         OR a=39
  ]])
    end, {
        -- <where7-2.66.2>
        8, 34, 39, 49, 50, 51, 60, 86, "scan", 0, "sort", 0
        -- </where7-2.66.2>
    })

test:do_test(
    "where7-2.67.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c>=34035
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR a=91
  ]])
    end, {
        -- <where7-2.67.1>
        11, 19, 27, 37, 63, 89, 91, 96, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.67.1>
    })

test:do_test(
    "where7-2.67.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c>=34035
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR a=91
  ]])
    end, {
        -- <where7-2.67.2>
        11, 19, 27, 37, 63, 89, 91, 96, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.67.2>
    })

test:do_test(
    "where7-2.68.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='gfedcba' AND f GLOB 'nopqr*')
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR b=649
         OR b=231
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR b=58
  ]])
    end, {
        -- <where7-2.68.1>
        9, 21, 28, 29, 35, 48, 59, 61, 87, 91, "scan", 0, "sort", 0
        -- </where7-2.68.1>
    })

test:do_test(
    "where7-2.68.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='gfedcba' AND f GLOB 'nopqr*')
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR b=649
         OR b=231
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR b=58
  ]])
    end, {
        -- <where7-2.68.2>
        9, 21, 28, 29, 35, 48, 59, 61, 87, 91, "scan", 0, "sort", 0
        -- </where7-2.68.2>
    })

test:do_test(
    "where7-2.69.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=979
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.69.1>
        78, 89, "scan", 0, "sort", 0
        -- </where7-2.69.1>
    })

test:do_test(
    "where7-2.69.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=979
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.69.2>
        78, 89, "scan", 0, "sort", 0
        -- </where7-2.69.2>
    })

test:do_test(
    "where7-2.70.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=825
         OR b=1004
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.70.1>
        56, 58, 60, 62, 75, "scan", 0, "sort", 0
        -- </where7-2.70.1>
    })

test:do_test(
    "where7-2.70.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=825
         OR b=1004
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.70.2>
        56, 58, 60, 62, 75, "scan", 0, "sort", 0
        -- </where7-2.70.2>
    })

test:do_test(
    "where7-2.71.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=65
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR c=22022
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
         OR b=671
         OR (g='onmlkji' AND f GLOB 'zabcd*')
         OR a=91
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
         OR ((a BETWEEN 47 AND 49) AND a!=48)
         OR b=1004
         OR b=960
  ]])
    end, {
        -- <where7-2.71.1>
        5, 31, 47, 49, 51, 57, 61, 64, 65, 66, 83, 91, 98, "scan", 0, "sort", 0
        -- </where7-2.71.1>
    })

test:do_test(
    "where7-2.71.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=65
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR c=22022
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
         OR b=671
         OR (g='onmlkji' AND f GLOB 'zabcd*')
         OR a=91
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
         OR ((a BETWEEN 47 AND 49) AND a!=48)
         OR b=1004
         OR b=960
  ]])
    end, {
        -- <where7-2.71.2>
        5, 31, 47, 49, 51, 57, 61, 64, 65, 66, 83, 91, 98, "scan", 0, "sort", 0
        -- </where7-2.71.2>
    })

test:do_test(
    "where7-2.72.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=762
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.72.1>
        56, 58, 93, "scan", 0, "sort", 0
        -- </where7-2.72.1>
    })

test:do_test(
    "where7-2.72.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=762
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.72.2>
        56, 58, 93, "scan", 0, "sort", 0
        -- </where7-2.72.2>
    })

test:do_test(
    "where7-2.73.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR a=14
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR b=212
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.73.1>
        11, 14, 23, 54, 78, 85, "scan", 0, "sort", 0
        -- </where7-2.73.1>
    })

test:do_test(
    "where7-2.73.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR a=14
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR b=212
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.73.2>
        11, 14, 23, 54, 78, 85, "scan", 0, "sort", 0
        -- </where7-2.73.2>
    })

test:do_test(
    "where7-2.74.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ihgfedc' AND f GLOB 'bcdef*')
         OR b=168
         OR b=25
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.74.1>
        66, 79, 89, "scan", 0, "sort", 0
        -- </where7-2.74.1>
    })

test:do_test(
    "where7-2.74.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ihgfedc' AND f GLOB 'bcdef*')
         OR b=168
         OR b=25
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.74.2>
        66, 79, 89, "scan", 0, "sort", 0
        -- </where7-2.74.2>
    })

test:do_test(
    "where7-2.75.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=28028
         OR f='jklmnopqr'
         OR b=1015
  ]])
    end, {
        -- <where7-2.75.1>
        9, 35, 61, 82, 83, 84, 87, "scan", 0, "sort", 0
        -- </where7-2.75.1>
    })

test:do_test(
    "where7-2.75.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=28028
         OR f='jklmnopqr'
         OR b=1015
  ]])
    end, {
        -- <where7-2.75.2>
        9, 35, 61, 82, 83, 84, 87, "scan", 0, "sort", 0
        -- </where7-2.75.2>
    })

test:do_test(
    "where7-2.76.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=31031
         OR (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR a=49
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.76.1>
        15, 41, 49, 56, 62, 67, 87, 89, 91, 92, 93, 100, "scan", 0, "sort", 0
        -- </where7-2.76.1>
    })

test:do_test(
    "where7-2.76.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=31031
         OR (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR a=49
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.76.2>
        15, 41, 49, 56, 62, 67, 87, 89, 91, 92, 93, 100, "scan", 0, "sort", 0
        -- </where7-2.76.2>
    })

test:do_test(
    "where7-2.77.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=80
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR b=971
         OR a=60
  ]])
    end, {
        -- <where7-2.77.1>
        4, 6, 25, 29, 60, 80, "scan", 0, "sort", 0
        -- </where7-2.77.1>
    })

test:do_test(
    "where7-2.77.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=80
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR b=971
         OR a=60
  ]])
    end, {
        -- <where7-2.77.2>
        4, 6, 25, 29, 60, 80, "scan", 0, "sort", 0
        -- </where7-2.77.2>
    })

test:do_test(
    "where7-2.78.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR b=1089
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.78.1>
        30, 32, 43, 85, 89, 99, "scan", 0, "sort", 0
        -- </where7-2.78.1>
    })

test:do_test(
    "where7-2.78.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR b=1089
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.78.2>
        30, 32, 43, 85, 89, 99, "scan", 0, "sort", 0
        -- </where7-2.78.2>
    })

test:do_test(
    "where7-2.79.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=399
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR a=10
         OR b=1026
  ]])
    end, {
        -- <where7-2.79.1>
        9, 10, 11, 57, 90, "scan", 0, "sort", 0
        -- </where7-2.79.1>
    })

test:do_test(
    "where7-2.79.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=399
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR a=10
         OR b=1026
  ]])
    end, {
        -- <where7-2.79.2>
        9, 10, 11, 57, 90, "scan", 0, "sort", 0
        -- </where7-2.79.2>
    })

test:do_test(
    "where7-2.80.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='jihgfed' AND f GLOB 'yzabc*')
         OR b=465
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR (g='xwvutsr' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.80.1>
        5, 43, 65, 76, "scan", 0, "sort", 0
        -- </where7-2.80.1>
    })

test:do_test(
    "where7-2.80.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='jihgfed' AND f GLOB 'yzabc*')
         OR b=465
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR (g='xwvutsr' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.80.2>
        5, 43, 65, 76, "scan", 0, "sort", 0
        -- </where7-2.80.2>
    })

test:do_test(
    "where7-2.81.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=25
         OR b=792
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
  ]])
    end, {
        -- <where7-2.81.1>
        19, 25, 45, 71, 72, 97, "scan", 0, "sort", 0
        -- </where7-2.81.1>
    })

test:do_test(
    "where7-2.81.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=25
         OR b=792
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
  ]])
    end, {
        -- <where7-2.81.2>
        19, 25, 45, 71, 72, 97, "scan", 0, "sort", 0
        -- </where7-2.81.2>
    })

test:do_test(
    "where7-2.82.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=979
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR a=13
         OR a=15
         OR ((a BETWEEN 6 AND 8) AND a!=7)
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR a=27
         OR ((a BETWEEN 98 AND 100) AND a!=99)
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR a=32
         OR a=39
  ]])
    end, {
        -- <where7-2.82.1>
        6, 8, 13, 15, 21, 27, 32, 39, 67, 89, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.82.1>
    })

test:do_test(
    "where7-2.82.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=979
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR a=13
         OR a=15
         OR ((a BETWEEN 6 AND 8) AND a!=7)
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR a=27
         OR ((a BETWEEN 98 AND 100) AND a!=99)
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR a=32
         OR a=39
  ]])
    end, {
        -- <where7-2.82.2>
        6, 8, 13, 15, 21, 27, 32, 39, 67, 89, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.82.2>
    })

test:do_test(
    "where7-2.83.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='hijklmnop'
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR b=528
         OR c=30030
         OR (g='qponmlk' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.83.1>
        1, 7, 21, 31, 33, 42, 48, 58, 59, 77, 79, 85, 88, 89, 90, "scan", 0, "sort", 0
        -- </where7-2.83.1>
    })

test:do_test(
    "where7-2.83.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='hijklmnop'
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR b=528
         OR c=30030
         OR (g='qponmlk' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.83.2>
        1, 7, 21, 31, 33, 42, 48, 58, 59, 77, 79, 85, 88, 89, 90, "scan", 0, "sort", 0
        -- </where7-2.83.2>
    })

test:do_test(
    "where7-2.84.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=69
         OR e IS NULL
         OR b=352
         OR 1000000<b
         OR b=289
  ]])
    end, {
        -- <where7-2.84.1>
        32, "scan", 0, "sort", 0
        -- </where7-2.84.1>
    })

test:do_test(
    "where7-2.84.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=69
         OR e IS NULL
         OR b=352
         OR 1000000<b
         OR b=289
  ]])
    end, {
        -- <where7-2.84.2>
        32, "scan", 0, "sort", 0
        -- </where7-2.84.2>
    })

test:do_test(
    "where7-2.85.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='lkjihgf' AND f GLOB 'pqrst*')
         OR b=748
         OR b=696
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
  ]])
    end, {
        -- <where7-2.85.1>
        4, 30, 43, 56, 67, 68, 82, "scan", 0, "sort", 0
        -- </where7-2.85.1>
    })

test:do_test(
    "where7-2.85.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='lkjihgf' AND f GLOB 'pqrst*')
         OR b=748
         OR b=696
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
  ]])
    end, {
        -- <where7-2.85.2>
        4, 30, 43, 56, 67, 68, 82, "scan", 0, "sort", 0
        -- </where7-2.85.2>
    })

test:do_test(
    "where7-2.86.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR a=87
         OR a=80
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR b=784
         OR a=49
         OR ((a BETWEEN 34 AND 36) AND a!=35)
  ]])
    end, {
        -- <where7-2.86.1>
        34, 36, 40, 49, 68, 71, 73, 80, 87, "scan", 0, "sort", 0
        -- </where7-2.86.1>
    })

test:do_test(
    "where7-2.86.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR a=87
         OR a=80
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR b=784
         OR a=49
         OR ((a BETWEEN 34 AND 36) AND a!=35)
  ]])
    end, {
        -- <where7-2.86.2>
        34, 36, 40, 49, 68, 71, 73, 80, 87, "scan", 0, "sort", 0
        -- </where7-2.86.2>
    })

test:do_test(
    "where7-2.87.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 14 AND 16) AND a!=15)
         OR (g='wvutsrq' AND f GLOB 'jklmn*')
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR c=1001
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR c=33033
  ]])
    end, {
        -- <where7-2.87.1>
        1, 2, 3, 8, 9, 14, 16, 78, 85, 86, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.87.1>
    })

test:do_test(
    "where7-2.87.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 14 AND 16) AND a!=15)
         OR (g='wvutsrq' AND f GLOB 'jklmn*')
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR c=1001
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR c=33033
  ]])
    end, {
        -- <where7-2.87.2>
        1, 2, 3, 8, 9, 14, 16, 78, 85, 86, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.87.2>
    })

test:do_test(
    "where7-2.88.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=311
         OR b=1103
         OR b=88
  ]])
    end, {
        -- <where7-2.88.1>
        8, "scan", 0, "sort", 0
        -- </where7-2.88.1>
    })

test:do_test(
    "where7-2.88.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=311
         OR b=1103
         OR b=88
  ]])
    end, {
        -- <where7-2.88.2>
        8, "scan", 0, "sort", 0
        -- </where7-2.88.2>
    })

test:do_test(
    "where7-2.89.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 65 AND 67) AND a!=66)
         OR ((a BETWEEN 26 AND 28) AND a!=27)
         OR c=5005
         OR b=1045
         OR c=8008
         OR f='bcdefghij'
  ]])
    end, {
        -- <where7-2.89.1>
        1, 13, 14, 15, 22, 23, 24, 26, 27, 28, 53, 65, 67, 79, 95, "scan", 0, "sort", 0
        -- </where7-2.89.1>
    })

test:do_test(
    "where7-2.89.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 65 AND 67) AND a!=66)
         OR ((a BETWEEN 26 AND 28) AND a!=27)
         OR c=5005
         OR b=1045
         OR c=8008
         OR f='bcdefghij'
  ]])
    end, {
        -- <where7-2.89.2>
        1, 13, 14, 15, 22, 23, 24, 26, 27, 28, 53, 65, 67, 79, 95, "scan", 0, "sort", 0
        -- </where7-2.89.2>
    })

test:do_test(
    "where7-2.90.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=66
         OR b=553
         OR a=64
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR a=62
         OR b=1081
         OR b=770
         OR b=762
         OR b=803
         OR (g='srqponm' AND f GLOB 'efghi*')
  ]])
    end, {
        -- <where7-2.90.1>
        6, 17, 30, 62, 64, 70, 73, 93, "scan", 0, "sort", 0
        -- </where7-2.90.1>
    })

test:do_test(
    "where7-2.90.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=66
         OR b=553
         OR a=64
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR a=62
         OR b=1081
         OR b=770
         OR b=762
         OR b=803
         OR (g='srqponm' AND f GLOB 'efghi*')
  ]])
    end, {
        -- <where7-2.90.2>
        6, 17, 30, 62, 64, 70, 73, 93, "scan", 0, "sort", 0
        -- </where7-2.90.2>
    })

test:do_test(
    "where7-2.91.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='wvutsrq' AND f GLOB 'klmno*')
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR c=17017
         OR b=168
         OR ((a BETWEEN 77 AND 79) AND a!=78)
  ]])
    end, {
        -- <where7-2.91.1>
        10, 19, 45, 49, 50, 51, 71, 77, 79, 97, "scan", 0, "sort", 0
        -- </where7-2.91.1>
    })

test:do_test(
    "where7-2.91.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='wvutsrq' AND f GLOB 'klmno*')
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR c=17017
         OR b=168
         OR ((a BETWEEN 77 AND 79) AND a!=78)
  ]])
    end, {
        -- <where7-2.91.2>
        10, 19, 45, 49, 50, 51, 71, 77, 79, 97, "scan", 0, "sort", 0
        -- </where7-2.91.2>
    })

test:do_test(
    "where7-2.92.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=34034
         OR (d>=68.0 AND d<69.0 AND d IS NOT NULL)
         OR a=44
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR c=31031
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR b=619
         OR (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR ((a BETWEEN 29 AND 31) AND a!=30)
  ]])
    end, {
        -- <where7-2.92.1>
        3, 12, 23, 29, 31, 44, 55, 68, 78, 81, 91, 92, 93, 100, "scan", 0, "sort", 0
        -- </where7-2.92.1>
    })

test:do_test(
    "where7-2.92.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=34034
         OR (d>=68.0 AND d<69.0 AND d IS NOT NULL)
         OR a=44
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR c=31031
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR b=619
         OR (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR ((a BETWEEN 29 AND 31) AND a!=30)
  ]])
    end, {
        -- <where7-2.92.2>
        3, 12, 23, 29, 31, 44, 55, 68, 78, 81, 91, 92, 93, 100, "scan", 0, "sort", 0
        -- </where7-2.92.2>
    })

test:do_test(
    "where7-2.93.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=48
         OR c=15015
         OR ((a BETWEEN 65 AND 67) AND a!=66)
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR b=110
         OR f='klmnopqrs'
         OR (g='fedcbaz' AND f GLOB 'qrstu*')
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR b=674
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
  ]])
    end, {
        -- <where7-2.93.1>
        10, 36, 43, 44, 45, 48, 52, 62, 65, 67, 88, 94, 96, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.93.1>
    })

test:do_test(
    "where7-2.93.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=48
         OR c=15015
         OR ((a BETWEEN 65 AND 67) AND a!=66)
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR b=110
         OR f='klmnopqrs'
         OR (g='fedcbaz' AND f GLOB 'qrstu*')
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR b=674
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
  ]])
    end, {
        -- <where7-2.93.2>
        10, 36, 43, 44, 45, 48, 52, 62, 65, 67, 88, 94, 96, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.93.2>
    })

test:do_test(
    "where7-2.94.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=72
         OR b=913
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR b=121
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.94.1>
        2, 11, 28, 72, 83, "scan", 0, "sort", 0
        -- </where7-2.94.1>
    })

test:do_test(
    "where7-2.94.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=72
         OR b=913
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR b=121
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.94.2>
        2, 11, 28, 72, 83, "scan", 0, "sort", 0
        -- </where7-2.94.2>
    })

test:do_test(
    "where7-2.95.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=18
         OR b=286
         OR b=1015
         OR a=49
         OR b=264
  ]])
    end, {
        -- <where7-2.95.1>
        18, 24, 26, 49, "scan", 0, "sort", 0
        -- </where7-2.95.1>
    })

test:do_test(
    "where7-2.95.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=18
         OR b=286
         OR b=1015
         OR a=49
         OR b=264
  ]])
    end, {
        -- <where7-2.95.2>
        18, 24, 26, 49, "scan", 0, "sort", 0
        -- </where7-2.95.2>
    })

test:do_test(
    "where7-2.96.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=69
         OR a=11
         OR c=1001
         OR ((a BETWEEN 54 AND 56) AND a!=55)
         OR a=57
         OR ((a BETWEEN 48 AND 50) AND a!=49)
  ]])
    end, {
        -- <where7-2.96.1>
        1, 2, 3, 11, 48, 50, 54, 56, 57, "scan", 0, "sort", 0
        -- </where7-2.96.1>
    })

test:do_test(
    "where7-2.96.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=69
         OR a=11
         OR c=1001
         OR ((a BETWEEN 54 AND 56) AND a!=55)
         OR a=57
         OR ((a BETWEEN 48 AND 50) AND a!=49)
  ]])
    end, {
        -- <where7-2.96.2>
        1, 2, 3, 11, 48, 50, 54, 56, 57, "scan", 0, "sort", 0
        -- </where7-2.96.2>
    })

test:do_test(
    "where7-2.97.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=231
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
  ]])
    end, {
        -- <where7-2.97.1>
        21, 84, "scan", 0, "sort", 0
        -- </where7-2.97.1>
    })

test:do_test(
    "where7-2.97.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=231
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
  ]])
    end, {
        -- <where7-2.97.2>
        21, 84, "scan", 0, "sort", 0
        -- </where7-2.97.2>
    })

test:do_test(
    "where7-2.98.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=25
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR ((a BETWEEN 81 AND 83) AND a!=82)
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR b=289
         OR ((a BETWEEN 85 AND 87) AND a!=86)
  ]])
    end, {
        -- <where7-2.98.1>
        3, 5, 17, 23, 81, 83, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.98.1>
    })

test:do_test(
    "where7-2.98.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=25
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR ((a BETWEEN 81 AND 83) AND a!=82)
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR b=289
         OR ((a BETWEEN 85 AND 87) AND a!=86)
  ]])
    end, {
        -- <where7-2.98.2>
        3, 5, 17, 23, 81, 83, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.98.2>
    })

test:do_test(
    "where7-2.99.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='defghijkl'
         OR b=465
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR c=9009
         OR b=990
         OR b=132
         OR a=35
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR a=81
         OR ((a BETWEEN 71 AND 73) AND a!=72)
  ]])
    end, {
        -- <where7-2.99.1>
        3, 12, 25, 26, 27, 29, 35, 46, 55, 71, 73, 78, 81, 90, "scan", 0, "sort", 0
        -- </where7-2.99.1>
    })

test:do_test(
    "where7-2.99.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='defghijkl'
         OR b=465
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR c=9009
         OR b=990
         OR b=132
         OR a=35
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR a=81
         OR ((a BETWEEN 71 AND 73) AND a!=72)
  ]])
    end, {
        -- <where7-2.99.2>
        3, 12, 25, 26, 27, 29, 35, 46, 55, 71, 73, 78, 81, 90, "scan", 0, "sort", 0
        -- </where7-2.99.2>
    })

test:do_test(
    "where7-2.100.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=26026
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR f='lmnopqrst'
         OR a=6
         OR ((a BETWEEN 59 AND 61) AND a!=60)
  ]])
    end, {
        -- <where7-2.100.1>
        6, 9, 11, 37, 59, 61, 63, 76, 77, 78, 89, "scan", 0, "sort", 0
        -- </where7-2.100.1>
    })

test:do_test(
    "where7-2.100.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=26026
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR f='lmnopqrst'
         OR a=6
         OR ((a BETWEEN 59 AND 61) AND a!=60)
  ]])
    end, {
        -- <where7-2.100.2>
        6, 9, 11, 37, 59, 61, 63, 76, 77, 78, 89, "scan", 0, "sort", 0
        -- </where7-2.100.2>
    })

test:do_test(
    "where7-2.101.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 98 AND 100) AND a!=99)
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.101.1>
        7, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.101.1>
    })

test:do_test(
    "where7-2.101.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 98 AND 100) AND a!=99)
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.101.2>
        7, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.101.2>
    })

test:do_test(
    "where7-2.102.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=11011
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR b=630
         OR c=19019
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR a=24
         OR (d>=95.0 AND d<96.0 AND d IS NOT NULL)
         OR ((a BETWEEN 51 AND 53) AND a!=52)
  ]])
    end, {
        -- <where7-2.102.1>
        24, 31, 32, 33, 51, 53, 55, 56, 57, 89, 95, "scan", 0, "sort", 0
        -- </where7-2.102.1>
    })

test:do_test(
    "where7-2.102.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=11011
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR b=630
         OR c=19019
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR a=24
         OR (d>=95.0 AND d<96.0 AND d IS NOT NULL)
         OR ((a BETWEEN 51 AND 53) AND a!=52)
  ]])
    end, {
        -- <where7-2.102.2>
        24, 31, 32, 33, 51, 53, 55, 56, 57, 89, 95, "scan", 0, "sort", 0
        -- </where7-2.102.2>
    })

test:do_test(
    "where7-2.103.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=993
         OR a=81
         OR b=366
         OR b=69
  ]])
    end, {
        -- <where7-2.103.1>
        81, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.103.1>
    })

test:do_test(
    "where7-2.103.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=993
         OR a=81
         OR b=366
         OR b=69
  ]])
    end, {
        -- <where7-2.103.2>
        81, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.103.2>
    })

test:do_test(
    "where7-2.104.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='stuvwxyza'
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR ((a BETWEEN 1 AND 3) AND a!=2)
         OR b=1037
         OR f='zabcdefgh'
         OR (g='gfedcba' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.104.1>
        1, 3, 18, 24, 25, 44, 50, 51, 70, 76, 77, 90, 96, "scan", 0, "sort", 0
        -- </where7-2.104.1>
    })

test:do_test(
    "where7-2.104.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='stuvwxyza'
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR ((a BETWEEN 1 AND 3) AND a!=2)
         OR b=1037
         OR f='zabcdefgh'
         OR (g='gfedcba' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.104.2>
        1, 3, 18, 24, 25, 44, 50, 51, 70, 76, 77, 90, 96, "scan", 0, "sort", 0
        -- </where7-2.104.2>
    })

test:do_test(
    "where7-2.105.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'ghijk*')
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR ((a BETWEEN 30 AND 32) AND a!=31)
  ]])
    end, {
        -- <where7-2.105.1>
        4, 6, 30, 32, "scan", 0, "sort", 0
        -- </where7-2.105.1>
    })

test:do_test(
    "where7-2.105.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'ghijk*')
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR ((a BETWEEN 30 AND 32) AND a!=31)
  ]])
    end, {
        -- <where7-2.105.2>
        4, 6, 30, 32, "scan", 0, "sort", 0
        -- </where7-2.105.2>
    })

test:do_test(
    "where7-2.106.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=847
         OR b=190
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR ((a BETWEEN 70 AND 72) AND a!=71)
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR b=704
  ]])
    end, {
        -- <where7-2.106.1>
        9, 23, 35, 38, 40, 61, 64, 70, 72, 77, 87, "scan", 0, "sort", 0
        -- </where7-2.106.1>
    })

test:do_test(
    "where7-2.106.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=847
         OR b=190
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR ((a BETWEEN 70 AND 72) AND a!=71)
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR b=704
  ]])
    end, {
        -- <where7-2.106.2>
        9, 23, 35, 38, 40, 61, 64, 70, 72, 77, 87, "scan", 0, "sort", 0
        -- </where7-2.106.2>
    })

test:do_test(
    "where7-2.107.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=88
         OR f='vwxyzabcd'
         OR f='fghijklmn'
         OR (g='gfedcba' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.107.1>
        5, 8, 21, 31, 47, 57, 73, 83, 89, 99, "scan", 0, "sort", 0
        -- </where7-2.107.1>
    })

test:do_test(
    "where7-2.107.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=88
         OR f='vwxyzabcd'
         OR f='fghijklmn'
         OR (g='gfedcba' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.107.2>
        5, 8, 21, 31, 47, 57, 73, 83, 89, 99, "scan", 0, "sort", 0
        -- </where7-2.107.2>
    })

test:do_test(
    "where7-2.108.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=498
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR a=1
  ]])
    end, {
        -- <where7-2.108.1>
        1, 69, "scan", 0, "sort", 0
        -- </where7-2.108.1>
    })

test:do_test(
    "where7-2.108.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=498
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR a=1
  ]])
    end, {
        -- <where7-2.108.2>
        1, 69, "scan", 0, "sort", 0
        -- </where7-2.108.2>
    })

test:do_test(
    "where7-2.109.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 47 AND 49) AND a!=48)
         OR a=5
         OR b=179
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR a=69
  ]])
    end, {
        -- <where7-2.109.1>
        5, 17, 43, 47, 49, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.109.1>
    })

test:do_test(
    "where7-2.109.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 47 AND 49) AND a!=48)
         OR a=5
         OR b=179
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR a=69
  ]])
    end, {
        -- <where7-2.109.2>
        5, 17, 43, 47, 49, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.109.2>
    })

test:do_test(
    "where7-2.110.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=971
         OR (g='xwvutsr' AND f GLOB 'hijkl*')
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR b=828
         OR a=81
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR b=627
         OR b=355
         OR b=377
         OR a=44
  ]])
    end, {
        -- <where7-2.110.1>
        1, 7, 23, 25, 44, 57, 81, "scan", 0, "sort", 0
        -- </where7-2.110.1>
    })

test:do_test(
    "where7-2.110.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=971
         OR (g='xwvutsr' AND f GLOB 'hijkl*')
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR b=828
         OR a=81
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR b=627
         OR b=355
         OR b=377
         OR a=44
  ]])
    end, {
        -- <where7-2.110.2>
        1, 7, 23, 25, 44, 57, 81, "scan", 0, "sort", 0
        -- </where7-2.110.2>
    })

test:do_test(
    "where7-2.111.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=850
         OR ((a BETWEEN 6 AND 8) AND a!=7)
  ]])
    end, {
        -- <where7-2.111.1>
        6, 8, "scan", 0, "sort", 0
        -- </where7-2.111.1>
    })

test:do_test(
    "where7-2.111.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=850
         OR ((a BETWEEN 6 AND 8) AND a!=7)
  ]])
    end, {
        -- <where7-2.111.2>
        6, 8, "scan", 0, "sort", 0
        -- </where7-2.111.2>
    })

test:do_test(
    "where7-2.112.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='lkjihgf' AND f GLOB 'opqrs*')
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
  ]])
    end, {
        -- <where7-2.112.1>
        17, 43, 66, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.112.1>
    })

test:do_test(
    "where7-2.112.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='lkjihgf' AND f GLOB 'opqrs*')
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
  ]])
    end, {
        -- <where7-2.112.2>
        17, 43, 66, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.112.2>
    })

test:do_test(
    "where7-2.113.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=726
         OR b=740
         OR a=33
         OR c=8008
         OR f='rstuvwxyz'
         OR b=168
  ]])
    end, {
        -- <where7-2.113.1>
        17, 22, 23, 24, 33, 43, 66, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.113.1>
    })

test:do_test(
    "where7-2.113.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=726
         OR b=740
         OR a=33
         OR c=8008
         OR f='rstuvwxyz'
         OR b=168
  ]])
    end, {
        -- <where7-2.113.2>
        17, 22, 23, 24, 33, 43, 66, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.113.2>
    })

test:do_test(
    "where7-2.114.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='vutsrqp' AND f GLOB 'rstuv*')
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR b=396
  ]])
    end, {
        -- <where7-2.114.1>
        17, 19, 36, "scan", 0, "sort", 0
        -- </where7-2.114.1>
    })

test:do_test(
    "where7-2.114.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='vutsrqp' AND f GLOB 'rstuv*')
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR b=396
  ]])
    end, {
        -- <where7-2.114.2>
        17, 19, 36, "scan", 0, "sort", 0
        -- </where7-2.114.2>
    })

test:do_test(
    "where7-2.115.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=77
         OR ((a BETWEEN 48 AND 50) AND a!=49)
         OR c<=10
         OR ((a BETWEEN 5 AND 7) AND a!=6)
  ]])
    end, {
        -- <where7-2.115.1>
        5, 7, 48, 50, 77, "scan", 0, "sort", 0
        -- </where7-2.115.1>
    })

test:do_test(
    "where7-2.115.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=77
         OR ((a BETWEEN 48 AND 50) AND a!=49)
         OR c<=10
         OR ((a BETWEEN 5 AND 7) AND a!=6)
  ]])
    end, {
        -- <where7-2.115.2>
        5, 7, 48, 50, 77, "scan", 0, "sort", 0
        -- </where7-2.115.2>
    })

test:do_test(
    "where7-2.116.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 12 AND 14) AND a!=13)
         OR ((a BETWEEN 13 AND 15) AND a!=14)
         OR b=253
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=396
         OR b=630
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR c=3003
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.116.1>
        1, 7, 8, 9, 12, 13, 14, 15, 20, 22, 23, 27, 36, 49, 53, 79, "scan", 0, "sort", 0
        -- </where7-2.116.1>
    })

test:do_test(
    "where7-2.116.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 12 AND 14) AND a!=13)
         OR ((a BETWEEN 13 AND 15) AND a!=14)
         OR b=253
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=396
         OR b=630
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR c=3003
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.116.2>
        1, 7, 8, 9, 12, 13, 14, 15, 20, 22, 23, 27, 36, 49, 53, 79, "scan", 0, "sort", 0
        -- </where7-2.116.2>
    })

test:do_test(
    "where7-2.117.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=957
         OR b=242
         OR b=113
         OR b=957
         OR b=311
         OR b=143
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.117.1>
        9, 10, 13, 22, 35, 48, 61, 87, "scan", 0, "sort", 0
        -- </where7-2.117.1>
    })

test:do_test(
    "where7-2.117.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=957
         OR b=242
         OR b=113
         OR b=957
         OR b=311
         OR b=143
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.117.2>
        9, 10, 13, 22, 35, 48, 61, 87, "scan", 0, "sort", 0
        -- </where7-2.117.2>
    })

test:do_test(
    "where7-2.118.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 74 AND 76) AND a!=75)
         OR ((a BETWEEN 94 AND 96) AND a!=95)
         OR b=451
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.118.1>
        41, 66, 74, 76, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.118.1>
    })

test:do_test(
    "where7-2.118.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 74 AND 76) AND a!=75)
         OR ((a BETWEEN 94 AND 96) AND a!=95)
         OR b=451
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.118.2>
        41, 66, 74, 76, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.118.2>
    })

test:do_test(
    "where7-2.119.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR b=451
         OR b=363
         OR b=330
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR ((a BETWEEN 52 AND 54) AND a!=53)
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR ((a BETWEEN 81 AND 83) AND a!=82)
         OR (g='gfedcba' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.119.1>
        3, 30, 33, 41, 52, 54, 81, 83, 89, "scan", 0, "sort", 0
        -- </where7-2.119.1>
    })

test:do_test(
    "where7-2.119.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR b=451
         OR b=363
         OR b=330
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR ((a BETWEEN 52 AND 54) AND a!=53)
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR ((a BETWEEN 81 AND 83) AND a!=82)
         OR (g='gfedcba' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.119.2>
        3, 30, 33, 41, 52, 54, 81, 83, 89, "scan", 0, "sort", 0
        -- </where7-2.119.2>
    })

test:do_test(
    "where7-2.120.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='fedcbaz' AND f GLOB 'rstuv*')
         OR (d>=68.0 AND d<69.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR e IS NULL
         OR b=759
  ]])
    end, {
        -- <where7-2.120.1>
        15, 68, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.120.1>
    })

test:do_test(
    "where7-2.120.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='fedcbaz' AND f GLOB 'rstuv*')
         OR (d>=68.0 AND d<69.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR e IS NULL
         OR b=759
  ]])
    end, {
        -- <where7-2.120.2>
        15, 68, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.120.2>
    })

test:do_test(
    "where7-2.121.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.121.1>
        19, 21, 45, 71, 74, 97, "scan", 0, "sort", 0
        -- </where7-2.121.1>
    })

test:do_test(
    "where7-2.121.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.121.2>
        19, 21, 45, 71, 74, 97, "scan", 0, "sort", 0
        -- </where7-2.121.2>
    })

test:do_test(
    "where7-2.122.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1037
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR ((a BETWEEN 75 AND 77) AND a!=76)
  ]])
    end, {
        -- <where7-2.122.1>
        27, 43, 45, 47, 75, 77, 82, "scan", 0, "sort", 0
        -- </where7-2.122.1>
    })

test:do_test(
    "where7-2.122.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1037
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR ((a BETWEEN 75 AND 77) AND a!=76)
  ]])
    end, {
        -- <where7-2.122.2>
        27, 43, 45, 47, 75, 77, 82, "scan", 0, "sort", 0
        -- </where7-2.122.2>
    })

test:do_test(
    "where7-2.123.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1045
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR c=12012
  ]])
    end, {
        -- <where7-2.123.1>
        34, 35, 36, 37, 38, 39, 95, "scan", 0, "sort", 0
        -- </where7-2.123.1>
    })

test:do_test(
    "where7-2.123.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1045
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR c=12012
  ]])
    end, {
        -- <where7-2.123.2>
        34, 35, 36, 37, 38, 39, 95, "scan", 0, "sort", 0
        -- </where7-2.123.2>
    })

test:do_test(
    "where7-2.124.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='fedcbaz' AND f GLOB 'tuvwx*')
         OR b=421
         OR b=429
         OR b=498
         OR b=33
         OR b=198
         OR c=14014
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
  ]])
    end, {
        -- <where7-2.124.1>
        3, 18, 23, 39, 40, 41, 42, 49, 75, 97, "scan", 0, "sort", 0
        -- </where7-2.124.1>
    })

test:do_test(
    "where7-2.124.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='fedcbaz' AND f GLOB 'tuvwx*')
         OR b=421
         OR b=429
         OR b=498
         OR b=33
         OR b=198
         OR c=14014
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
  ]])
    end, {
        -- <where7-2.124.2>
        3, 18, 23, 39, 40, 41, 42, 49, 75, 97, "scan", 0, "sort", 0
        -- </where7-2.124.2>
    })

test:do_test(
    "where7-2.125.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=47
         OR c=31031
         OR a=38
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR b=242
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR b=352
         OR a=49
         OR (g='nmlkjih' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.125.1>
        8, 22, 31, 32, 34, 38, 49, 57, 60, 70, 86, 91, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.125.1>
    })

test:do_test(
    "where7-2.125.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=47
         OR c=31031
         OR a=38
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR b=242
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR b=352
         OR a=49
         OR (g='nmlkjih' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.125.2>
        8, 22, 31, 32, 34, 38, 49, 57, 60, 70, 86, 91, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.125.2>
    })

test:do_test(
    "where7-2.126.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR b=704
         OR a=7
         OR a=8
         OR a=46
         OR b=740
         OR b=993
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.126.1>
        7, 8, 38, 46, 64, 87, "scan", 0, "sort", 0
        -- </where7-2.126.1>
    })

test:do_test(
    "where7-2.126.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR b=704
         OR a=7
         OR a=8
         OR a=46
         OR b=740
         OR b=993
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.126.2>
        7, 8, 38, 46, 64, 87, "scan", 0, "sort", 0
        -- </where7-2.126.2>
    })

test:do_test(
    "where7-2.127.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 62 AND 64) AND a!=63)
         OR c=32032
         OR a=76
  ]])
    end, {
        -- <where7-2.127.1>
        62, 64, 76, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.127.1>
    })

test:do_test(
    "where7-2.127.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 62 AND 64) AND a!=63)
         OR c=32032
         OR a=76
  ]])
    end, {
        -- <where7-2.127.2>
        62, 64, 76, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.127.2>
    })

test:do_test(
    "where7-2.128.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR b=528
         OR (g='gfedcba' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.128.1>
        19, 48, 91, "scan", 0, "sort", 0
        -- </where7-2.128.1>
    })

test:do_test(
    "where7-2.128.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR b=528
         OR (g='gfedcba' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.128.2>
        19, 48, 91, "scan", 0, "sort", 0
        -- </where7-2.128.2>
    })

test:do_test(
    "where7-2.129.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR a=65
  ]])
    end, {
        -- <where7-2.129.1>
        26, 65, 97, "scan", 0, "sort", 0
        -- </where7-2.129.1>
    })

test:do_test(
    "where7-2.129.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR a=65
  ]])
    end, {
        -- <where7-2.129.2>
        26, 65, 97, "scan", 0, "sort", 0
        -- </where7-2.129.2>
    })

test:do_test(
    "where7-2.130.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR 1000000<b
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR a=24
  ]])
    end, {
        -- <where7-2.130.1>
        2, 24, 50, 71, "scan", 0, "sort", 0
        -- </where7-2.130.1>
    })

test:do_test(
    "where7-2.130.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR 1000000<b
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR a=24
  ]])
    end, {
        -- <where7-2.130.2>
        2, 24, 50, 71, "scan", 0, "sort", 0
        -- </where7-2.130.2>
    })

test:do_test(
    "where7-2.131.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=60
         OR a=39
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR b=36
         OR b=814
         OR a=14
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR b=440
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR (f GLOB '?abcd*' AND f GLOB 'zabc*')
  ]])
    end, {
        -- <where7-2.131.1>
        5, 14, 25, 39, 40, 51, 60, 61, 74, 77, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.131.1>
    })

test:do_test(
    "where7-2.131.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=60
         OR a=39
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR b=36
         OR b=814
         OR a=14
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR b=440
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR (f GLOB '?abcd*' AND f GLOB 'zabc*')
  ]])
    end, {
        -- <where7-2.131.2>
        5, 14, 25, 39, 40, 51, 60, 61, 74, 77, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.131.2>
    })

test:do_test(
    "where7-2.132.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f IS NULL
         OR ((a BETWEEN 39 AND 41) AND a!=40)
  ]])
    end, {
        -- <where7-2.132.1>
        39, 41, "scan", 0, "sort", 0
        -- </where7-2.132.1>
    })

test:do_test(
    "where7-2.132.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f IS NULL
         OR ((a BETWEEN 39 AND 41) AND a!=40)
  ]])
    end, {
        -- <where7-2.132.2>
        39, 41, "scan", 0, "sort", 0
        -- </where7-2.132.2>
    })

test:do_test(
    "where7-2.133.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=44
         OR ((a BETWEEN 17 AND 19) AND a!=18)
  ]])
    end, {
        -- <where7-2.133.1>
        4, 17, 19, "scan", 0, "sort", 0
        -- </where7-2.133.1>
    })

test:do_test(
    "where7-2.133.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=44
         OR ((a BETWEEN 17 AND 19) AND a!=18)
  ]])
    end, {
        -- <where7-2.133.2>
        4, 17, 19, "scan", 0, "sort", 0
        -- </where7-2.133.2>
    })

test:do_test(
    "where7-2.134.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=80.0 AND d<81.0 AND d IS NOT NULL)
         OR a=82
  ]])
    end, {
        -- <where7-2.134.1>
        80, 82, "scan", 0, "sort", 0
        -- </where7-2.134.1>
    })

test:do_test(
    "where7-2.134.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=80.0 AND d<81.0 AND d IS NOT NULL)
         OR a=82
  ]])
    end, {
        -- <where7-2.134.2>
        80, 82, "scan", 0, "sort", 0
        -- </where7-2.134.2>
    })

test:do_test(
    "where7-2.135.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 84 AND 86) AND a!=85)
         OR c=24024
         OR b=946
         OR a=19
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.135.1>
        19, 47, 70, 71, 72, 84, 86, "scan", 0, "sort", 0
        -- </where7-2.135.1>
    })

test:do_test(
    "where7-2.135.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 84 AND 86) AND a!=85)
         OR c=24024
         OR b=946
         OR a=19
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.135.2>
        19, 47, 70, 71, 72, 84, 86, "scan", 0, "sort", 0
        -- </where7-2.135.2>
    })

test:do_test(
    "where7-2.136.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=27
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR b=1045
         OR a=84
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.136.1>
        16, 19, 27, 42, 45, 68, 71, 82, 84, 89, 91, 94, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.136.1>
    })

test:do_test(
    "where7-2.136.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=27
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR b=1045
         OR a=84
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.136.2>
        16, 19, 27, 42, 45, 68, 71, 82, 84, 89, 91, 94, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.136.2>
    })

test:do_test(
    "where7-2.137.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=704
         OR b=949
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR c=24024
         OR b=553
         OR a=18
         OR a=92
  ]])
    end, {
        -- <where7-2.137.1>
        18, 22, 64, 70, 71, 72, 92, "scan", 0, "sort", 0
        -- </where7-2.137.1>
    })

test:do_test(
    "where7-2.137.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=704
         OR b=949
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR c=24024
         OR b=553
         OR a=18
         OR a=92
  ]])
    end, {
        -- <where7-2.137.2>
        18, 22, 64, 70, 71, 72, 92, "scan", 0, "sort", 0
        -- </where7-2.137.2>
    })

test:do_test(
    "where7-2.138.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR b=902
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR b=25
         OR ((a BETWEEN 16 AND 18) AND a!=17)
         OR f='zabcdefgh'
         OR b=385
  ]])
    end, {
        -- <where7-2.138.1>
        1, 16, 18, 25, 27, 35, 51, 53, 61, 77, 79, 82, "scan", 0, "sort", 0
        -- </where7-2.138.1>
    })

test:do_test(
    "where7-2.138.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR b=902
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR b=25
         OR ((a BETWEEN 16 AND 18) AND a!=17)
         OR f='zabcdefgh'
         OR b=385
  ]])
    end, {
        -- <where7-2.138.2>
        1, 16, 18, 25, 27, 35, 51, 53, 61, 77, 79, 82, "scan", 0, "sort", 0
        -- </where7-2.138.2>
    })

test:do_test(
    "where7-2.139.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=22
         OR b=36
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR ((a BETWEEN 81 AND 83) AND a!=82)
  ]])
    end, {
        -- <where7-2.139.1>
        22, 31, 57, 59, 81, 83, "scan", 0, "sort", 0
        -- </where7-2.139.1>
    })

test:do_test(
    "where7-2.139.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=22
         OR b=36
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR ((a BETWEEN 81 AND 83) AND a!=82)
  ]])
    end, {
        -- <where7-2.139.2>
        22, 31, 57, 59, 81, 83, "scan", 0, "sort", 0
        -- </where7-2.139.2>
    })

test:do_test(
    "where7-2.140.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=253
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.140.1>
        23, 60, "scan", 0, "sort", 0
        -- </where7-2.140.1>
    })

test:do_test(
    "where7-2.140.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=253
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.140.2>
        23, 60, "scan", 0, "sort", 0
        -- </where7-2.140.2>
    })

test:do_test(
    "where7-2.141.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR b=641
         OR ((a BETWEEN 36 AND 38) AND a!=37)
  ]])
    end, {
        -- <where7-2.141.1>
        1, 15, 27, 36, 38, 41, 53, 67, 79, 93, "scan", 0, "sort", 0
        -- </where7-2.141.1>
    })

test:do_test(
    "where7-2.141.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR b=641
         OR ((a BETWEEN 36 AND 38) AND a!=37)
  ]])
    end, {
        -- <where7-2.141.2>
        1, 15, 27, 36, 38, 41, 53, 67, 79, 93, "scan", 0, "sort", 0
        -- </where7-2.141.2>
    })

test:do_test(
    "where7-2.142.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=30030
         OR a=18
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR b=11
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR a=52
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR a=13
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.142.1>
        1, 13, 18, 22, 40, 44, 46, 52, 65, 88, 89, 90, "scan", 0, "sort", 0
        -- </where7-2.142.1>
    })

test:do_test(
    "where7-2.142.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=30030
         OR a=18
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR b=11
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR a=52
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR a=13
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.142.2>
        1, 13, 18, 22, 40, 44, 46, 52, 65, 88, 89, 90, "scan", 0, "sort", 0
        -- </where7-2.142.2>
    })

test:do_test(
    "where7-2.143.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=23023
         OR f='efghijklm'
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR b=1045
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR f='uvwxyzabc'
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
  ]])
    end, {
        -- <where7-2.143.1>
        4, 20, 24, 30, 39, 41, 46, 50, 56, 67, 68, 69, 72, 76, 82, 95, 98, "scan", 0, "sort", 0
        -- </where7-2.143.1>
    })

test:do_test(
    "where7-2.143.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=23023
         OR f='efghijklm'
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR b=1045
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR f='uvwxyzabc'
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
  ]])
    end, {
        -- <where7-2.143.2>
        4, 20, 24, 30, 39, 41, 46, 50, 56, 67, 68, 69, 72, 76, 82, 95, 98, "scan", 0, "sort", 0
        -- </where7-2.143.2>
    })

test:do_test(
    "where7-2.144.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=476
         OR a=11
         OR a=52
         OR b=858
         OR b=264
         OR f='wxyzabcde'
         OR c=18018
         OR b=597
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.144.1>
        11, 22, 24, 48, 52, 53, 54, 69, 74, 78, 100, "scan", 0, "sort", 0
        -- </where7-2.144.1>
    })

test:do_test(
    "where7-2.144.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=476
         OR a=11
         OR a=52
         OR b=858
         OR b=264
         OR f='wxyzabcde'
         OR c=18018
         OR b=597
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.144.2>
        11, 22, 24, 48, 52, 53, 54, 69, 74, 78, 100, "scan", 0, "sort", 0
        -- </where7-2.144.2>
    })

test:do_test(
    "where7-2.145.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=91
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR b=102
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR b=784
  ]])
    end, {
        -- <where7-2.145.1>
        12, 21, 22, 36, 59, 61, 85, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.145.1>
    })

test:do_test(
    "where7-2.145.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=91
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR b=102
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR b=784
  ]])
    end, {
        -- <where7-2.145.2>
        12, 21, 22, 36, 59, 61, 85, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.145.2>
    })

test:do_test(
    "where7-2.146.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='vutsrqp' AND f GLOB 'opqrs*')
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR b=990
         OR a=52
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.146.1>
        14, 38, 52, 90, 91, "scan", 0, "sort", 0
        -- </where7-2.146.1>
    })

test:do_test(
    "where7-2.146.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='vutsrqp' AND f GLOB 'opqrs*')
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR b=990
         OR a=52
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.146.2>
        14, 38, 52, 90, 91, "scan", 0, "sort", 0
        -- </where7-2.146.2>
    })

test:do_test(
    "where7-2.147.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=22022
         OR b=960
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR a=48
         OR b=729
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR a=44
         OR b=773
  ]])
    end, {
        -- <where7-2.147.1>
        41, 43, 44, 45, 48, 64, 65, 66, "scan", 0, "sort", 0
        -- </where7-2.147.1>
    })

test:do_test(
    "where7-2.147.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=22022
         OR b=960
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR a=48
         OR b=729
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR a=44
         OR b=773
  ]])
    end, {
        -- <where7-2.147.2>
        41, 43, 44, 45, 48, 64, 65, 66, "scan", 0, "sort", 0
        -- </where7-2.147.2>
    })

test:do_test(
    "where7-2.148.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 68 AND 70) AND a!=69)
         OR b=421
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR c=22022
         OR b=825
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
  ]])
    end, {
        -- <where7-2.148.1>
        2, 6, 17, 19, 22, 24, 29, 32, 58, 64, 65, 66, 68, 70, 75, 84, 89, "scan", 0, "sort", 0
        -- </where7-2.148.1>
    })

test:do_test(
    "where7-2.148.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 68 AND 70) AND a!=69)
         OR b=421
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR c=22022
         OR b=825
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
  ]])
    end, {
        -- <where7-2.148.2>
        2, 6, 17, 19, 22, 24, 29, 32, 58, 64, 65, 66, 68, 70, 75, 84, 89, "scan", 0, "sort", 0
        -- </where7-2.148.2>
    })

test:do_test(
    "where7-2.149.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR b=484
         OR b=1026
         OR a=90
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR b=608
         OR a=32
  ]])
    end, {
        -- <where7-2.149.1>
        32, 44, 74, 90, "scan", 0, "sort", 0
        -- </where7-2.149.1>
    })

test:do_test(
    "where7-2.149.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR b=484
         OR b=1026
         OR a=90
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR b=608
         OR a=32
  ]])
    end, {
        -- <where7-2.149.2>
        32, 44, 74, 90, "scan", 0, "sort", 0
        -- </where7-2.149.2>
    })

test:do_test(
    "where7-2.150.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c<=10
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR b=154
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR b=880
         OR a=55
         OR b=773
         OR b=319
         OR (g='hgfedcb' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.150.1>
        14, 29, 55, 76, 77, 80, 83, "scan", 0, "sort", 0
        -- </where7-2.150.1>
    })

test:do_test(
    "where7-2.150.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c<=10
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR b=154
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR b=880
         OR a=55
         OR b=773
         OR b=319
         OR (g='hgfedcb' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.150.2>
        14, 29, 55, 76, 77, 80, 83, "scan", 0, "sort", 0
        -- </where7-2.150.2>
    })

test:do_test(
    "where7-2.151.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='wvutsrq' AND f GLOB 'ijklm*')
         OR f='mnopqrstu'
         OR a=62
  ]])
    end, {
        -- <where7-2.151.1>
        8, 12, 38, 62, 64, 90, "scan", 0, "sort", 0
        -- </where7-2.151.1>
    })

test:do_test(
    "where7-2.151.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='wvutsrq' AND f GLOB 'ijklm*')
         OR f='mnopqrstu'
         OR a=62
  ]])
    end, {
        -- <where7-2.151.2>
        8, 12, 38, 62, 64, 90, "scan", 0, "sort", 0
        -- </where7-2.151.2>
    })

test:do_test(
    "where7-2.152.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=33
         OR b=1045
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR c=13013
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR b=124
         OR (g='gfedcba' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.152.1>
        33, 37, 38, 39, 40, 88, 90, 95, "scan", 0, "sort", 0
        -- </where7-2.152.1>
    })

test:do_test(
    "where7-2.152.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=33
         OR b=1045
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR c=13013
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR b=124
         OR (g='gfedcba' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.152.2>
        33, 37, 38, 39, 40, 88, 90, 95, "scan", 0, "sort", 0
        -- </where7-2.152.2>
    })

test:do_test(
    "where7-2.153.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=883
         OR c=32032
         OR f='fghijklmn'
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR b=421
         OR b=803
         OR c=4004
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
  ]])
    end, {
        -- <where7-2.153.1>
        2, 5, 10, 11, 12, 28, 31, 49, 51, 54, 57, 73, 80, 83, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.153.1>
    })

test:do_test(
    "where7-2.153.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=883
         OR c=32032
         OR f='fghijklmn'
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR b=421
         OR b=803
         OR c=4004
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
  ]])
    end, {
        -- <where7-2.153.2>
        2, 5, 10, 11, 12, 28, 31, 49, 51, 54, 57, 73, 80, 83, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.153.2>
    })

test:do_test(
    "where7-2.154.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR b=99
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.154.1>
        9, 16, 42, 68, 72, 94, "scan", 0, "sort", 0
        -- </where7-2.154.1>
    })

test:do_test(
    "where7-2.154.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR b=99
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.154.2>
        9, 16, 42, 68, 72, 94, "scan", 0, "sort", 0
        -- </where7-2.154.2>
    })

test:do_test(
    "where7-2.155.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='defghijkl'
         OR b=308
  ]])
    end, {
        -- <where7-2.155.1>
        3, 28, 29, 55, 81, "scan", 0, "sort", 0
        -- </where7-2.155.1>
    })

test:do_test(
    "where7-2.155.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='defghijkl'
         OR b=308
  ]])
    end, {
        -- <where7-2.155.2>
        3, 28, 29, 55, 81, "scan", 0, "sort", 0
        -- </where7-2.155.2>
    })

test:do_test(
    "where7-2.156.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=795
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR f='jklmnopqr'
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR b=1056
  ]])
    end, {
        -- <where7-2.156.1>
        2, 9, 28, 35, 51, 54, 61, 80, 87, 96, "scan", 0, "sort", 0
        -- </where7-2.156.1>
    })

test:do_test(
    "where7-2.156.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=795
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR f='jklmnopqr'
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR b=1056
  ]])
    end, {
        -- <where7-2.156.2>
        2, 9, 28, 35, 51, 54, 61, 80, 87, 96, "scan", 0, "sort", 0
        -- </where7-2.156.2>
    })

test:do_test(
    "where7-2.157.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=47
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR b=410
         OR b=682
         OR ((a BETWEEN 98 AND 100) AND a!=99)
         OR f='hijklmnop'
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR b=168
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR a=32
         OR a=72
  ]])
    end, {
        -- <where7-2.157.1>
        7, 32, 33, 40, 47, 51, 59, 62, 72, 85, 94, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.157.1>
    })

test:do_test(
    "where7-2.157.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=47
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR b=410
         OR b=682
         OR ((a BETWEEN 98 AND 100) AND a!=99)
         OR f='hijklmnop'
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR b=168
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR a=32
         OR a=72
  ]])
    end, {
        -- <where7-2.157.2>
        7, 32, 33, 40, 47, 51, 59, 62, 72, 85, 94, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.157.2>
    })

test:do_test(
    "where7-2.158.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=616
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR a=96
  ]])
    end, {
        -- <where7-2.158.1>
        25, 27, 38, 56, 96, "scan", 0, "sort", 0
        -- </where7-2.158.1>
    })

test:do_test(
    "where7-2.158.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=616
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR a=96
  ]])
    end, {
        -- <where7-2.158.2>
        25, 27, 38, 56, 96, "scan", 0, "sort", 0
        -- </where7-2.158.2>
    })

test:do_test(
    "where7-2.159.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR b=352
  ]])
    end, {
        -- <where7-2.159.1>
        32, 66, "scan", 0, "sort", 0
        -- </where7-2.159.1>
    })

test:do_test(
    "where7-2.159.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR b=352
  ]])
    end, {
        -- <where7-2.159.2>
        32, 66, "scan", 0, "sort", 0
        -- </where7-2.159.2>
    })

test:do_test(
    "where7-2.160.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=795
         OR c=13013
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR b=597
  ]])
    end, {
        -- <where7-2.160.1>
        28, 37, 38, 39, "scan", 0, "sort", 0
        -- </where7-2.160.1>
    })

test:do_test(
    "where7-2.160.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=795
         OR c=13013
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR b=597
  ]])
    end, {
        -- <where7-2.160.2>
        28, 37, 38, 39, "scan", 0, "sort", 0
        -- </where7-2.160.2>
    })

test:do_test(
    "where7-2.161.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=23
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR b=641
         OR b=352
         OR b=179
         OR b=806
         OR b=839
         OR b=33
  ]])
    end, {
        -- <where7-2.161.1>
        3, 23, 32, 68, "scan", 0, "sort", 0
        -- </where7-2.161.1>
    })

test:do_test(
    "where7-2.161.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=23
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR b=641
         OR b=352
         OR b=179
         OR b=806
         OR b=839
         OR b=33
  ]])
    end, {
        -- <where7-2.161.2>
        3, 23, 32, 68, "scan", 0, "sort", 0
        -- </where7-2.161.2>
    })

test:do_test(
    "where7-2.162.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1070
         OR b=1078
         OR ((a BETWEEN 11 AND 13) AND a!=12)
         OR c=12012
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR b=319
         OR c=5005
         OR 1000000<b
         OR b=1037
         OR b=234
  ]])
    end, {
        -- <where7-2.162.1>
        11, 13, 14, 15, 29, 34, 35, 36, 84, 98, "scan", 0, "sort", 0
        -- </where7-2.162.1>
    })

test:do_test(
    "where7-2.162.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1070
         OR b=1078
         OR ((a BETWEEN 11 AND 13) AND a!=12)
         OR c=12012
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR b=319
         OR c=5005
         OR 1000000<b
         OR b=1037
         OR b=234
  ]])
    end, {
        -- <where7-2.162.2>
        11, 13, 14, 15, 29, 34, 35, 36, 84, 98, "scan", 0, "sort", 0
        -- </where7-2.162.2>
    })

test:do_test(
    "where7-2.163.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='cdefghijk'
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR (d>=59.0 AND d<60.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.163.1>
        2, 17, 28, 43, 54, 59, 69, 80, 81, 95, "scan", 0, "sort", 0
        -- </where7-2.163.1>
    })

test:do_test(
    "where7-2.163.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='cdefghijk'
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR (d>=59.0 AND d<60.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.163.2>
        2, 17, 28, 43, 54, 59, 69, 80, 81, 95, "scan", 0, "sort", 0
        -- </where7-2.163.2>
    })

test:do_test(
    "where7-2.164.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=65
         OR c=14014
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR a=47
         OR b=220
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.164.1>
        20, 37, 40, 41, 42, 47, 65, 88, "scan", 0, "sort", 0
        -- </where7-2.164.1>
    })

test:do_test(
    "where7-2.164.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=65
         OR c=14014
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR a=47
         OR b=220
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.164.2>
        20, 37, 40, 41, 42, 47, 65, 88, "scan", 0, "sort", 0
        -- </where7-2.164.2>
    })

test:do_test(
    "where7-2.165.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'ijklm*')
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR b=891
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR b=484
         OR a=62
         OR (g='ihgfedc' AND f GLOB 'defgh*')
  ]])
    end, {
        -- <where7-2.165.1>
        35, 44, 57, 62, 81, 86, "scan", 0, "sort", 0
        -- </where7-2.165.1>
    })

test:do_test(
    "where7-2.165.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'ijklm*')
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR b=891
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR b=484
         OR a=62
         OR (g='ihgfedc' AND f GLOB 'defgh*')
  ]])
    end, {
        -- <where7-2.165.2>
        35, 44, 57, 62, 81, 86, "scan", 0, "sort", 0
        -- </where7-2.165.2>
    })

test:do_test(
    "where7-2.166.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=363
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR a=39
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.166.1>
        2, 10, 25, 33, 39, 46, 54, 58, 60, "scan", 0, "sort", 0
        -- </where7-2.166.1>
    })

test:do_test(
    "where7-2.166.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=363
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR a=39
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.166.2>
        2, 10, 25, 33, 39, 46, 54, 58, 60, "scan", 0, "sort", 0
        -- </where7-2.166.2>
    })

test:do_test(
    "where7-2.167.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=30030
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR b=850
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
  ]])
    end, {
        -- <where7-2.167.1>
        20, 46, 52, 72, 88, 89, 90, 98, "scan", 0, "sort", 0
        -- </where7-2.167.1>
    })

test:do_test(
    "where7-2.167.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=30030
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR b=850
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
  ]])
    end, {
        -- <where7-2.167.2>
        20, 46, 52, 72, 88, 89, 90, 98, "scan", 0, "sort", 0
        -- </where7-2.167.2>
    })

test:do_test(
    "where7-2.168.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR b=80
  ]])
    end, {
        -- <where7-2.168.1>
        23, 91, "scan", 0, "sort", 0
        -- </where7-2.168.1>
    })

test:do_test(
    "where7-2.168.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR b=80
  ]])
    end, {
        -- <where7-2.168.2>
        23, 91, "scan", 0, "sort", 0
        -- </where7-2.168.2>
    })

test:do_test(
    "where7-2.169.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 59 AND 61) AND a!=60)
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR b=462
         OR a=51
         OR b=344
         OR b=333
         OR ((a BETWEEN 61 AND 63) AND a!=62)
  ]])
    end, {
        -- <where7-2.169.1>
        42, 51, 59, 61, 63, 77, "scan", 0, "sort", 0
        -- </where7-2.169.1>
    })

test:do_test(
    "where7-2.169.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 59 AND 61) AND a!=60)
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR b=462
         OR a=51
         OR b=344
         OR b=333
         OR ((a BETWEEN 61 AND 63) AND a!=62)
  ]])
    end, {
        -- <where7-2.169.2>
        42, 51, 59, 61, 63, 77, "scan", 0, "sort", 0
        -- </where7-2.169.2>
    })

test:do_test(
    "where7-2.170.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=586
         OR a=21
         OR b=638
  ]])
    end, {
        -- <where7-2.170.1>
        21, 58, "scan", 0, "sort", 0
        -- </where7-2.170.1>
    })

test:do_test(
    "where7-2.170.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=586
         OR a=21
         OR b=638
  ]])
    end, {
        -- <where7-2.170.2>
        21, 58, "scan", 0, "sort", 0
        -- </where7-2.170.2>
    })

test:do_test(
    "where7-2.171.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=179
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR ((a BETWEEN 72 AND 74) AND a!=73)
  ]])
    end, {
        -- <where7-2.171.1>
        2, 4, 13, 40, 42, 72, 74, "scan", 0, "sort", 0
        -- </where7-2.171.1>
    })

test:do_test(
    "where7-2.171.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=179
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR ((a BETWEEN 72 AND 74) AND a!=73)
  ]])
    end, {
        -- <where7-2.171.2>
        2, 4, 13, 40, 42, 72, 74, "scan", 0, "sort", 0
        -- </where7-2.171.2>
    })

test:do_test(
    "where7-2.172.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=333
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR b=407
         OR a=5
         OR b=817
         OR b=891
  ]])
    end, {
        -- <where7-2.172.1>
        5, 37, 53, 62, 81, "scan", 0, "sort", 0
        -- </where7-2.172.1>
    })

test:do_test(
    "where7-2.172.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=333
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR b=407
         OR a=5
         OR b=817
         OR b=891
  ]])
    end, {
        -- <where7-2.172.2>
        5, 37, 53, 62, 81, "scan", 0, "sort", 0
        -- </where7-2.172.2>
    })

test:do_test(
    "where7-2.173.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b<0
         OR b=352
         OR b=517
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 12 AND 14) AND a!=13)
         OR b=1012
         OR ((a BETWEEN 11 AND 13) AND a!=12)
  ]])
    end, {
        -- <where7-2.173.1>
        11, 12, 13, 14, 32, 47, 92, 97, "scan", 0, "sort", 0
        -- </where7-2.173.1>
    })

test:do_test(
    "where7-2.173.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b<0
         OR b=352
         OR b=517
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 12 AND 14) AND a!=13)
         OR b=1012
         OR ((a BETWEEN 11 AND 13) AND a!=12)
  ]])
    end, {
        -- <where7-2.173.2>
        11, 12, 13, 14, 32, 47, 92, 97, "scan", 0, "sort", 0
        -- </where7-2.173.2>
    })

test:do_test(
    "where7-2.174.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='qponmlk' AND f GLOB 'pqrst*')
         OR c<=10
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR a=32
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR d<0.0
  ]])
    end, {
        -- <where7-2.174.1>
        12, 14, 32, 41, "scan", 0, "sort", 0
        -- </where7-2.174.1>
    })

test:do_test(
    "where7-2.174.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='qponmlk' AND f GLOB 'pqrst*')
         OR c<=10
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR a=32
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR d<0.0
  ]])
    end, {
        -- <where7-2.174.2>
        12, 14, 32, 41, "scan", 0, "sort", 0
        -- </where7-2.174.2>
    })

test:do_test(
    "where7-2.175.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=1045
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR a=26
         OR (g='gfedcba' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.175.1>
        20, 22, 26, 78, 92, 95, "scan", 0, "sort", 0
        -- </where7-2.175.1>
    })

test:do_test(
    "where7-2.175.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=1045
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR a=26
         OR (g='gfedcba' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.175.2>
        20, 22, 26, 78, 92, 95, "scan", 0, "sort", 0
        -- </where7-2.175.2>
    })

test:do_test(
    "where7-2.176.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=49
         OR b=58
  ]])
    end, {
        -- <where7-2.176.1>
        49, "scan", 0, "sort", 0
        -- </where7-2.176.1>
    })

test:do_test(
    "where7-2.176.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=49
         OR b=58
  ]])
    end, {
        -- <where7-2.176.2>
        49, "scan", 0, "sort", 0
        -- </where7-2.176.2>
    })

test:do_test(
    "where7-2.177.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'xyzab*')
         OR c=32032
         OR b=289
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.177.1>
        3, 14, 16, 17, 19, 75, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.177.1>
    })

test:do_test(
    "where7-2.177.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'xyzab*')
         OR c=32032
         OR b=289
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.177.2>
        3, 14, 16, 17, 19, 75, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.177.2>
    })

test:do_test(
    "where7-2.178.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 15 AND 17) AND a!=16)
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR b=33
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
  ]])
    end, {
        -- <where7-2.178.1>
        3, 15, 17, 43, 57, 59, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.178.1>
    })

test:do_test(
    "where7-2.178.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 15 AND 17) AND a!=16)
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR b=33
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
  ]])
    end, {
        -- <where7-2.178.2>
        3, 15, 17, 43, 57, 59, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.178.2>
    })

test:do_test(
    "where7-2.179.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=828
         OR b=341
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR b=902
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=242
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR (g='qponmlk' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.179.1>
        1, 2, 16, 22, 31, 42, 64, 66, 68, 82, 91, 94, 95, "scan", 0, "sort", 0
        -- </where7-2.179.1>
    })

test:do_test(
    "where7-2.179.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=828
         OR b=341
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR b=902
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=242
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR (g='qponmlk' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.179.2>
        1, 2, 16, 22, 31, 42, 64, 66, 68, 82, 91, 94, 95, "scan", 0, "sort", 0
        -- </where7-2.179.2>
    })

test:do_test(
    "where7-2.180.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='nmlkjih' AND f GLOB 'efghi*')
         OR b=982
         OR b=781
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR d>1e10
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.180.1>
        56, 66, 68, 71, "scan", 0, "sort", 0
        -- </where7-2.180.1>
    })

test:do_test(
    "where7-2.180.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='nmlkjih' AND f GLOB 'efghi*')
         OR b=982
         OR b=781
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR d>1e10
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.180.2>
        56, 66, 68, 71, "scan", 0, "sort", 0
        -- </where7-2.180.2>
    })

test:do_test(
    "where7-2.181.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'rstuv*')
         OR a=31
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR a=76
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR b=176
  ]])
    end, {
        -- <where7-2.181.1>
        8, 16, 23, 31, 34, 57, 59, 60, 69, 74, 76, 86, "scan", 0, "sort", 0
        -- </where7-2.181.1>
    })

test:do_test(
    "where7-2.181.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'rstuv*')
         OR a=31
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR a=76
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR b=176
  ]])
    end, {
        -- <where7-2.181.2>
        8, 16, 23, 31, 34, 57, 59, 60, 69, 74, 76, 86, "scan", 0, "sort", 0
        -- </where7-2.181.2>
    })

test:do_test(
    "where7-2.182.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR b=14
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR f='zabcdefgh'
  ]])
    end, {
        -- <where7-2.182.1>
        12, 25, 47, 51, 55, 59, 60, 61, 77, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.182.1>
    })

test:do_test(
    "where7-2.182.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR b=14
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR f='zabcdefgh'
  ]])
    end, {
        -- <where7-2.182.2>
        12, 25, 47, 51, 55, 59, 60, 61, 77, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.182.2>
    })

test:do_test(
    "where7-2.183.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='tsrqpon' AND f GLOB 'zabcd*')
         OR b=286
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR b=91
         OR ((a BETWEEN 43 AND 45) AND a!=44)
  ]])
    end, {
        -- <where7-2.183.1>
        25, 26, 31, 43, 45, "scan", 0, "sort", 0
        -- </where7-2.183.1>
    })

test:do_test(
    "where7-2.183.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='tsrqpon' AND f GLOB 'zabcd*')
         OR b=286
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR b=91
         OR ((a BETWEEN 43 AND 45) AND a!=44)
  ]])
    end, {
        -- <where7-2.183.2>
        25, 26, 31, 43, 45, "scan", 0, "sort", 0
        -- </where7-2.183.2>
    })

test:do_test(
    "where7-2.184.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='lkjihgf' AND f GLOB 'nopqr*')
         OR c=19019
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
         OR b=374
  ]])
    end, {
        -- <where7-2.184.1>
        22, 34, 48, 55, 56, 57, 65, 74, 100, "scan", 0, "sort", 0
        -- </where7-2.184.1>
    })

test:do_test(
    "where7-2.184.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='lkjihgf' AND f GLOB 'nopqr*')
         OR c=19019
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
         OR b=374
  ]])
    end, {
        -- <where7-2.184.2>
        22, 34, 48, 55, 56, 57, 65, 74, 100, "scan", 0, "sort", 0
        -- </where7-2.184.2>
    })

test:do_test(
    "where7-2.185.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE g IS NULL
         OR (g='onmlkji' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.185.1>
        48, "scan", 0, "sort", 0
        -- </where7-2.185.1>
    })

test:do_test(
    "where7-2.185.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE g IS NULL
         OR (g='onmlkji' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.185.2>
        48, "scan", 0, "sort", 0
        -- </where7-2.185.2>
    })

test:do_test(
    "where7-2.186.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=4
         OR b=407
  ]])
    end, {
        -- <where7-2.186.1>
        4, 37, "scan", 0, "sort", 0
        -- </where7-2.186.1>
    })

test:do_test(
    "where7-2.186.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=4
         OR b=407
  ]])
    end, {
        -- <where7-2.186.2>
        4, 37, "scan", 0, "sort", 0
        -- </where7-2.186.2>
    })

test:do_test(
    "where7-2.187.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=564
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR b=234
         OR b=641
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR a=98
  ]])
    end, {
        -- <where7-2.187.1>
        1, 5, 12, 13, 27, 39, 53, 65, 66, 68, 79, 91, 98, "scan", 0, "sort", 0
        -- </where7-2.187.1>
    })

test:do_test(
    "where7-2.187.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=564
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR b=234
         OR b=641
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR a=98
  ]])
    end, {
        -- <where7-2.187.2>
        1, 5, 12, 13, 27, 39, 53, 65, 66, 68, 79, 91, 98, "scan", 0, "sort", 0
        -- </where7-2.187.2>
    })

test:do_test(
    "where7-2.188.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=73
         OR b=44
         OR b=539
         OR c=11011
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=69
         OR b=1001
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR ((a BETWEEN 32 AND 34) AND a!=33)
  ]])
    end, {
        -- <where7-2.188.1>
        4, 23, 26, 31, 32, 33, 34, 49, 73, 81, 91, 95, "scan", 0, "sort", 0
        -- </where7-2.188.1>
    })

test:do_test(
    "where7-2.188.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=73
         OR b=44
         OR b=539
         OR c=11011
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=69
         OR b=1001
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR ((a BETWEEN 32 AND 34) AND a!=33)
  ]])
    end, {
        -- <where7-2.188.2>
        4, 23, 26, 31, 32, 33, 34, 49, 73, 81, 91, 95, "scan", 0, "sort", 0
        -- </where7-2.188.2>
    })

test:do_test(
    "where7-2.189.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=264
         OR b=143
         OR a=48
  ]])
    end, {
        -- <where7-2.189.1>
        13, 24, 48, "scan", 0, "sort", 0
        -- </where7-2.189.1>
    })

test:do_test(
    "where7-2.189.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=264
         OR b=143
         OR a=48
  ]])
    end, {
        -- <where7-2.189.2>
        13, 24, 48, "scan", 0, "sort", 0
        -- </where7-2.189.2>
    })

test:do_test(
    "where7-2.190.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1001
         OR b=1070
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR b=14
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR a=66
  ]])
    end, {
        -- <where7-2.190.1>
        18, 56, 58, 66, 72, 74, 91, "scan", 0, "sort", 0
        -- </where7-2.190.1>
    })

test:do_test(
    "where7-2.190.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1001
         OR b=1070
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR b=14
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR a=66
  ]])
    end, {
        -- <where7-2.190.2>
        18, 56, 58, 66, 72, 74, 91, "scan", 0, "sort", 0
        -- </where7-2.190.2>
    })

test:do_test(
    "where7-2.191.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=23023
         OR (d>=83.0 AND d<84.0 AND d IS NOT NULL)
         OR a=66
         OR (g='onmlkji' AND f GLOB 'zabcd*')
         OR a=51
         OR a=23
         OR c=4004
  ]])
    end, {
        -- <where7-2.191.1>
        10, 11, 12, 23, 51, 66, 67, 68, 69, 83, "scan", 0, "sort", 0
        -- </where7-2.191.1>
    })

test:do_test(
    "where7-2.191.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=23023
         OR (d>=83.0 AND d<84.0 AND d IS NOT NULL)
         OR a=66
         OR (g='onmlkji' AND f GLOB 'zabcd*')
         OR a=51
         OR a=23
         OR c=4004
  ]])
    end, {
        -- <where7-2.191.2>
        10, 11, 12, 23, 51, 66, 67, 68, 69, 83, "scan", 0, "sort", 0
        -- </where7-2.191.2>
    })

test:do_test(
    "where7-2.192.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=36
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR a=80
  ]])
    end, {
        -- <where7-2.192.1>
        37, 80, "scan", 0, "sort", 0
        -- </where7-2.192.1>
    })

test:do_test(
    "where7-2.192.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=36
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR a=80
  ]])
    end, {
        -- <where7-2.192.2>
        37, 80, "scan", 0, "sort", 0
        -- </where7-2.192.2>
    })

test:do_test(
    "where7-2.193.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR a=55
         OR f='efghijklm'
         OR a=8
         OR a=80
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR b=256
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.193.1>
        4, 8, 30, 34, 37, 39, 55, 56, 60, 72, 80, 82, 86, "scan", 0, "sort", 0
        -- </where7-2.193.1>
    })

test:do_test(
    "where7-2.193.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR a=55
         OR f='efghijklm'
         OR a=8
         OR a=80
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR b=256
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.193.2>
        4, 8, 30, 34, 37, 39, 55, 56, 60, 72, 80, 82, 86, "scan", 0, "sort", 0
        -- </where7-2.193.2>
    })

test:do_test(
    "where7-2.194.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR b=836
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR a=91
         OR b=594
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.194.1>
        2, 8, 28, 47, 54, 76, 80, 87, 91, "scan", 0, "sort", 0
        -- </where7-2.194.1>
    })

test:do_test(
    "where7-2.194.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR b=836
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR a=91
         OR b=594
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.194.2>
        2, 8, 28, 47, 54, 76, 80, 87, 91, "scan", 0, "sort", 0
        -- </where7-2.194.2>
    })

test:do_test(
    "where7-2.195.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='tsrqpon' AND f GLOB 'yzabc*')
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 62 AND 64) AND a!=63)
         OR c=6006
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.195.1>
        12, 16, 17, 18, 24, 43, 50, 52, 62, 64, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.195.1>
    })

test:do_test(
    "where7-2.195.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='tsrqpon' AND f GLOB 'yzabc*')
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 62 AND 64) AND a!=63)
         OR c=6006
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.195.2>
        12, 16, 17, 18, 24, 43, 50, 52, 62, 64, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.195.2>
    })

test:do_test(
    "where7-2.196.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 83 AND 85) AND a!=84)
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR a=13
         OR b=121
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR ((a BETWEEN 12 AND 14) AND a!=13)
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR b=660
         OR b=792
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
  ]])
    end, {
        -- <where7-2.196.1>
        6, 11, 12, 13, 14, 16, 18, 44, 60, 72, 83, 85, "scan", 0, "sort", 0
        -- </where7-2.196.1>
    })

test:do_test(
    "where7-2.196.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 83 AND 85) AND a!=84)
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR a=13
         OR b=121
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR ((a BETWEEN 12 AND 14) AND a!=13)
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR b=660
         OR b=792
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
  ]])
    end, {
        -- <where7-2.196.2>
        6, 11, 12, 13, 14, 16, 18, 44, 60, 72, 83, 85, "scan", 0, "sort", 0
        -- </where7-2.196.2>
    })

test:do_test(
    "where7-2.197.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1089
         OR b=495
         OR b=157
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
         OR (d>=59.0 AND d<60.0 AND d IS NOT NULL)
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR (g='xwvutsr' AND f GLOB 'hijkl*')
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR f='wxyzabcde'
  ]])
    end, {
        -- <where7-2.197.1>
        1, 7, 20, 22, 45, 46, 48, 59, 72, 74, 98, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.197.1>
    })

test:do_test(
    "where7-2.197.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1089
         OR b=495
         OR b=157
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
         OR (d>=59.0 AND d<60.0 AND d IS NOT NULL)
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR (g='xwvutsr' AND f GLOB 'hijkl*')
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR f='wxyzabcde'
  ]])
    end, {
        -- <where7-2.197.2>
        1, 7, 20, 22, 45, 46, 48, 59, 72, 74, 98, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.197.2>
    })

test:do_test(
    "where7-2.198.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='bcdefghij'
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR b=157
         OR b=267
         OR c=34034
  ]])
    end, {
        -- <where7-2.198.1>
        1, 27, 32, 40, 42, 53, 79, 100, "scan", 0, "sort", 0
        -- </where7-2.198.1>
    })

test:do_test(
    "where7-2.198.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='bcdefghij'
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR b=157
         OR b=267
         OR c=34034
  ]])
    end, {
        -- <where7-2.198.2>
        1, 27, 32, 40, 42, 53, 79, 100, "scan", 0, "sort", 0
        -- </where7-2.198.2>
    })

test:do_test(
    "where7-2.199.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=19
         OR a=23
         OR c<=10
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.199.1>
        19, 23, 66, "scan", 0, "sort", 0
        -- </where7-2.199.1>
    })

test:do_test(
    "where7-2.199.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=19
         OR a=23
         OR c<=10
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.199.2>
        19, 23, 66, "scan", 0, "sort", 0
        -- </where7-2.199.2>
    })

test:do_test(
    "where7-2.200.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 1 AND 3) AND a!=2)
         OR b=792
         OR b=803
         OR b=36
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
  ]])
    end, {
        -- <where7-2.200.1>
        1, 3, 27, 53, 72, 73, 79, "scan", 0, "sort", 0
        -- </where7-2.200.1>
    })

test:do_test(
    "where7-2.200.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 1 AND 3) AND a!=2)
         OR b=792
         OR b=803
         OR b=36
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
  ]])
    end, {
        -- <where7-2.200.2>
        1, 3, 27, 53, 72, 73, 79, "scan", 0, "sort", 0
        -- </where7-2.200.2>
    })

test:do_test(
    "where7-2.201.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 71 AND 73) AND a!=72)
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR f='jklmnopqr'
         OR (g='onmlkji' AND f GLOB 'yzabc*')
         OR b=891
         OR a=40
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.201.1>
        9, 20, 35, 40, 50, 61, 67, 71, 73, 76, 78, 81, 87, "scan", 0, "sort", 0
        -- </where7-2.201.1>
    })

test:do_test(
    "where7-2.201.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 71 AND 73) AND a!=72)
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR f='jklmnopqr'
         OR (g='onmlkji' AND f GLOB 'yzabc*')
         OR b=891
         OR a=40
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.201.2>
        9, 20, 35, 40, 50, 61, 67, 71, 73, 76, 78, 81, 87, "scan", 0, "sort", 0
        -- </where7-2.201.2>
    })

test:do_test(
    "where7-2.202.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR a=32
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR a=95
         OR d>1e10
         OR b=429
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR c=10010
         OR ((a BETWEEN 83 AND 85) AND a!=84)
  ]])
    end, {
        -- <where7-2.202.1>
        15, 28, 29, 30, 32, 39, 54, 76, 83, 85, 88, 95, "scan", 0, "sort", 0
        -- </where7-2.202.1>
    })

test:do_test(
    "where7-2.202.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR a=32
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR a=95
         OR d>1e10
         OR b=429
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR c=10010
         OR ((a BETWEEN 83 AND 85) AND a!=84)
  ]])
    end, {
        -- <where7-2.202.2>
        15, 28, 29, 30, 32, 39, 54, 76, 83, 85, 88, 95, "scan", 0, "sort", 0
        -- </where7-2.202.2>
    })

test:do_test(
    "where7-2.203.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'defgh*')
         OR a=22
         OR a=26
         OR a=81
         OR a=53
         OR ((a BETWEEN 92 AND 94) AND a!=93)
         OR c=30030
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR a=82
         OR b=594
  ]])
    end, {
        -- <where7-2.203.1>
        3, 8, 22, 26, 53, 54, 81, 82, 88, 89, 90, 92, 94, "scan", 0, "sort", 0
        -- </where7-2.203.1>
    })

test:do_test(
    "where7-2.203.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'defgh*')
         OR a=22
         OR a=26
         OR a=81
         OR a=53
         OR ((a BETWEEN 92 AND 94) AND a!=93)
         OR c=30030
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR a=82
         OR b=594
  ]])
    end, {
        -- <where7-2.203.2>
        3, 8, 22, 26, 53, 54, 81, 82, 88, 89, 90, 92, 94, "scan", 0, "sort", 0
        -- </where7-2.203.2>
    })

test:do_test(
    "where7-2.204.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 34 AND 36) AND a!=35)
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR a=83
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR ((a BETWEEN 99 AND 101) AND a!=100)
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=1092
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR b=25
  ]])
    end, {
        -- <where7-2.204.1>
        12, 30, 34, 36, 57, 68, 83, 86, 99, "scan", 0, "sort", 0
        -- </where7-2.204.1>
    })

test:do_test(
    "where7-2.204.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 34 AND 36) AND a!=35)
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR a=83
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR ((a BETWEEN 99 AND 101) AND a!=100)
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=1092
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR b=25
  ]])
    end, {
        -- <where7-2.204.2>
        12, 30, 34, 36, 57, 68, 83, 86, 99, "scan", 0, "sort", 0
        -- </where7-2.204.2>
    })

test:do_test(
    "where7-2.205.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=20
         OR b=421
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR a=50
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.205.1>
        5, 20, 40, 50, 53, "scan", 0, "sort", 0
        -- </where7-2.205.1>
    })

test:do_test(
    "where7-2.205.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=20
         OR b=421
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR a=50
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.205.2>
        5, 20, 40, 50, 53, "scan", 0, "sort", 0
        -- </where7-2.205.2>
    })

test:do_test(
    "where7-2.206.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=960
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
  ]])
    end, {
        -- <where7-2.206.1>
        13, 39, 65, 91, "scan", 0, "sort", 0
        -- </where7-2.206.1>
    })

test:do_test(
    "where7-2.206.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=960
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
  ]])
    end, {
        -- <where7-2.206.2>
        13, 39, 65, 91, "scan", 0, "sort", 0
        -- </where7-2.206.2>
    })

test:do_test(
    "where7-2.207.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=891
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR f='nopqrstuv'
  ]])
    end, {
        -- <where7-2.207.1>
        13, 31, 39, 65, 81, 91, "scan", 0, "sort", 0
        -- </where7-2.207.1>
    })

test:do_test(
    "where7-2.207.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=891
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR f='nopqrstuv'
  ]])
    end, {
        -- <where7-2.207.2>
        13, 31, 39, 65, 81, 91, "scan", 0, "sort", 0
        -- </where7-2.207.2>
    })

test:do_test(
    "where7-2.208.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=157
         OR b=289
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=1001
         OR b=707
  ]])
    end, {
        -- <where7-2.208.1>
        32, 34, 91, "scan", 0, "sort", 0
        -- </where7-2.208.1>
    })

test:do_test(
    "where7-2.208.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=157
         OR b=289
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=1001
         OR b=707
  ]])
    end, {
        -- <where7-2.208.2>
        32, 34, 91, "scan", 0, "sort", 0
        -- </where7-2.208.2>
    })

test:do_test(
    "where7-2.209.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='edcbazy' AND f GLOB 'wxyza*')
         OR b=957
         OR ((a BETWEEN 48 AND 50) AND a!=49)
  ]])
    end, {
        -- <where7-2.209.1>
        48, 50, 87, 100, "scan", 0, "sort", 0
        -- </where7-2.209.1>
    })

test:do_test(
    "where7-2.209.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='edcbazy' AND f GLOB 'wxyza*')
         OR b=957
         OR ((a BETWEEN 48 AND 50) AND a!=49)
  ]])
    end, {
        -- <where7-2.209.2>
        48, 50, 87, 100, "scan", 0, "sort", 0
        -- </where7-2.209.2>
    })

test:do_test(
    "where7-2.210.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR a=77
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.210.1>
        40, 77, 85, "scan", 0, "sort", 0
        -- </where7-2.210.1>
    })

test:do_test(
    "where7-2.210.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR a=77
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.210.2>
        40, 77, 85, "scan", 0, "sort", 0
        -- </where7-2.210.2>
    })

test:do_test(
    "where7-2.211.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR b=11
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR a=99
  ]])
    end, {
        -- <where7-2.211.1>
        1, 14, 16, 38, 66, 96, 99, "scan", 0, "sort", 0
        -- </where7-2.211.1>
    })

test:do_test(
    "where7-2.211.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR b=11
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR a=99
  ]])
    end, {
        -- <where7-2.211.2>
        1, 14, 16, 38, 66, 96, 99, "scan", 0, "sort", 0
        -- </where7-2.211.2>
    })

test:do_test(
    "where7-2.212.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='fghijklmn'
         OR a=16
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR b=80
  ]])
    end, {
        -- <where7-2.212.1>
        3, 5, 9, 11, 16, 31, 52, 57, 60, 62, 71, 83, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.212.1>
    })

test:do_test(
    "where7-2.212.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='fghijklmn'
         OR a=16
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR b=80
  ]])
    end, {
        -- <where7-2.212.2>
        3, 5, 9, 11, 16, 31, 52, 57, 60, 62, 71, 83, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.212.2>
    })

test:do_test(
    "where7-2.213.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='wvutsrq' AND f GLOB 'mnopq*')
         OR a=44
         OR a=43
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
         OR b=25
  ]])
    end, {
        -- <where7-2.213.1>
        12, 43, 44, 66, "scan", 0, "sort", 0
        -- </where7-2.213.1>
    })

test:do_test(
    "where7-2.213.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='wvutsrq' AND f GLOB 'mnopq*')
         OR a=44
         OR a=43
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
         OR b=25
  ]])
    end, {
        -- <where7-2.213.2>
        12, 43, 44, 66, "scan", 0, "sort", 0
        -- </where7-2.213.2>
    })

test:do_test(
    "where7-2.214.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='vwxyzabcd'
         OR a=73
         OR b=597
  ]])
    end, {
        -- <where7-2.214.1>
        21, 47, 73, 99, "scan", 0, "sort", 0
        -- </where7-2.214.1>
    })

test:do_test(
    "where7-2.214.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='vwxyzabcd'
         OR a=73
         OR b=597
  ]])
    end, {
        -- <where7-2.214.2>
        21, 47, 73, 99, "scan", 0, "sort", 0
        -- </where7-2.214.2>
    })

test:do_test(
    "where7-2.215.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=22
         OR ((a BETWEEN 61 AND 63) AND a!=62)
         OR e IS NULL
         OR a=1
  ]])
    end, {
        -- <where7-2.215.1>
        1, 2, 61, 63, "scan", 0, "sort", 0
        -- </where7-2.215.1>
    })

test:do_test(
    "where7-2.215.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=22
         OR ((a BETWEEN 61 AND 63) AND a!=62)
         OR e IS NULL
         OR a=1
  ]])
    end, {
        -- <where7-2.215.2>
        1, 2, 61, 63, "scan", 0, "sort", 0
        -- </where7-2.215.2>
    })

test:do_test(
    "where7-2.216.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'opqrs*')
         OR b=1015
         OR c=16016
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR f='abcdefghi'
         OR b=605
         OR a=63
  ]])
    end, {
        -- <where7-2.216.1>
        3, 19, 26, 45, 46, 47, 48, 52, 55, 63, 71, 78, 92, 97, "scan", 0, "sort", 0
        -- </where7-2.216.1>
    })

test:do_test(
    "where7-2.216.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'opqrs*')
         OR b=1015
         OR c=16016
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR f='abcdefghi'
         OR b=605
         OR a=63
  ]])
    end, {
        -- <where7-2.216.2>
        3, 19, 26, 45, 46, 47, 48, 52, 55, 63, 71, 78, 92, 97, "scan", 0, "sort", 0
        -- </where7-2.216.2>
    })

test:do_test(
    "where7-2.217.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='yxwvuts' AND f GLOB 'bcdef*')
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR b=641
         OR b=795
  ]])
    end, {
        -- <where7-2.217.1>
        1, 44, "scan", 0, "sort", 0
        -- </where7-2.217.1>
    })

test:do_test(
    "where7-2.217.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='yxwvuts' AND f GLOB 'bcdef*')
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR b=641
         OR b=795
  ]])
    end, {
        -- <where7-2.217.2>
        1, 44, "scan", 0, "sort", 0
        -- </where7-2.217.2>
    })

test:do_test(
    "where7-2.218.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='fghijklmn'
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.218.1>
        5, 15, 31, 44, 57, 83, "scan", 0, "sort", 0
        -- </where7-2.218.1>
    })

test:do_test(
    "where7-2.218.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='fghijklmn'
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.218.2>
        5, 15, 31, 44, 57, 83, "scan", 0, "sort", 0
        -- </where7-2.218.2>
    })

test:do_test(
    "where7-2.219.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 99 AND 101) AND a!=100)
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR a=92
         OR b=1100
         OR ((a BETWEEN 98 AND 100) AND a!=99)
         OR ((a BETWEEN 30 AND 32) AND a!=31)
  ]])
    end, {
        -- <where7-2.219.1>
        30, 32, 72, 74, 85, 87, 92, 98, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.219.1>
    })

test:do_test(
    "where7-2.219.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 99 AND 101) AND a!=100)
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR a=92
         OR b=1100
         OR ((a BETWEEN 98 AND 100) AND a!=99)
         OR ((a BETWEEN 30 AND 32) AND a!=31)
  ]])
    end, {
        -- <where7-2.219.2>
        30, 32, 72, 74, 85, 87, 92, 98, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.219.2>
    })

test:do_test(
    "where7-2.220.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=880
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR b=1089
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR f IS NULL
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
  ]])
    end, {
        -- <where7-2.220.1>
        5, 12, 16, 31, 57, 69, 71, 80, 83, 86, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.220.1>
    })

test:do_test(
    "where7-2.220.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=880
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR b=1089
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR f IS NULL
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
  ]])
    end, {
        -- <where7-2.220.2>
        5, 12, 16, 31, 57, 69, 71, 80, 83, 86, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.220.2>
    })

test:do_test(
    "where7-2.221.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1026
         OR b=407
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR b=564
         OR c=23023
         OR b=891
         OR c=22022
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR (g='rqponml' AND f GLOB 'ijklm*')
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.221.1>
        9, 11, 21, 22, 24, 31, 34, 37, 64, 65, 66, 67, 68, 69, 81, "scan", 0, "sort", 0
        -- </where7-2.221.1>
    })

test:do_test(
    "where7-2.221.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1026
         OR b=407
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR b=564
         OR c=23023
         OR b=891
         OR c=22022
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR (g='rqponml' AND f GLOB 'ijklm*')
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.221.2>
        9, 11, 21, 22, 24, 31, 34, 37, 64, 65, 66, 67, 68, 69, 81, "scan", 0, "sort", 0
        -- </where7-2.221.2>
    })

test:do_test(
    "where7-2.222.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR a=72
         OR a=43
  ]])
    end, {
        -- <where7-2.222.1>
        43, 71, 72, 73, "scan", 0, "sort", 0
        -- </where7-2.222.1>
    })

test:do_test(
    "where7-2.222.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR a=72
         OR a=43
  ]])
    end, {
        -- <where7-2.222.2>
        43, 71, 72, 73, "scan", 0, "sort", 0
        -- </where7-2.222.2>
    })

test:do_test(
    "where7-2.223.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 67 AND 69) AND a!=68)
         OR ((a BETWEEN 79 AND 81) AND a!=80)
         OR c=18018
         OR b=792
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR f='uvwxyzabc'
         OR (d>=74.0 AND d<75.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.223.1>
        8, 20, 46, 52, 53, 54, 61, 67, 69, 72, 74, 77, 79, 81, 91, 98, "scan", 0, "sort", 0
        -- </where7-2.223.1>
    })

test:do_test(
    "where7-2.223.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 67 AND 69) AND a!=68)
         OR ((a BETWEEN 79 AND 81) AND a!=80)
         OR c=18018
         OR b=792
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR f='uvwxyzabc'
         OR (d>=74.0 AND d<75.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.223.2>
        8, 20, 46, 52, 53, 54, 61, 67, 69, 72, 74, 77, 79, 81, 91, 98, "scan", 0, "sort", 0
        -- </where7-2.223.2>
    })

test:do_test(
    "where7-2.224.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=429
         OR (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR b=1070
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.224.1>
        4, 17, 30, 33, 39, 40, 56, 82, "scan", 0, "sort", 0
        -- </where7-2.224.1>
    })

test:do_test(
    "where7-2.224.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=429
         OR (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR b=1070
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.224.2>
        4, 17, 30, 33, 39, 40, 56, 82, "scan", 0, "sort", 0
        -- </where7-2.224.2>
    })

test:do_test(
    "where7-2.225.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=572
  ]])
    end, {
        -- <where7-2.225.1>
        52, 61, "scan", 0, "sort", 0
        -- </where7-2.225.1>
    })

test:do_test(
    "where7-2.225.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=572
  ]])
    end, {
        -- <where7-2.225.2>
        52, 61, "scan", 0, "sort", 0
        -- </where7-2.225.2>
    })

test:do_test(
    "where7-2.226.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 62 AND 64) AND a!=63)
         OR f='abcdefghi'
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
  ]])
    end, {
        -- <where7-2.226.1>
        8, 26, 52, 62, 64, 78, "scan", 0, "sort", 0
        -- </where7-2.226.1>
    })

test:do_test(
    "where7-2.226.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 62 AND 64) AND a!=63)
         OR f='abcdefghi'
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
  ]])
    end, {
        -- <where7-2.226.2>
        8, 26, 52, 62, 64, 78, "scan", 0, "sort", 0
        -- </where7-2.226.2>
    })

test:do_test(
    "where7-2.227.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=40
         OR ((a BETWEEN 85 AND 87) AND a!=86)
  ]])
    end, {
        -- <where7-2.227.1>
        40, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.227.1>
    })

test:do_test(
    "where7-2.227.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=40
         OR ((a BETWEEN 85 AND 87) AND a!=86)
  ]])
    end, {
        -- <where7-2.227.2>
        40, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.227.2>
    })

test:do_test(
    "where7-2.228.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=43
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR a=1
         OR ((a BETWEEN 75 AND 77) AND a!=76)
         OR a=75
         OR (g='hgfedcb' AND f GLOB 'fghij*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
  ]])
    end, {
        -- <where7-2.228.1>
        1, 43, 44, 46, 59, 61, 75, 77, 83, "scan", 0, "sort", 0
        -- </where7-2.228.1>
    })

test:do_test(
    "where7-2.228.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=43
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR a=1
         OR ((a BETWEEN 75 AND 77) AND a!=76)
         OR a=75
         OR (g='hgfedcb' AND f GLOB 'fghij*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
  ]])
    end, {
        -- <where7-2.228.2>
        1, 43, 44, 46, 59, 61, 75, 77, 83, "scan", 0, "sort", 0
        -- </where7-2.228.2>
    })

test:do_test(
    "where7-2.229.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='gfedcba' AND f GLOB 'nopqr*')
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR b=231
         OR a=87
  ]])
    end, {
        -- <where7-2.229.1>
        8, 21, 34, 60, 86, 87, 91, "scan", 0, "sort", 0
        -- </where7-2.229.1>
    })

test:do_test(
    "where7-2.229.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='gfedcba' AND f GLOB 'nopqr*')
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR b=231
         OR a=87
  ]])
    end, {
        -- <where7-2.229.2>
        8, 21, 34, 60, 86, 87, 91, "scan", 0, "sort", 0
        -- </where7-2.229.2>
    })

test:do_test(
    "where7-2.230.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=77
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR (g='rqponml' AND f GLOB 'hijkl*')
         OR c=24024
         OR c=5005
  ]])
    end, {
        -- <where7-2.230.1>
        13, 14, 15, 33, 65, 70, 71, 72, 77, "scan", 0, "sort", 0
        -- </where7-2.230.1>
    })

test:do_test(
    "where7-2.230.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=77
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR (g='rqponml' AND f GLOB 'hijkl*')
         OR c=24024
         OR c=5005
  ]])
    end, {
        -- <where7-2.230.2>
        13, 14, 15, 33, 65, 70, 71, 72, 77, "scan", 0, "sort", 0
        -- </where7-2.230.2>
    })

test:do_test(
    "where7-2.231.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='mlkjihg' AND f GLOB 'ijklm*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR b=682
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.231.1>
        22, 29, 34, 60, 62, 64, 65, 66, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.231.1>
    })

test:do_test(
    "where7-2.231.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='mlkjihg' AND f GLOB 'ijklm*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR b=682
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.231.2>
        22, 29, 34, 60, 62, 64, 65, 66, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.231.2>
    })

test:do_test(
    "where7-2.232.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=212
         OR b=121
         OR c=2002
         OR ((a BETWEEN 84 AND 86) AND a!=85)
         OR (g='jihgfed' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.232.1>
        4, 5, 6, 11, 75, 84, 86, "scan", 0, "sort", 0
        -- </where7-2.232.1>
    })

test:do_test(
    "where7-2.232.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=212
         OR b=121
         OR c=2002
         OR ((a BETWEEN 84 AND 86) AND a!=85)
         OR (g='jihgfed' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.232.2>
        4, 5, 6, 11, 75, 84, 86, "scan", 0, "sort", 0
        -- </where7-2.232.2>
    })

test:do_test(
    "where7-2.233.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR f='abcdefghi'
         OR b=267
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR a=82
         OR a=54
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=1078
  ]])
    end, {
        -- <where7-2.233.1>
        16, 20, 26, 52, 54, 55, 78, 82, 98, "scan", 0, "sort", 0
        -- </where7-2.233.1>
    })

test:do_test(
    "where7-2.233.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR f='abcdefghi'
         OR b=267
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR a=82
         OR a=54
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=1078
  ]])
    end, {
        -- <where7-2.233.2>
        16, 20, 26, 52, 54, 55, 78, 82, 98, "scan", 0, "sort", 0
        -- </where7-2.233.2>
    })

test:do_test(
    "where7-2.234.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR f='hijklmnop'
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.234.1>
        7, 33, 34, 59, 85, 93, "scan", 0, "sort", 0
        -- </where7-2.234.1>
    })

test:do_test(
    "where7-2.234.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR f='hijklmnop'
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.234.2>
        7, 33, 34, 59, 85, 93, "scan", 0, "sort", 0
        -- </where7-2.234.2>
    })

test:do_test(
    "where7-2.235.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 98 AND 100) AND a!=99)
         OR ((a BETWEEN 51 AND 53) AND a!=52)
         OR a=18
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR 1000000<b
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
  ]])
    end, {
        -- <where7-2.235.1>
        7, 14, 18, 31, 33, 37, 40, 51, 53, 59, 66, 85, 92, 94, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.235.1>
    })

test:do_test(
    "where7-2.235.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 98 AND 100) AND a!=99)
         OR ((a BETWEEN 51 AND 53) AND a!=52)
         OR a=18
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR 1000000<b
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
  ]])
    end, {
        -- <where7-2.235.2>
        7, 14, 18, 31, 33, 37, 40, 51, 53, 59, 66, 85, 92, 94, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.235.2>
    })

test:do_test(
    "where7-2.236.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1001
         OR b=168
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.236.1>
        7, 33, 59, 85, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.236.1>
    })

test:do_test(
    "where7-2.236.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1001
         OR b=168
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.236.2>
        7, 33, 59, 85, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.236.2>
    })

test:do_test(
    "where7-2.237.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=51
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
         OR b=330
  ]])
    end, {
        -- <where7-2.237.1>
        30, 51, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.237.1>
    })

test:do_test(
    "where7-2.237.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=51
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
         OR b=330
  ]])
    end, {
        -- <where7-2.237.2>
        30, 51, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.237.2>
    })

test:do_test(
    "where7-2.238.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR b=704
         OR a=62
         OR f='pqrstuvwx'
         OR b=495
         OR c=26026
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR b<0
         OR b=597
  ]])
    end, {
        -- <where7-2.238.1>
        15, 41, 45, 62, 64, 67, 68, 71, 76, 77, 78, 93, "scan", 0, "sort", 0
        -- </where7-2.238.1>
    })

test:do_test(
    "where7-2.238.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR b=704
         OR a=62
         OR f='pqrstuvwx'
         OR b=495
         OR c=26026
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR b<0
         OR b=597
  ]])
    end, {
        -- <where7-2.238.2>
        15, 41, 45, 62, 64, 67, 68, 71, 76, 77, 78, 93, "scan", 0, "sort", 0
        -- </where7-2.238.2>
    })

test:do_test(
    "where7-2.239.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR b=520
         OR ((a BETWEEN 47 AND 49) AND a!=48)
         OR f IS NULL
  ]])
    end, {
        -- <where7-2.239.1>
        2, 47, 49, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.239.1>
    })

test:do_test(
    "where7-2.239.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR b=520
         OR ((a BETWEEN 47 AND 49) AND a!=48)
         OR f IS NULL
  ]])
    end, {
        -- <where7-2.239.2>
        2, 47, 49, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.239.2>
    })

test:do_test(
    "where7-2.240.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=14014
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=572
         OR c=15015
  ]])
    end, {
        -- <where7-2.240.1>
        40, 41, 42, 43, 44, 45, 52, 95, "scan", 0, "sort", 0
        -- </where7-2.240.1>
    })

test:do_test(
    "where7-2.240.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=14014
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=572
         OR c=15015
  ]])
    end, {
        -- <where7-2.240.2>
        40, 41, 42, 43, 44, 45, 52, 95, "scan", 0, "sort", 0
        -- </where7-2.240.2>
    })

test:do_test(
    "where7-2.241.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR b=850
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 15 AND 17) AND a!=16)
         OR b=88
         OR f='hijklmnop'
         OR b=806
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR b=88
  ]])
    end, {
        -- <where7-2.241.1>
        3, 7, 8, 15, 17, 29, 33, 46, 55, 59, 65, 81, 85, "scan", 0, "sort", 0
        -- </where7-2.241.1>
    })

test:do_test(
    "where7-2.241.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR b=850
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 15 AND 17) AND a!=16)
         OR b=88
         OR f='hijklmnop'
         OR b=806
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR b=88
  ]])
    end, {
        -- <where7-2.241.2>
        3, 7, 8, 15, 17, 29, 33, 46, 55, 59, 65, 81, 85, "scan", 0, "sort", 0
        -- </where7-2.241.2>
    })

test:do_test(
    "where7-2.242.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=817
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR a=36
         OR b=960
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR b=374
         OR b=938
         OR b=773
         OR (g='jihgfed' AND f GLOB 'zabcd*')
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
  ]])
    end, {
        -- <where7-2.242.1>
        34, 36, 55, 58, 63, 77, "scan", 0, "sort", 0
        -- </where7-2.242.1>
    })

test:do_test(
    "where7-2.242.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=817
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR a=36
         OR b=960
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR b=374
         OR b=938
         OR b=773
         OR (g='jihgfed' AND f GLOB 'zabcd*')
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
  ]])
    end, {
        -- <where7-2.242.2>
        34, 36, 55, 58, 63, 77, "scan", 0, "sort", 0
        -- </where7-2.242.2>
    })

test:do_test(
    "where7-2.243.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=146
  ]])
    end, {
        -- <where7-2.243.1>
        69, "scan", 0, "sort", 0
        -- </where7-2.243.1>
    })

test:do_test(
    "where7-2.243.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=146
  ]])
    end, {
        -- <where7-2.243.2>
        69, "scan", 0, "sort", 0
        -- </where7-2.243.2>
    })

test:do_test(
    "where7-2.244.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='pqrstuvwx'
         OR ((a BETWEEN 6 AND 8) AND a!=7)
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR b=704
         OR a=18
  ]])
    end, {
        -- <where7-2.244.1>
        6, 8, 15, 18, 41, 64, 67, 76, 78, 93, "scan", 0, "sort", 0
        -- </where7-2.244.1>
    })

test:do_test(
    "where7-2.244.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='pqrstuvwx'
         OR ((a BETWEEN 6 AND 8) AND a!=7)
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR b=704
         OR a=18
  ]])
    end, {
        -- <where7-2.244.2>
        6, 8, 15, 18, 41, 64, 67, 76, 78, 93, "scan", 0, "sort", 0
        -- </where7-2.244.2>
    })

test:do_test(
    "where7-2.245.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR b=399
         OR b=1004
         OR c=16016
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR b=671
         OR a=25
         OR a=30
         OR a=8
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.245.1>
        5, 8, 19, 25, 30, 31, 45, 46, 47, 48, 61, 71, 97, "scan", 0, "sort", 0
        -- </where7-2.245.1>
    })

test:do_test(
    "where7-2.245.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR b=399
         OR b=1004
         OR c=16016
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR b=671
         OR a=25
         OR a=30
         OR a=8
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.245.2>
        5, 8, 19, 25, 30, 31, 45, 46, 47, 48, 61, 71, 97, "scan", 0, "sort", 0
        -- </where7-2.245.2>
    })

test:do_test(
    "where7-2.246.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=561
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR b=594
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR b=861
         OR (d>=90.0 AND d<91.0 AND d IS NOT NULL)
         OR b=949
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
  ]])
    end, {
        -- <where7-2.246.1>
        18, 28, 30, 39, 41, 51, 54, 90, "scan", 0, "sort", 0
        -- </where7-2.246.1>
    })

test:do_test(
    "where7-2.246.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=561
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR b=594
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR b=861
         OR (d>=90.0 AND d<91.0 AND d IS NOT NULL)
         OR b=949
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
  ]])
    end, {
        -- <where7-2.246.2>
        18, 28, 30, 39, 41, 51, 54, 90, "scan", 0, "sort", 0
        -- </where7-2.246.2>
    })

test:do_test(
    "where7-2.247.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'hijkl*')
         OR a=83
         OR c=26026
         OR a=49
         OR a=57
         OR c=23023
         OR f='uvwxyzabc'
  ]])
    end, {
        -- <where7-2.247.1>
        7, 20, 46, 49, 57, 67, 68, 69, 72, 76, 77, 78, 83, 98, "scan", 0, "sort", 0
        -- </where7-2.247.1>
    })

test:do_test(
    "where7-2.247.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'hijkl*')
         OR a=83
         OR c=26026
         OR a=49
         OR a=57
         OR c=23023
         OR f='uvwxyzabc'
  ]])
    end, {
        -- <where7-2.247.2>
        7, 20, 46, 49, 57, 67, 68, 69, 72, 76, 77, 78, 83, 98, "scan", 0, "sort", 0
        -- </where7-2.247.2>
    })

test:do_test(
    "where7-2.248.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE d>1e10
         OR b=355
         OR f='stuvwxyza'
         OR b=22
  ]])
    end, {
        -- <where7-2.248.1>
        2, 18, 44, 70, 96, "scan", 0, "sort", 0
        -- </where7-2.248.1>
    })

test:do_test(
    "where7-2.248.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE d>1e10
         OR b=355
         OR f='stuvwxyza'
         OR b=22
  ]])
    end, {
        -- <where7-2.248.2>
        2, 18, 44, 70, 96, "scan", 0, "sort", 0
        -- </where7-2.248.2>
    })

test:do_test(
    "where7-2.249.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=451
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
  ]])
    end, {
        -- <where7-2.249.1>
        8, 34, 41, 60, 86, "scan", 0, "sort", 0
        -- </where7-2.249.1>
    })

test:do_test(
    "where7-2.249.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=451
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
  ]])
    end, {
        -- <where7-2.249.2>
        8, 34, 41, 60, 86, "scan", 0, "sort", 0
        -- </where7-2.249.2>
    })

test:do_test(
    "where7-2.250.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=47
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
  ]])
    end, {
        -- <where7-2.250.1>
        1, 27, 53, 79, "scan", 0, "sort", 0
        -- </where7-2.250.1>
    })

test:do_test(
    "where7-2.250.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=47
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
  ]])
    end, {
        -- <where7-2.250.2>
        1, 27, 53, 79, "scan", 0, "sort", 0
        -- </where7-2.250.2>
    })

test:do_test(
    "where7-2.251.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1037
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=344
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.251.1>
        12, 66, 68, 86, "scan", 0, "sort", 0
        -- </where7-2.251.1>
    })

test:do_test(
    "where7-2.251.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1037
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=344
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.251.2>
        12, 66, 68, 86, "scan", 0, "sort", 0
        -- </where7-2.251.2>
    })

test:do_test(
    "where7-2.252.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=506
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR b=429
         OR b=275
  ]])
    end, {
        -- <where7-2.252.1>
        20, 22, 25, 39, 46, 86, "scan", 0, "sort", 0
        -- </where7-2.252.1>
    })

test:do_test(
    "where7-2.252.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=506
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR b=429
         OR b=275
  ]])
    end, {
        -- <where7-2.252.2>
        20, 22, 25, 39, 46, 86, "scan", 0, "sort", 0
        -- </where7-2.252.2>
    })

test:do_test(
    "where7-2.253.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 60 AND 62) AND a!=61)
         OR a=28
         OR b=443
         OR b=363
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR a=60
         OR b=80
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR b=616
  ]])
    end, {
        -- <where7-2.253.1>
        28, 33, 47, 56, 60, 62, "scan", 0, "sort", 0
        -- </where7-2.253.1>
    })

test:do_test(
    "where7-2.253.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 60 AND 62) AND a!=61)
         OR a=28
         OR b=443
         OR b=363
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR a=60
         OR b=80
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR b=616
  ]])
    end, {
        -- <where7-2.253.2>
        28, 33, 47, 56, 60, 62, "scan", 0, "sort", 0
        -- </where7-2.253.2>
    })

test:do_test(
    "where7-2.254.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR b=660
  ]])
    end, {
        -- <where7-2.254.1>
        33, 60, "scan", 0, "sort", 0
        -- </where7-2.254.1>
    })

test:do_test(
    "where7-2.254.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR b=660
  ]])
    end, {
        -- <where7-2.254.2>
        33, 60, "scan", 0, "sort", 0
        -- </where7-2.254.2>
    })

test:do_test(
    "where7-2.255.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'hijkl*')
         OR a=43
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR b=586
         OR c=17017
         OR (g='onmlkji' AND f GLOB 'yzabc*')
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR a=87
         OR b=968
  ]])
    end, {
        -- <where7-2.255.1>
        7, 21, 43, 47, 49, 50, 51, 64, 66, 73, 87, 88, 99, "scan", 0, "sort", 0
        -- </where7-2.255.1>
    })

test:do_test(
    "where7-2.255.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'hijkl*')
         OR a=43
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR b=586
         OR c=17017
         OR (g='onmlkji' AND f GLOB 'yzabc*')
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR a=87
         OR b=968
  ]])
    end, {
        -- <where7-2.255.2>
        7, 21, 43, 47, 49, 50, 51, 64, 66, 73, 87, 88, 99, "scan", 0, "sort", 0
        -- </where7-2.255.2>
    })

test:do_test(
    "where7-2.256.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='klmnopqrs'
         OR b=982
         OR b=575
         OR b=110
         OR b=99
  ]])
    end, {
        -- <where7-2.256.1>
        9, 10, 36, 62, 88, "scan", 0, "sort", 0
        -- </where7-2.256.1>
    })

test:do_test(
    "where7-2.256.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='klmnopqrs'
         OR b=982
         OR b=575
         OR b=110
         OR b=99
  ]])
    end, {
        -- <where7-2.256.2>
        9, 10, 36, 62, 88, "scan", 0, "sort", 0
        -- </where7-2.256.2>
    })

test:do_test(
    "where7-2.257.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='rqponml' AND f GLOB 'jklmn*')
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR c>=34035
         OR b=850
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=924
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR b=355
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.257.1>
        4, 32, 34, 35, 37, 56, 78, 84, 86, "scan", 0, "sort", 0
        -- </where7-2.257.1>
    })

test:do_test(
    "where7-2.257.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='rqponml' AND f GLOB 'jklmn*')
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR c>=34035
         OR b=850
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=924
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR b=355
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.257.2>
        4, 32, 34, 35, 37, 56, 78, 84, 86, "scan", 0, "sort", 0
        -- </where7-2.257.2>
    })

test:do_test(
    "where7-2.258.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR b=982
         OR ((a BETWEEN 81 AND 83) AND a!=82)
         OR b=374
  ]])
    end, {
        -- <where7-2.258.1>
        34, 46, 81, 83, "scan", 0, "sort", 0
        -- </where7-2.258.1>
    })

test:do_test(
    "where7-2.258.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR b=982
         OR ((a BETWEEN 81 AND 83) AND a!=82)
         OR b=374
  ]])
    end, {
        -- <where7-2.258.2>
        34, 46, 81, 83, "scan", 0, "sort", 0
        -- </where7-2.258.2>
    })

test:do_test(
    "where7-2.259.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 87 AND 89) AND a!=88)
         OR b=814
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.259.1>
        19, 74, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.259.1>
    })

test:do_test(
    "where7-2.259.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 87 AND 89) AND a!=88)
         OR b=814
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.259.2>
        19, 74, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.259.2>
    })

test:do_test(
    "where7-2.260.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='qponmlk' AND f GLOB 'nopqr*')
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=993
  ]])
    end, {
        -- <where7-2.260.1>
        12, 39, "scan", 0, "sort", 0
        -- </where7-2.260.1>
    })

test:do_test(
    "where7-2.260.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='qponmlk' AND f GLOB 'nopqr*')
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=993
  ]])
    end, {
        -- <where7-2.260.2>
        12, 39, "scan", 0, "sort", 0
        -- </where7-2.260.2>
    })

test:do_test(
    "where7-2.261.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=234
         OR a=22
         OR b=289
         OR b=795
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR b=242
         OR a=59
         OR b=1045
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.261.1>
        8, 22, 59, 91, 95, "scan", 0, "sort", 0
        -- </where7-2.261.1>
    })

test:do_test(
    "where7-2.261.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=234
         OR a=22
         OR b=289
         OR b=795
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR b=242
         OR a=59
         OR b=1045
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.261.2>
        8, 22, 59, 91, 95, "scan", 0, "sort", 0
        -- </where7-2.261.2>
    })

test:do_test(
    "where7-2.262.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=245
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR c=3003
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR ((a BETWEEN 71 AND 73) AND a!=72)
  ]])
    end, {
        -- <where7-2.262.1>
        1, 7, 8, 9, 10, 26, 33, 52, 68, 70, 71, 73, 78, "scan", 0, "sort", 0
        -- </where7-2.262.1>
    })

test:do_test(
    "where7-2.262.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=245
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR c=3003
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR ((a BETWEEN 71 AND 73) AND a!=72)
  ]])
    end, {
        -- <where7-2.262.2>
        1, 7, 8, 9, 10, 26, 33, 52, 68, 70, 71, 73, 78, "scan", 0, "sort", 0
        -- </where7-2.262.2>
    })

test:do_test(
    "where7-2.263.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'jklmn*')
         OR b=220
         OR b=443
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR a=62
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR b=1023
         OR a=100
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.263.1>
        18, 20, 44, 55, 62, 70, 87, 93, 96, 97, 100, "scan", 0, "sort", 0
        -- </where7-2.263.1>
    })

test:do_test(
    "where7-2.263.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'jklmn*')
         OR b=220
         OR b=443
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR a=62
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR b=1023
         OR a=100
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.263.2>
        18, 20, 44, 55, 62, 70, 87, 93, 96, 97, 100, "scan", 0, "sort", 0
        -- </where7-2.263.2>
    })

test:do_test(
    "where7-2.264.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=11011
         OR f='tuvwxyzab'
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
  ]])
    end, {
        -- <where7-2.264.1>
        19, 31, 32, 33, 45, 47, 71, 84, 97, "scan", 0, "sort", 0
        -- </where7-2.264.1>
    })

test:do_test(
    "where7-2.264.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=11011
         OR f='tuvwxyzab'
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
  ]])
    end, {
        -- <where7-2.264.2>
        19, 31, 32, 33, 45, 47, 71, 84, 97, "scan", 0, "sort", 0
        -- </where7-2.264.2>
    })

test:do_test(
    "where7-2.265.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 93 AND 95) AND a!=94)
         OR a=79
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
         OR b=462
  ]])
    end, {
        -- <where7-2.265.1>
        39, 42, 79, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.265.1>
    })

test:do_test(
    "where7-2.265.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 93 AND 95) AND a!=94)
         OR a=79
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
         OR b=462
  ]])
    end, {
        -- <where7-2.265.2>
        39, 42, 79, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.265.2>
    })

test:do_test(
    "where7-2.266.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR b=146
         OR 1000000<b
         OR b=99
         OR ((a BETWEEN 75 AND 77) AND a!=76)
  ]])
    end, {
        -- <where7-2.266.1>
        9, 33, 75, 77, "scan", 0, "sort", 0
        -- </where7-2.266.1>
    })

test:do_test(
    "where7-2.266.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR b=146
         OR 1000000<b
         OR b=99
         OR ((a BETWEEN 75 AND 77) AND a!=76)
  ]])
    end, {
        -- <where7-2.266.2>
        9, 33, 75, 77, "scan", 0, "sort", 0
        -- </where7-2.266.2>
    })

test:do_test(
    "where7-2.267.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=935
         OR b=473
         OR a=28
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR ((a BETWEEN 62 AND 64) AND a!=63)
         OR a=62
         OR b=619
         OR a=82
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR c=14014
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.267.1>
        20, 28, 40, 41, 42, 43, 62, 64, 67, 82, 85, "scan", 0, "sort", 0
        -- </where7-2.267.1>
    })

test:do_test(
    "where7-2.267.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=935
         OR b=473
         OR a=28
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR ((a BETWEEN 62 AND 64) AND a!=63)
         OR a=62
         OR b=619
         OR a=82
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR c=14014
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.267.2>
        20, 28, 40, 41, 42, 43, 62, 64, 67, 82, 85, "scan", 0, "sort", 0
        -- </where7-2.267.2>
    })

test:do_test(
    "where7-2.268.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR b=443
         OR b=33
         OR b=762
         OR b=575
         OR c=16016
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR b=1092
  ]])
    end, {
        -- <where7-2.268.1>
        3, 40, 41, 43, 46, 47, 48, 72, "scan", 0, "sort", 0
        -- </where7-2.268.1>
    })

test:do_test(
    "where7-2.268.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR b=443
         OR b=33
         OR b=762
         OR b=575
         OR c=16016
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR b=1092
  ]])
    end, {
        -- <where7-2.268.2>
        3, 40, 41, 43, 46, 47, 48, 72, "scan", 0, "sort", 0
        -- </where7-2.268.2>
    })

test:do_test(
    "where7-2.269.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=806
         OR b=872
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR f='uvwxyzabc'
         OR b=748
         OR b=586
         OR ((a BETWEEN 15 AND 17) AND a!=16)
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR b=891
  ]])
    end, {
        -- <where7-2.269.1>
        15, 17, 20, 32, 34, 46, 68, 72, 80, 81, 88, 98, "scan", 0, "sort", 0
        -- </where7-2.269.1>
    })

test:do_test(
    "where7-2.269.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=806
         OR b=872
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR f='uvwxyzabc'
         OR b=748
         OR b=586
         OR ((a BETWEEN 15 AND 17) AND a!=16)
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR b=891
  ]])
    end, {
        -- <where7-2.269.2>
        15, 17, 20, 32, 34, 46, 68, 72, 80, 81, 88, 98, "scan", 0, "sort", 0
        -- </where7-2.269.2>
    })

test:do_test(
    "where7-2.270.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=693
         OR f='fghijklmn'
         OR (g='rqponml' AND f GLOB 'hijkl*')
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 71 AND 73) AND a!=72)
         OR a=96
  ]])
    end, {
        -- <where7-2.270.1>
        5, 31, 33, 39, 57, 63, 71, 73, 83, 96, "scan", 0, "sort", 0
        -- </where7-2.270.1>
    })

test:do_test(
    "where7-2.270.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=693
         OR f='fghijklmn'
         OR (g='rqponml' AND f GLOB 'hijkl*')
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 71 AND 73) AND a!=72)
         OR a=96
  ]])
    end, {
        -- <where7-2.270.2>
        5, 31, 33, 39, 57, 63, 71, 73, 83, 96, "scan", 0, "sort", 0
        -- </where7-2.270.2>
    })

test:do_test(
    "where7-2.271.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'ijklm*')
         OR b=451
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR a=84
  ]])
    end, {
        -- <where7-2.271.1>
        41, 84, 86, 96, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.271.1>
    })

test:do_test(
    "where7-2.271.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'ijklm*')
         OR b=451
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR a=84
  ]])
    end, {
        -- <where7-2.271.2>
        41, 84, 86, 96, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.271.2>
    })

test:do_test(
    "where7-2.272.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='nmlkjih' AND f GLOB 'bcdef*')
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR a=75
         OR b=960
         OR (g='tsrqpon' AND f GLOB 'yzabc*')
         OR b=616
         OR b=330
         OR ((a BETWEEN 16 AND 18) AND a!=17)
         OR a=26
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.272.1>
        16, 18, 24, 26, 30, 53, 56, 63, 72, 75, "scan", 0, "sort", 0
        -- </where7-2.272.1>
    })

test:do_test(
    "where7-2.272.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='nmlkjih' AND f GLOB 'bcdef*')
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR a=75
         OR b=960
         OR (g='tsrqpon' AND f GLOB 'yzabc*')
         OR b=616
         OR b=330
         OR ((a BETWEEN 16 AND 18) AND a!=17)
         OR a=26
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.272.2>
        16, 18, 24, 26, 30, 53, 56, 63, 72, 75, "scan", 0, "sort", 0
        -- </where7-2.272.2>
    })

test:do_test(
    "where7-2.273.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=762
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.273.1>
        53, "scan", 0, "sort", 0
        -- </where7-2.273.1>
    })

test:do_test(
    "where7-2.273.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=762
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.273.2>
        53, "scan", 0, "sort", 0
        -- </where7-2.273.2>
    })

test:do_test(
    "where7-2.274.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=14
         OR a=23
         OR b=748
         OR b=407
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR b=979
         OR ((a BETWEEN 15 AND 17) AND a!=16)
  ]])
    end, {
        -- <where7-2.274.1>
        4, 15, 17, 23, 37, 68, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.274.1>
    })

test:do_test(
    "where7-2.274.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=14
         OR a=23
         OR b=748
         OR b=407
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR b=979
         OR ((a BETWEEN 15 AND 17) AND a!=16)
  ]])
    end, {
        -- <where7-2.274.2>
        4, 15, 17, 23, 37, 68, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.274.2>
    })

test:do_test(
    "where7-2.275.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 36 AND 38) AND a!=37)
         OR a=92
  ]])
    end, {
        -- <where7-2.275.1>
        36, 38, 92, "scan", 0, "sort", 0
        -- </where7-2.275.1>
    })

test:do_test(
    "where7-2.275.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 36 AND 38) AND a!=37)
         OR a=92
  ]])
    end, {
        -- <where7-2.275.2>
        36, 38, 92, "scan", 0, "sort", 0
        -- </where7-2.275.2>
    })

test:do_test(
    "where7-2.276.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=14014
         OR b=927
         OR b=176
         OR ((a BETWEEN 34 AND 36) AND a!=35)
         OR b=220
         OR (g='tsrqpon' AND f GLOB 'yzabc*')
         OR a=4
  ]])
    end, {
        -- <where7-2.276.1>
        4, 16, 20, 24, 34, 36, 40, 41, 42, "scan", 0, "sort", 0
        -- </where7-2.276.1>
    })

test:do_test(
    "where7-2.276.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=14014
         OR b=927
         OR b=176
         OR ((a BETWEEN 34 AND 36) AND a!=35)
         OR b=220
         OR (g='tsrqpon' AND f GLOB 'yzabc*')
         OR a=4
  ]])
    end, {
        -- <where7-2.276.2>
        4, 16, 20, 24, 34, 36, 40, 41, 42, "scan", 0, "sort", 0
        -- </where7-2.276.2>
    })

test:do_test(
    "where7-2.277.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=29
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR b=979
         OR b=275
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=41.0 AND d<42.0 AND d IS NOT NULL)
         OR b=539
         OR a=87
  ]])
    end, {
        -- <where7-2.277.1>
        19, 25, 29, 41, 49, 56, 58, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.277.1>
    })

test:do_test(
    "where7-2.277.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=29
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR b=979
         OR b=275
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=41.0 AND d<42.0 AND d IS NOT NULL)
         OR b=539
         OR a=87
  ]])
    end, {
        -- <where7-2.277.2>
        19, 25, 29, 41, 49, 56, 58, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.277.2>
    })

test:do_test(
    "where7-2.278.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 43 AND 45) AND a!=44)
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR f='fghijklmn'
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR a=74
         OR ((a BETWEEN 7 AND 9) AND a!=8)
  ]])
    end, {
        -- <where7-2.278.1>
        4, 5, 6, 7, 9, 31, 36, 43, 45, 57, 59, 69, 74, 83, "scan", 0, "sort", 0
        -- </where7-2.278.1>
    })

test:do_test(
    "where7-2.278.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 43 AND 45) AND a!=44)
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR f='fghijklmn'
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR a=74
         OR ((a BETWEEN 7 AND 9) AND a!=8)
  ]])
    end, {
        -- <where7-2.278.2>
        4, 5, 6, 7, 9, 31, 36, 43, 45, 57, 59, 69, 74, 83, "scan", 0, "sort", 0
        -- </where7-2.278.2>
    })

test:do_test(
    "where7-2.279.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 80 AND 82) AND a!=81)
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.279.1>
        8, 34, 42, 49, 51, 60, 79, 80, 82, 86, "scan", 0, "sort", 0
        -- </where7-2.279.1>
    })

test:do_test(
    "where7-2.279.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 80 AND 82) AND a!=81)
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.279.2>
        8, 34, 42, 49, 51, 60, 79, 80, 82, 86, "scan", 0, "sort", 0
        -- </where7-2.279.2>
    })

test:do_test(
    "where7-2.280.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 58 AND 60) AND a!=59)
         OR b=696
         OR f='tuvwxyzab'
         OR b=374
         OR b=110
         OR a=90
  ]])
    end, {
        -- <where7-2.280.1>
        10, 19, 34, 45, 58, 60, 71, 90, 97, "scan", 0, "sort", 0
        -- </where7-2.280.1>
    })

test:do_test(
    "where7-2.280.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 58 AND 60) AND a!=59)
         OR b=696
         OR f='tuvwxyzab'
         OR b=374
         OR b=110
         OR a=90
  ]])
    end, {
        -- <where7-2.280.2>
        10, 19, 34, 45, 58, 60, 71, 90, 97, "scan", 0, "sort", 0
        -- </where7-2.280.2>
    })

test:do_test(
    "where7-2.281.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'ghijk*')
         OR c=23023
         OR b=377
         OR b=858
         OR (g='nmlkjih' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.281.1>
        6, 57, 67, 68, 69, 78, "scan", 0, "sort", 0
        -- </where7-2.281.1>
    })

test:do_test(
    "where7-2.281.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'ghijk*')
         OR c=23023
         OR b=377
         OR b=858
         OR (g='nmlkjih' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.281.2>
        6, 57, 67, 68, 69, 78, "scan", 0, "sort", 0
        -- </where7-2.281.2>
    })

test:do_test(
    "where7-2.282.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR b=322
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR (g='fedcbaz' AND f GLOB 'pqrst*')
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR b=432
         OR b=55
         OR a=53
         OR (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR b=25
  ]])
    end, {
        -- <where7-2.282.1>
        5, 7, 19, 33, 38, 48, 53, 59, 74, 85, 93, "scan", 0, "sort", 0
        -- </where7-2.282.1>
    })

test:do_test(
    "where7-2.282.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR b=322
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR (g='fedcbaz' AND f GLOB 'pqrst*')
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR b=432
         OR b=55
         OR a=53
         OR (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR b=25
  ]])
    end, {
        -- <where7-2.282.2>
        5, 7, 19, 33, 38, 48, 53, 59, 74, 85, 93, "scan", 0, "sort", 0
        -- </where7-2.282.2>
    })

test:do_test(
    "where7-2.283.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=484
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR b=616
         OR c=5005
         OR ((a BETWEEN 27 AND 29) AND a!=28)
  ]])
    end, {
        -- <where7-2.283.1>
        13, 14, 15, 27, 29, 44, 56, 74, "scan", 0, "sort", 0
        -- </where7-2.283.1>
    })

test:do_test(
    "where7-2.283.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=484
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR b=616
         OR c=5005
         OR ((a BETWEEN 27 AND 29) AND a!=28)
  ]])
    end, {
        -- <where7-2.283.2>
        13, 14, 15, 27, 29, 44, 56, 74, "scan", 0, "sort", 0
        -- </where7-2.283.2>
    })

test:do_test(
    "where7-2.284.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=916
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR b=1048
         OR c=6006
         OR b=762
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR b=751
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.284.1>
        13, 14, 16, 17, 18, 39, 40, 59, 61, 65, 66, 73, 91, 92, "scan", 0, "sort", 0
        -- </where7-2.284.1>
    })

test:do_test(
    "where7-2.284.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=916
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR b=1048
         OR c=6006
         OR b=762
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR b=751
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.284.2>
        13, 14, 16, 17, 18, 39, 40, 59, 61, 65, 66, 73, 91, 92, "scan", 0, "sort", 0
        -- </where7-2.284.2>
    })

test:do_test(
    "where7-2.285.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=32.0 AND d<33.0 AND d IS NOT NULL)
         OR b=927
         OR b=275
         OR b=396
         OR c=4004
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR b=319
         OR ((a BETWEEN 83 AND 85) AND a!=84)
         OR a=3
         OR ((a BETWEEN 73 AND 75) AND a!=74)
  ]])
    end, {
        -- <where7-2.285.1>
        3, 10, 11, 12, 14, 25, 29, 32, 36, 73, 75, 83, 85, "scan", 0, "sort", 0
        -- </where7-2.285.1>
    })

test:do_test(
    "where7-2.285.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=32.0 AND d<33.0 AND d IS NOT NULL)
         OR b=927
         OR b=275
         OR b=396
         OR c=4004
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR b=319
         OR ((a BETWEEN 83 AND 85) AND a!=84)
         OR a=3
         OR ((a BETWEEN 73 AND 75) AND a!=74)
  ]])
    end, {
        -- <where7-2.285.2>
        3, 10, 11, 12, 14, 25, 29, 32, 36, 73, 75, 83, 85, "scan", 0, "sort", 0
        -- </where7-2.285.2>
    })

test:do_test(
    "where7-2.286.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='wvutsrq' AND f GLOB 'lmnop*')
         OR b=718
         OR f='vwxyzabcd'
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.286.1>
        9, 11, 19, 21, 22, 35, 45, 47, 61, 66, 68, 71, 73, 87, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.286.1>
    })

test:do_test(
    "where7-2.286.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='wvutsrq' AND f GLOB 'lmnop*')
         OR b=718
         OR f='vwxyzabcd'
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.286.2>
        9, 11, 19, 21, 22, 35, 45, 47, 61, 66, 68, 71, 73, 87, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.286.2>
    })

test:do_test(
    "where7-2.287.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=187
         OR b=1056
         OR b=861
         OR b=1081
         OR b=572
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR a=11
         OR ((a BETWEEN 99 AND 101) AND a!=100)
         OR a=89
         OR b=421
  ]])
    end, {
        -- <where7-2.287.1>
        4, 11, 17, 52, 89, 96, 99, "scan", 0, "sort", 0
        -- </where7-2.287.1>
    })

test:do_test(
    "where7-2.287.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=187
         OR b=1056
         OR b=861
         OR b=1081
         OR b=572
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR a=11
         OR ((a BETWEEN 99 AND 101) AND a!=100)
         OR a=89
         OR b=421
  ]])
    end, {
        -- <where7-2.287.2>
        4, 11, 17, 52, 89, 96, 99, "scan", 0, "sort", 0
        -- </where7-2.287.2>
    })

test:do_test(
    "where7-2.288.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=231
         OR b=388
         OR d<0.0
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
         OR b=1045
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.288.1>
        12, 21, 39, 95, "scan", 0, "sort", 0
        -- </where7-2.288.1>
    })

test:do_test(
    "where7-2.288.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=231
         OR b=388
         OR d<0.0
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
         OR b=1045
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.288.2>
        12, 21, 39, 95, "scan", 0, "sort", 0
        -- </where7-2.288.2>
    })

test:do_test(
    "where7-2.289.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=528
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR b=762
  ]])
    end, {
        -- <where7-2.289.1>
        48, 53, "scan", 0, "sort", 0
        -- </where7-2.289.1>
    })

test:do_test(
    "where7-2.289.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=528
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR b=762
  ]])
    end, {
        -- <where7-2.289.2>
        48, 53, "scan", 0, "sort", 0
        -- </where7-2.289.2>
    })

test:do_test(
    "where7-2.290.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='stuvwxyza'
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR b=916
  ]])
    end, {
        -- <where7-2.290.1>
        18, 44, 70, 90, 92, 96, "scan", 0, "sort", 0
        -- </where7-2.290.1>
    })

test:do_test(
    "where7-2.290.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='stuvwxyza'
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR b=916
  ]])
    end, {
        -- <where7-2.290.2>
        18, 44, 70, 90, 92, 96, "scan", 0, "sort", 0
        -- </where7-2.290.2>
    })

test:do_test(
    "where7-2.291.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.291.1>
        4, 19, 52, 76, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.291.1>
    })

test:do_test(
    "where7-2.291.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.291.2>
        4, 19, 52, 76, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.291.2>
    })

test:do_test(
    "where7-2.292.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=102
         OR c=6006
         OR b=231
         OR b=212
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR c=30030
         OR (g='onmlkji' AND f GLOB 'abcde*')
  ]])
    end, {
        -- <where7-2.292.1>
        16, 17, 18, 21, 36, 52, 88, 89, 90, "scan", 0, "sort", 0
        -- </where7-2.292.1>
    })

test:do_test(
    "where7-2.292.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=102
         OR c=6006
         OR b=231
         OR b=212
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR c=30030
         OR (g='onmlkji' AND f GLOB 'abcde*')
  ]])
    end, {
        -- <where7-2.292.2>
        16, 17, 18, 21, 36, 52, 88, 89, 90, "scan", 0, "sort", 0
        -- </where7-2.292.2>
    })

test:do_test(
    "where7-2.293.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=47
         OR a=82
         OR c=25025
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR f='qrstuvwxy'
         OR a=5
  ]])
    end, {
        -- <where7-2.293.1>
        5, 16, 40, 42, 47, 68, 73, 74, 75, 82, 94, "scan", 0, "sort", 0
        -- </where7-2.293.1>
    })

test:do_test(
    "where7-2.293.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=47
         OR a=82
         OR c=25025
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR f='qrstuvwxy'
         OR a=5
  ]])
    end, {
        -- <where7-2.293.2>
        5, 16, 40, 42, 47, 68, 73, 74, 75, 82, 94, "scan", 0, "sort", 0
        -- </where7-2.293.2>
    })

test:do_test(
    "where7-2.294.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=429
         OR a=30
         OR f='vwxyzabcd'
         OR b=762
         OR a=60
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR (g='xwvutsr' AND f GLOB 'efghi*')
  ]])
    end, {
        -- <where7-2.294.1>
        4, 21, 30, 39, 47, 60, 73, 99, "scan", 0, "sort", 0
        -- </where7-2.294.1>
    })

test:do_test(
    "where7-2.294.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=429
         OR a=30
         OR f='vwxyzabcd'
         OR b=762
         OR a=60
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR (g='xwvutsr' AND f GLOB 'efghi*')
  ]])
    end, {
        -- <where7-2.294.2>
        4, 21, 30, 39, 47, 60, 73, 99, "scan", 0, "sort", 0
        -- </where7-2.294.2>
    })

test:do_test(
    "where7-2.295.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='mlkjihg' AND f GLOB 'ghijk*')
         OR a=3
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR b=498
         OR a=100
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR a=69
  ]])
    end, {
        -- <where7-2.295.1>
        3, 13, 31, 39, 58, 63, 65, 69, 91, 100, "scan", 0, "sort", 0
        -- </where7-2.295.1>
    })

test:do_test(
    "where7-2.295.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='mlkjihg' AND f GLOB 'ghijk*')
         OR a=3
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR b=498
         OR a=100
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR a=69
  ]])
    end, {
        -- <where7-2.295.2>
        3, 13, 31, 39, 58, 63, 65, 69, 91, 100, "scan", 0, "sort", 0
        -- </where7-2.295.2>
    })

test:do_test(
    "where7-2.296.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ihgfedc' AND f GLOB 'efghi*')
         OR b=300
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
         OR b=58
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR b=286
         OR b=234
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR f='ghijklmno'
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.296.1>
        6, 7, 26, 32, 43, 45, 55, 57, 58, 82, 84, "scan", 0, "sort", 0
        -- </where7-2.296.1>
    })

test:do_test(
    "where7-2.296.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ihgfedc' AND f GLOB 'efghi*')
         OR b=300
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
         OR b=58
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR b=286
         OR b=234
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR f='ghijklmno'
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.296.2>
        6, 7, 26, 32, 43, 45, 55, 57, 58, 82, 84, "scan", 0, "sort", 0
        -- </where7-2.296.2>
    })

test:do_test(
    "where7-2.297.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=95
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR b=594
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
         OR ((a BETWEEN 53 AND 55) AND a!=54)
  ]])
    end, {
        -- <where7-2.297.1>
        5, 7, 18, 20, 23, 25, 31, 33, 37, 39, 45, 53, 54, 55, 56, 57, 58, 59, 72, 74, 83, 85, 95, "scan", 0, "sort", 0
        -- </where7-2.297.1>
    })

test:do_test(
    "where7-2.297.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=95
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR b=594
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
         OR ((a BETWEEN 53 AND 55) AND a!=54)
  ]])
    end, {
        -- <where7-2.297.2>
        5, 7, 18, 20, 23, 25, 31, 33, 37, 39, 45, 53, 54, 55, 56, 57, 58, 59, 72, 74, 83, 85, 95, "scan", 0, "sort", 0
        -- </where7-2.297.2>
    })

test:do_test(
    "where7-2.298.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=949
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.298.1>
        5, 14, "scan", 0, "sort", 0
        -- </where7-2.298.1>
    })

test:do_test(
    "where7-2.298.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=949
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.298.2>
        5, 14, "scan", 0, "sort", 0
        -- </where7-2.298.2>
    })

test:do_test(
    "where7-2.299.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=960
         OR a=44
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR a=39
         OR b=828
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR d<0.0
         OR b=770
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR b=594
         OR ((a BETWEEN 89 AND 91) AND a!=90)
  ]])
    end, {
        -- <where7-2.299.1>
        3, 5, 6, 18, 39, 44, 54, 70, 89, 91, 96, "scan", 0, "sort", 0
        -- </where7-2.299.1>
    })

test:do_test(
    "where7-2.299.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=960
         OR a=44
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR a=39
         OR b=828
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR d<0.0
         OR b=770
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR b=594
         OR ((a BETWEEN 89 AND 91) AND a!=90)
  ]])
    end, {
        -- <where7-2.299.2>
        3, 5, 6, 18, 39, 44, 54, 70, 89, 91, 96, "scan", 0, "sort", 0
        -- </where7-2.299.2>
    })

test:do_test(
    "where7-2.300.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 40 AND 42) AND a!=41)
         OR b=198
         OR a=51
         OR b=1056
         OR b=748
         OR ((a BETWEEN 9 AND 11) AND a!=10)
  ]])
    end, {
        -- <where7-2.300.1>
        9, 11, 18, 40, 42, 51, 68, 96, "scan", 0, "sort", 0
        -- </where7-2.300.1>
    })

test:do_test(
    "where7-2.300.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 40 AND 42) AND a!=41)
         OR b=198
         OR a=51
         OR b=1056
         OR b=748
         OR ((a BETWEEN 9 AND 11) AND a!=10)
  ]])
    end, {
        -- <where7-2.300.2>
        9, 11, 18, 40, 42, 51, 68, 96, "scan", 0, "sort", 0
        -- </where7-2.300.2>
    })

test:do_test(
    "where7-2.301.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1081
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=1004
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 29 AND 31) AND a!=30)
         OR b=660
         OR b=957
         OR b=869
  ]])
    end, {
        -- <where7-2.301.1>
        29, 31, 60, 66, 68, 79, 87, 91, "scan", 0, "sort", 0
        -- </where7-2.301.1>
    })

test:do_test(
    "where7-2.301.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1081
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=1004
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 29 AND 31) AND a!=30)
         OR b=660
         OR b=957
         OR b=869
  ]])
    end, {
        -- <where7-2.301.2>
        29, 31, 60, 66, 68, 79, 87, 91, "scan", 0, "sort", 0
        -- </where7-2.301.2>
    })

test:do_test(
    "where7-2.302.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=927
         OR c=12012
         OR f='yzabcdefg'
         OR b=880
         OR a=63
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
  ]])
    end, {
        -- <where7-2.302.1>
        24, 34, 35, 36, 44, 50, 58, 63, 76, 80, 86, "scan", 0, "sort", 0
        -- </where7-2.302.1>
    })

test:do_test(
    "where7-2.302.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=927
         OR c=12012
         OR f='yzabcdefg'
         OR b=880
         OR a=63
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
  ]])
    end, {
        -- <where7-2.302.2>
        24, 34, 35, 36, 44, 50, 58, 63, 76, 80, 86, "scan", 0, "sort", 0
        -- </where7-2.302.2>
    })

test:do_test(
    "where7-2.303.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=69
         OR b=1103
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR f='wxyzabcde'
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR f='pqrstuvwx'
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR a=59
         OR b=946
  ]])
    end, {
        -- <where7-2.303.1>
        15, 18, 22, 26, 41, 44, 48, 52, 59, 67, 69, 70, 73, 74, 78, 86, 88, 93, 96, 100, "scan", 0, "sort", 0
        -- </where7-2.303.1>
    })

test:do_test(
    "where7-2.303.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=69
         OR b=1103
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR f='wxyzabcde'
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR f='pqrstuvwx'
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR a=59
         OR b=946
  ]])
    end, {
        -- <where7-2.303.2>
        15, 18, 22, 26, 41, 44, 48, 52, 59, 67, 69, 70, 73, 74, 78, 86, 88, 93, 96, 100, "scan", 0, "sort", 0
        -- </where7-2.303.2>
    })

test:do_test(
    "where7-2.304.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'xyzab*')
         OR a=68
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.304.1>
        14, 16, 47, 68, 75, "scan", 0, "sort", 0
        -- </where7-2.304.1>
    })

test:do_test(
    "where7-2.304.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'xyzab*')
         OR a=68
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.304.2>
        14, 16, 47, 68, 75, "scan", 0, "sort", 0
        -- </where7-2.304.2>
    })

test:do_test(
    "where7-2.305.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.305.1>
        10, 63, "scan", 0, "sort", 0
        -- </where7-2.305.1>
    })

test:do_test(
    "where7-2.305.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.305.2>
        10, 63, "scan", 0, "sort", 0
        -- </where7-2.305.2>
    })

test:do_test(
    "where7-2.306.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=32
         OR ((a BETWEEN 15 AND 17) AND a!=16)
         OR ((a BETWEEN 92 AND 94) AND a!=93)
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR c=7007
         OR b=968
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.306.1>
        15, 17, 18, 19, 20, 21, 32, 86, 88, 92, 94, "scan", 0, "sort", 0
        -- </where7-2.306.1>
    })

test:do_test(
    "where7-2.306.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=32
         OR ((a BETWEEN 15 AND 17) AND a!=16)
         OR ((a BETWEEN 92 AND 94) AND a!=93)
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR c=7007
         OR b=968
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.306.2>
        15, 17, 18, 19, 20, 21, 32, 86, 88, 92, 94, "scan", 0, "sort", 0
        -- </where7-2.306.2>
    })

test:do_test(
    "where7-2.307.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='cdefghijk'
         OR b=1103
  ]])
    end, {
        -- <where7-2.307.1>
        2, 28, 54, 80, "scan", 0, "sort", 0
        -- </where7-2.307.1>
    })

test:do_test(
    "where7-2.307.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='cdefghijk'
         OR b=1103
  ]])
    end, {
        -- <where7-2.307.2>
        2, 28, 54, 80, "scan", 0, "sort", 0
        -- </where7-2.307.2>
    })

test:do_test(
    "where7-2.308.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 21 AND 23) AND a!=22)
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR c=14014
         OR b=990
         OR (g='nmlkjih' AND f GLOB 'efghi*')
         OR c=14014
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
         OR b=740
         OR c=3003
  ]])
    end, {
        -- <where7-2.308.1>
        7, 8, 9, 13, 14, 21, 23, 40, 41, 42, 56, 90, "scan", 0, "sort", 0
        -- </where7-2.308.1>
    })

test:do_test(
    "where7-2.308.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 21 AND 23) AND a!=22)
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR c=14014
         OR b=990
         OR (g='nmlkjih' AND f GLOB 'efghi*')
         OR c=14014
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
         OR b=740
         OR c=3003
  ]])
    end, {
        -- <where7-2.308.2>
        7, 8, 9, 13, 14, 21, 23, 40, 41, 42, 56, 90, "scan", 0, "sort", 0
        -- </where7-2.308.2>
    })

test:do_test(
    "where7-2.309.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR a=67
         OR b=135
         OR f='bcdefghij'
         OR b=924
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.309.1>
        1, 22, 27, 53, 60, 67, 79, 84, "scan", 0, "sort", 0
        -- </where7-2.309.1>
    })

test:do_test(
    "where7-2.309.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR a=67
         OR b=135
         OR f='bcdefghij'
         OR b=924
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.309.2>
        1, 22, 27, 53, 60, 67, 79, 84, "scan", 0, "sort", 0
        -- </where7-2.309.2>
    })

test:do_test(
    "where7-2.310.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=96
         OR a=13
  ]])
    end, {
        -- <where7-2.310.1>
        13, 96, "scan", 0, "sort", 0
        -- </where7-2.310.1>
    })

test:do_test(
    "where7-2.310.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=96
         OR a=13
  ]])
    end, {
        -- <where7-2.310.2>
        13, 96, "scan", 0, "sort", 0
        -- </where7-2.310.2>
    })

test:do_test(
    "where7-2.311.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 34 AND 36) AND a!=35)
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR ((a BETWEEN 35 AND 37) AND a!=36)
         OR a=49
         OR a=38
         OR b=157
         OR a=4
         OR b=311
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR b=396
  ]])
    end, {
        -- <where7-2.311.1>
        4, 27, 34, 35, 36, 37, 38, 49, 50, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.311.1>
    })

test:do_test(
    "where7-2.311.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 34 AND 36) AND a!=35)
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR ((a BETWEEN 35 AND 37) AND a!=36)
         OR a=49
         OR a=38
         OR b=157
         OR a=4
         OR b=311
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR b=396
  ]])
    end, {
        -- <where7-2.311.2>
        4, 27, 34, 35, 36, 37, 38, 49, 50, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.311.2>
    })

test:do_test(
    "where7-2.312.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=82
         OR b=333
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
         OR b=99
         OR a=63
         OR a=35
         OR b=176
  ]])
    end, {
        -- <where7-2.312.1>
        9, 16, 22, 35, 48, 63, 74, 82, 100, "scan", 0, "sort", 0
        -- </where7-2.312.1>
    })

test:do_test(
    "where7-2.312.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=82
         OR b=333
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
         OR b=99
         OR a=63
         OR a=35
         OR b=176
  ]])
    end, {
        -- <where7-2.312.2>
        9, 16, 22, 35, 48, 63, 74, 82, 100, "scan", 0, "sort", 0
        -- </where7-2.312.2>
    })

test:do_test(
    "where7-2.313.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=90
         OR a=81
         OR ((a BETWEEN 51 AND 53) AND a!=52)
         OR f='mnopqrstu'
         OR b=927
         OR b=311
         OR a=34
         OR b=715
         OR f='rstuvwxyz'
  ]])
    end, {
        -- <where7-2.313.1>
        12, 17, 34, 38, 43, 51, 53, 64, 65, 69, 81, 90, 95, "scan", 0, "sort", 0
        -- </where7-2.313.1>
    })

test:do_test(
    "where7-2.313.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=90
         OR a=81
         OR ((a BETWEEN 51 AND 53) AND a!=52)
         OR f='mnopqrstu'
         OR b=927
         OR b=311
         OR a=34
         OR b=715
         OR f='rstuvwxyz'
  ]])
    end, {
        -- <where7-2.313.2>
        12, 17, 34, 38, 43, 51, 53, 64, 65, 69, 81, 90, 95, "scan", 0, "sort", 0
        -- </where7-2.313.2>
    })

test:do_test(
    "where7-2.314.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=484
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR f='lmnopqrst'
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
         OR b<0
         OR b=231
         OR a=14
  ]])
    end, {
        -- <where7-2.314.1>
        7, 10, 11, 12, 14, 21, 37, 39, 44, 63, 64, 89, "scan", 0, "sort", 0
        -- </where7-2.314.1>
    })

test:do_test(
    "where7-2.314.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=484
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR f='lmnopqrst'
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
         OR b<0
         OR b=231
         OR a=14
  ]])
    end, {
        -- <where7-2.314.2>
        7, 10, 11, 12, 14, 21, 37, 39, 44, 63, 64, 89, "scan", 0, "sort", 0
        -- </where7-2.314.2>
    })

test:do_test(
    "where7-2.315.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=905
         OR f='hijklmnop'
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR b=817
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.315.1>
        7, 11, 20, 22, 26, 33, 37, 45, 59, 63, 80, 85, 89, "scan", 0, "sort", 0
        -- </where7-2.315.1>
    })

test:do_test(
    "where7-2.315.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=905
         OR f='hijklmnop'
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR b=817
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.315.2>
        7, 11, 20, 22, 26, 33, 37, 45, 59, 63, 80, 85, 89, "scan", 0, "sort", 0
        -- </where7-2.315.2>
    })

test:do_test(
    "where7-2.316.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'hijkl*')
         OR b=311
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR a=48
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR c=32032
         OR f='opqrstuvw'
         OR b=300
         OR b=1001
         OR ((a BETWEEN 94 AND 96) AND a!=95)
  ]])
    end, {
        -- <where7-2.316.1>
        14, 40, 43, 47, 48, 61, 66, 85, 91, 92, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.316.1>
    })

test:do_test(
    "where7-2.316.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'hijkl*')
         OR b=311
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR a=48
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR c=32032
         OR f='opqrstuvw'
         OR b=300
         OR b=1001
         OR ((a BETWEEN 94 AND 96) AND a!=95)
  ]])
    end, {
        -- <where7-2.316.2>
        14, 40, 43, 47, 48, 61, 66, 85, 91, 92, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.316.2>
    })

test:do_test(
    "where7-2.317.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=95.0 AND d<96.0 AND d IS NOT NULL)
         OR b=1070
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR a=22
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR f='tuvwxyzab'
         OR a=72
         OR ((a BETWEEN 53 AND 55) AND a!=54)
  ]])
    end, {
        -- <where7-2.317.1>
        11, 19, 22, 45, 53, 55, 61, 71, 72, 95, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.317.1>
    })

test:do_test(
    "where7-2.317.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=95.0 AND d<96.0 AND d IS NOT NULL)
         OR b=1070
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR a=22
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR f='tuvwxyzab'
         OR a=72
         OR ((a BETWEEN 53 AND 55) AND a!=54)
  ]])
    end, {
        -- <where7-2.317.2>
        11, 19, 22, 45, 53, 55, 61, 71, 72, 95, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.317.2>
    })

test:do_test(
    "where7-2.318.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
         OR a=21
         OR b=1026
         OR ((a BETWEEN 34 AND 36) AND a!=35)
         OR b=473
  ]])
    end, {
        -- <where7-2.318.1>
        8, 16, 21, 34, 36, 43, "scan", 0, "sort", 0
        -- </where7-2.318.1>
    })

test:do_test(
    "where7-2.318.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
         OR a=21
         OR b=1026
         OR ((a BETWEEN 34 AND 36) AND a!=35)
         OR b=473
  ]])
    end, {
        -- <where7-2.318.2>
        8, 16, 21, 34, 36, 43, "scan", 0, "sort", 0
        -- </where7-2.318.2>
    })

test:do_test(
    "where7-2.319.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR a=100
         OR a=29
         OR c=15015
         OR a=87
         OR (g='gfedcba' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.319.1>
        29, 43, 44, 45, 71, 73, 87, 88, 100, "scan", 0, "sort", 0
        -- </where7-2.319.1>
    })

test:do_test(
    "where7-2.319.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR a=100
         OR a=29
         OR c=15015
         OR a=87
         OR (g='gfedcba' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.319.2>
        29, 43, 44, 45, 71, 73, 87, 88, 100, "scan", 0, "sort", 0
        -- </where7-2.319.2>
    })

test:do_test(
    "where7-2.320.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR b=542
         OR b=638
  ]])
    end, {
        -- <where7-2.320.1>
        1, 58, "scan", 0, "sort", 0
        -- </where7-2.320.1>
    })

test:do_test(
    "where7-2.320.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR b=542
         OR b=638
  ]])
    end, {
        -- <where7-2.320.2>
        1, 58, "scan", 0, "sort", 0
        -- </where7-2.320.2>
    })

test:do_test(
    "where7-2.321.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 5 AND 7) AND a!=6)
         OR b=1070
         OR a=91
         OR b=1015
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR ((a BETWEEN 91 AND 93) AND a!=92)
  ]])
    end, {
        -- <where7-2.321.1>
        5, 7, 12, 80, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.321.1>
    })

test:do_test(
    "where7-2.321.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 5 AND 7) AND a!=6)
         OR b=1070
         OR a=91
         OR b=1015
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR ((a BETWEEN 91 AND 93) AND a!=92)
  ]])
    end, {
        -- <where7-2.321.2>
        5, 7, 12, 80, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.321.2>
    })

test:do_test(
    "where7-2.322.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=7
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR b=1015
         OR b=839
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR b=410
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR a=71
  ]])
    end, {
        -- <where7-2.322.1>
        1, 2, 7, 28, 36, 54, 71, 80, "scan", 0, "sort", 0
        -- </where7-2.322.1>
    })

test:do_test(
    "where7-2.322.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=7
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR b=1015
         OR b=839
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR b=410
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR a=71
  ]])
    end, {
        -- <where7-2.322.2>
        1, 2, 7, 28, 36, 54, 71, 80, "scan", 0, "sort", 0
        -- </where7-2.322.2>
    })

test:do_test(
    "where7-2.323.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=880
         OR b=982
         OR a=52
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR a=24
         OR ((a BETWEEN 47 AND 49) AND a!=48)
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
  ]])
    end, {
        -- <where7-2.323.1>
        24, 47, 49, 50, 52, 60, 76, 80, "scan", 0, "sort", 0
        -- </where7-2.323.1>
    })

test:do_test(
    "where7-2.323.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=880
         OR b=982
         OR a=52
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR a=24
         OR ((a BETWEEN 47 AND 49) AND a!=48)
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
  ]])
    end, {
        -- <where7-2.323.2>
        24, 47, 49, 50, 52, 60, 76, 80, "scan", 0, "sort", 0
        -- </where7-2.323.2>
    })

test:do_test(
    "where7-2.324.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 67 AND 69) AND a!=68)
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
  ]])
    end, {
        -- <where7-2.324.1>
        5, 22, 31, 57, 67, 69, 83, "scan", 0, "sort", 0
        -- </where7-2.324.1>
    })

test:do_test(
    "where7-2.324.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 67 AND 69) AND a!=68)
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
  ]])
    end, {
        -- <where7-2.324.2>
        5, 22, 31, 57, 67, 69, 83, "scan", 0, "sort", 0
        -- </where7-2.324.2>
    })

test:do_test(
    "where7-2.325.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='abcdefghi'
         OR a=5
         OR b=124
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=432
         OR 1000000<b
         OR a=58
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=77
         OR b=605
  ]])
    end, {
        -- <where7-2.325.1>
        5, 7, 26, 45, 52, 55, 58, 69, 78, "scan", 0, "sort", 0
        -- </where7-2.325.1>
    })

test:do_test(
    "where7-2.325.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='abcdefghi'
         OR a=5
         OR b=124
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=432
         OR 1000000<b
         OR a=58
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=77
         OR b=605
  ]])
    end, {
        -- <where7-2.325.2>
        5, 7, 26, 45, 52, 55, 58, 69, 78, "scan", 0, "sort", 0
        -- </where7-2.325.2>
    })

test:do_test(
    "where7-2.326.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR b=583
         OR a=62
  ]])
    end, {
        -- <where7-2.326.1>
        53, 62, 89, "scan", 0, "sort", 0
        -- </where7-2.326.1>
    })

test:do_test(
    "where7-2.326.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR b=583
         OR a=62
  ]])
    end, {
        -- <where7-2.326.2>
        53, 62, 89, "scan", 0, "sort", 0
        -- </where7-2.326.2>
    })

test:do_test(
    "where7-2.327.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 84 AND 86) AND a!=85)
         OR f='pqrstuvwx'
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR b=278
         OR a=10
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR f='uvwxyzabc'
  ]])
    end, {
        -- <where7-2.327.1>
        5, 10, 15, 20, 28, 41, 46, 54, 63, 65, 67, 68, 72, 84, 86, 93, 98, "scan", 0, "sort", 0
        -- </where7-2.327.1>
    })

test:do_test(
    "where7-2.327.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 84 AND 86) AND a!=85)
         OR f='pqrstuvwx'
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR b=278
         OR a=10
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR f='uvwxyzabc'
  ]])
    end, {
        -- <where7-2.327.2>
        5, 10, 15, 20, 28, 41, 46, 54, 63, 65, 67, 68, 72, 84, 86, 93, 98, "scan", 0, "sort", 0
        -- </where7-2.327.2>
    })

test:do_test(
    "where7-2.328.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 57 AND 59) AND a!=58)
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR b=564
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR b=77
         OR (g='nmlkjih' AND f GLOB 'efghi*')
         OR b=968
         OR b=847
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.328.1>
        7, 14, 40, 56, 57, 58, 59, 66, 77, 85, 88, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.328.1>
    })

test:do_test(
    "where7-2.328.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 57 AND 59) AND a!=58)
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR b=564
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR b=77
         OR (g='nmlkjih' AND f GLOB 'efghi*')
         OR b=968
         OR b=847
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.328.2>
        7, 14, 40, 56, 57, 58, 59, 66, 77, 85, 88, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.328.2>
    })

test:do_test(
    "where7-2.329.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=539
         OR b=594
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR f='abcdefghi'
         OR a=6
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR b=762
  ]])
    end, {
        -- <where7-2.329.1>
        6, 17, 26, 49, 52, 54, 63, 65, 78, "scan", 0, "sort", 0
        -- </where7-2.329.1>
    })

test:do_test(
    "where7-2.329.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=539
         OR b=594
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR f='abcdefghi'
         OR a=6
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR b=762
  ]])
    end, {
        -- <where7-2.329.2>
        6, 17, 26, 49, 52, 54, 63, 65, 78, "scan", 0, "sort", 0
        -- </where7-2.329.2>
    })

test:do_test(
    "where7-2.330.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=869
         OR b=630
  ]])
    end, {
        -- <where7-2.330.1>
        79, "scan", 0, "sort", 0
        -- </where7-2.330.1>
    })

test:do_test(
    "where7-2.330.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=869
         OR b=630
  ]])
    end, {
        -- <where7-2.330.2>
        79, "scan", 0, "sort", 0
        -- </where7-2.330.2>
    })

test:do_test(
    "where7-2.331.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR b=693
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR b=968
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR b=132
         OR f='nopqrstuv'
         OR ((a BETWEEN 28 AND 30) AND a!=29)
  ]])
    end, {
        -- <where7-2.331.1>
        12, 13, 28, 30, 39, 63, 65, 72, 86, 88, 91, "scan", 0, "sort", 0
        -- </where7-2.331.1>
    })

test:do_test(
    "where7-2.331.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR b=693
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR b=968
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR b=132
         OR f='nopqrstuv'
         OR ((a BETWEEN 28 AND 30) AND a!=29)
  ]])
    end, {
        -- <where7-2.331.2>
        12, 13, 28, 30, 39, 63, 65, 72, 86, 88, 91, "scan", 0, "sort", 0
        -- </where7-2.331.2>
    })

test:do_test(
    "where7-2.332.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=32032
         OR b=814
         OR (d>=90.0 AND d<91.0 AND d IS NOT NULL)
         OR b=814
         OR a=78
         OR a=37
  ]])
    end, {
        -- <where7-2.332.1>
        37, 74, 78, 90, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.332.1>
    })

test:do_test(
    "where7-2.332.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=32032
         OR b=814
         OR (d>=90.0 AND d<91.0 AND d IS NOT NULL)
         OR b=814
         OR a=78
         OR a=37
  ]])
    end, {
        -- <where7-2.332.2>
        37, 74, 78, 90, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.332.2>
    })

test:do_test(
    "where7-2.333.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=190
         OR (g='mlkjihg' AND f GLOB 'hijkl*')
         OR b=924
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR b=759
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.333.1>
        1, 40, 59, 69, 84, "scan", 0, "sort", 0
        -- </where7-2.333.1>
    })

test:do_test(
    "where7-2.333.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=190
         OR (g='mlkjihg' AND f GLOB 'hijkl*')
         OR b=924
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR b=759
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.333.2>
        1, 40, 59, 69, 84, "scan", 0, "sort", 0
        -- </where7-2.333.2>
    })

test:do_test(
    "where7-2.334.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=97
         OR b=201
         OR b=597
         OR a=6
         OR f='cdefghijk'
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR b=300
         OR b=693
         OR b=333
         OR b=740
  ]])
    end, {
        -- <where7-2.334.1>
        2, 6, 28, 54, 63, 74, 76, 80, 97, "scan", 0, "sort", 0
        -- </where7-2.334.1>
    })

test:do_test(
    "where7-2.334.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=97
         OR b=201
         OR b=597
         OR a=6
         OR f='cdefghijk'
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR b=300
         OR b=693
         OR b=333
         OR b=740
  ]])
    end, {
        -- <where7-2.334.2>
        2, 6, 28, 54, 63, 74, 76, 80, 97, "scan", 0, "sort", 0
        -- </where7-2.334.2>
    })

test:do_test(
    "where7-2.335.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=26026
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR c=17017
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR (g='jihgfed' AND f GLOB 'zabcd*')
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.335.1>
        2, 4, 32, 43, 49, 50, 51, 60, 72, 74, 76, 77, 78, "scan", 0, "sort", 0
        -- </where7-2.335.1>
    })

test:do_test(
    "where7-2.335.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=26026
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR c=17017
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR (g='jihgfed' AND f GLOB 'zabcd*')
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.335.2>
        2, 4, 32, 43, 49, 50, 51, 60, 72, 74, 76, 77, 78, "scan", 0, "sort", 0
        -- </where7-2.335.2>
    })

test:do_test(
    "where7-2.336.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR ((a BETWEEN 13 AND 15) AND a!=14)
         OR b=638
         OR b=495
         OR a=44
         OR b=374
         OR a=22
         OR c=12012
  ]])
    end, {
        -- <where7-2.336.1>
        13, 15, 22, 34, 35, 36, 44, 45, 58, 70, "scan", 0, "sort", 0
        -- </where7-2.336.1>
    })

test:do_test(
    "where7-2.336.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR ((a BETWEEN 13 AND 15) AND a!=14)
         OR b=638
         OR b=495
         OR a=44
         OR b=374
         OR a=22
         OR c=12012
  ]])
    end, {
        -- <where7-2.336.2>
        13, 15, 22, 34, 35, 36, 44, 45, 58, 70, "scan", 0, "sort", 0
        -- </where7-2.336.2>
    })

test:do_test(
    "where7-2.337.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=8008
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
         OR (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR b=300
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR a=41
         OR (g='onmlkji' AND f GLOB 'xyzab*')
         OR b=135
         OR b=605
  ]])
    end, {
        -- <where7-2.337.1>
        1, 2, 22, 23, 24, 39, 41, 49, 55, 100, "scan", 0, "sort", 0
        -- </where7-2.337.1>
    })

test:do_test(
    "where7-2.337.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=8008
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
         OR (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR b=300
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR a=41
         OR (g='onmlkji' AND f GLOB 'xyzab*')
         OR b=135
         OR b=605
  ]])
    end, {
        -- <where7-2.337.2>
        1, 2, 22, 23, 24, 39, 41, 49, 55, 100, "scan", 0, "sort", 0
        -- </where7-2.337.2>
    })

test:do_test(
    "where7-2.338.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR b=762
         OR b=484
         OR b=190
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=1023
  ]])
    end, {
        -- <where7-2.338.1>
        4, 17, 30, 41, 43, 44, 56, 61, 69, 74, 82, 93, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.338.1>
    })

test:do_test(
    "where7-2.338.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR b=762
         OR b=484
         OR b=190
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=1023
  ]])
    end, {
        -- <where7-2.338.2>
        4, 17, 30, 41, 43, 44, 56, 61, 69, 74, 82, 93, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.338.2>
    })

test:do_test(
    "where7-2.339.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ihgfedc' AND f GLOB 'efghi*')
         OR a=34
         OR f='rstuvwxyz'
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR b=729
  ]])
    end, {
        -- <where7-2.339.1>
        10, 17, 34, 43, 69, 82, 95, "scan", 0, "sort", 0
        -- </where7-2.339.1>
    })

test:do_test(
    "where7-2.339.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ihgfedc' AND f GLOB 'efghi*')
         OR a=34
         OR f='rstuvwxyz'
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR b=729
  ]])
    end, {
        -- <where7-2.339.2>
        10, 17, 34, 43, 69, 82, 95, "scan", 0, "sort", 0
        -- </where7-2.339.2>
    })

test:do_test(
    "where7-2.340.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR b=1004
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR g IS NULL
  ]])
    end, {
        -- <where7-2.340.1>
        37, 41, "scan", 0, "sort", 0
        -- </where7-2.340.1>
    })

test:do_test(
    "where7-2.340.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR b=1004
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR g IS NULL
  ]])
    end, {
        -- <where7-2.340.2>
        37, 41, "scan", 0, "sort", 0
        -- </where7-2.340.2>
    })

test:do_test(
    "where7-2.341.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=73
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR ((a BETWEEN 51 AND 53) AND a!=52)
         OR a=9
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR a=44
         OR a=23
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR (g='rqponml' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.341.1>
        1, 9, 23, 36, 37, 38, 44, 51, 53, 55, 63, 73, 78, "scan", 0, "sort", 0
        -- </where7-2.341.1>
    })

test:do_test(
    "where7-2.341.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=73
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR ((a BETWEEN 51 AND 53) AND a!=52)
         OR a=9
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR a=44
         OR a=23
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR (g='rqponml' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.341.2>
        1, 9, 23, 36, 37, 38, 44, 51, 53, 55, 63, 73, 78, "scan", 0, "sort", 0
        -- </where7-2.341.2>
    })

test:do_test(
    "where7-2.342.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=487
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR a=11
         OR ((a BETWEEN 12 AND 14) AND a!=13)
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR a=13
         OR a=15
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR a=36
  ]])
    end, {
        -- <where7-2.342.1>
        11, 12, 13, 14, 15, 29, 36, 69, 71, 77, 78, 79, "scan", 0, "sort", 0
        -- </where7-2.342.1>
    })

test:do_test(
    "where7-2.342.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=487
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR a=11
         OR ((a BETWEEN 12 AND 14) AND a!=13)
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR a=13
         OR a=15
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR a=36
  ]])
    end, {
        -- <where7-2.342.2>
        11, 12, 13, 14, 15, 29, 36, 69, 71, 77, 78, 79, "scan", 0, "sort", 0
        -- </where7-2.342.2>
    })

test:do_test(
    "where7-2.343.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=938
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (d>=32.0 AND d<33.0 AND d IS NOT NULL)
         OR b=245
         OR (d>=35.0 AND d<36.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.343.1>
        32, 35, 54, 57, 59, "scan", 0, "sort", 0
        -- </where7-2.343.1>
    })

test:do_test(
    "where7-2.343.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=938
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (d>=32.0 AND d<33.0 AND d IS NOT NULL)
         OR b=245
         OR (d>=35.0 AND d<36.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.343.2>
        32, 35, 54, 57, 59, "scan", 0, "sort", 0
        -- </where7-2.343.2>
    })

test:do_test(
    "where7-2.344.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1078
         OR c=19019
         OR a=38
         OR a=59
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR c=25025
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.344.1>
        30, 32, 38, 51, 55, 56, 57, 59, 73, 74, 75, 76, 79, 95, 97, 98, "scan", 0, "sort", 0
        -- </where7-2.344.1>
    })

test:do_test(
    "where7-2.344.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1078
         OR c=19019
         OR a=38
         OR a=59
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR c=25025
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.344.2>
        30, 32, 38, 51, 55, 56, 57, 59, 73, 74, 75, 76, 79, 95, 97, 98, "scan", 0, "sort", 0
        -- </where7-2.344.2>
    })

test:do_test(
    "where7-2.345.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='cdefghijk'
         OR b=168
         OR b=561
         OR a=81
         OR a=87
  ]])
    end, {
        -- <where7-2.345.1>
        2, 28, 51, 54, 80, 81, 87, "scan", 0, "sort", 0
        -- </where7-2.345.1>
    })

test:do_test(
    "where7-2.345.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='cdefghijk'
         OR b=168
         OR b=561
         OR a=81
         OR a=87
  ]])
    end, {
        -- <where7-2.345.2>
        2, 28, 51, 54, 80, 81, 87, "scan", 0, "sort", 0
        -- </where7-2.345.2>
    })

test:do_test(
    "where7-2.346.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='gfedcba' AND f GLOB 'klmno*')
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR (g='rqponml' AND f GLOB 'hijkl*')
         OR a=48
         OR b=113
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=880
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.346.1>
        9, 11, 20, 22, 33, 48, 53, 73, 80, 85, 87, 88, "scan", 0, "sort", 0
        -- </where7-2.346.1>
    })

test:do_test(
    "where7-2.346.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='gfedcba' AND f GLOB 'klmno*')
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR (g='rqponml' AND f GLOB 'hijkl*')
         OR a=48
         OR b=113
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=880
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.346.2>
        9, 11, 20, 22, 33, 48, 53, 73, 80, 85, 87, 88, "scan", 0, "sort", 0
        -- </where7-2.346.2>
    })

test:do_test(
    "where7-2.347.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=517
         OR b=187
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR b=1092
         OR ((a BETWEEN 84 AND 86) AND a!=85)
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.347.1>
        6, 17, 47, 84, 86, "scan", 0, "sort", 0
        -- </where7-2.347.1>
    })

test:do_test(
    "where7-2.347.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=517
         OR b=187
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR b=1092
         OR ((a BETWEEN 84 AND 86) AND a!=85)
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.347.2>
        6, 17, 47, 84, 86, "scan", 0, "sort", 0
        -- </where7-2.347.2>
    })

test:do_test(
    "where7-2.348.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=982
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=234
         OR c=15015
         OR a=47
         OR f='qrstuvwxy'
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
         OR b=814
         OR b=440
         OR b=454
  ]])
    end, {
        -- <where7-2.348.1>
        16, 40, 42, 43, 44, 45, 47, 65, 68, 74, 94, "scan", 0, "sort", 0
        -- </where7-2.348.1>
    })

test:do_test(
    "where7-2.348.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=982
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=234
         OR c=15015
         OR a=47
         OR f='qrstuvwxy'
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
         OR b=814
         OR b=440
         OR b=454
  ]])
    end, {
        -- <where7-2.348.2>
        16, 40, 42, 43, 44, 45, 47, 65, 68, 74, 94, "scan", 0, "sort", 0
        -- </where7-2.348.2>
    })

test:do_test(
    "where7-2.349.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR c=7007
         OR b=429
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR b=231
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR c=22022
         OR f='bcdefghij'
  ]])
    end, {
        -- <where7-2.349.1>
        1, 19, 20, 21, 25, 26, 27, 39, 47, 53, 64, 65, 66, 79, "scan", 0, "sort", 0
        -- </where7-2.349.1>
    })

test:do_test(
    "where7-2.349.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR c=7007
         OR b=429
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR b=231
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR c=22022
         OR f='bcdefghij'
  ]])
    end, {
        -- <where7-2.349.2>
        1, 19, 20, 21, 25, 26, 27, 39, 47, 53, 64, 65, 66, 79, "scan", 0, "sort", 0
        -- </where7-2.349.2>
    })

test:do_test(
    "where7-2.350.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=17017
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR b=784
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR ((a BETWEEN 54 AND 56) AND a!=55)
         OR ((a BETWEEN 16 AND 18) AND a!=17)
         OR f='zabcdefgh'
  ]])
    end, {
        -- <where7-2.350.1>
        16, 18, 22, 24, 25, 49, 50, 51, 54, 56, 62, 77, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.350.1>
    })

test:do_test(
    "where7-2.350.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=17017
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR b=784
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR ((a BETWEEN 54 AND 56) AND a!=55)
         OR ((a BETWEEN 16 AND 18) AND a!=17)
         OR f='zabcdefgh'
  ]])
    end, {
        -- <where7-2.350.2>
        16, 18, 22, 24, 25, 49, 50, 51, 54, 56, 62, 77, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.350.2>
    })

test:do_test(
    "where7-2.351.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=344
         OR b=275
         OR c<=10
  ]])
    end, {
        -- <where7-2.351.1>
        25, "scan", 0, "sort", 0
        -- </where7-2.351.1>
    })

test:do_test(
    "where7-2.351.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=344
         OR b=275
         OR c<=10
  ]])
    end, {
        -- <where7-2.351.2>
        25, "scan", 0, "sort", 0
        -- </where7-2.351.2>
    })

test:do_test(
    "where7-2.352.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 44 AND 46) AND a!=45)
         OR a=76
         OR b=154
         OR a=30
         OR c=3003
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
         OR b=564
         OR b=55
         OR a=38
  ]])
    end, {
        -- <where7-2.352.1>
        5, 7, 8, 9, 14, 23, 30, 38, 44, 46, 49, 75, 76, 88, "scan", 0, "sort", 0
        -- </where7-2.352.1>
    })

test:do_test(
    "where7-2.352.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 44 AND 46) AND a!=45)
         OR a=76
         OR b=154
         OR a=30
         OR c=3003
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
         OR b=564
         OR b=55
         OR a=38
  ]])
    end, {
        -- <where7-2.352.2>
        5, 7, 8, 9, 14, 23, 30, 38, 44, 46, 49, 75, 76, 88, "scan", 0, "sort", 0
        -- </where7-2.352.2>
    })

test:do_test(
    "where7-2.353.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=52
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.353.1>
        52, 54, 66, 68, "scan", 0, "sort", 0
        -- </where7-2.353.1>
    })

test:do_test(
    "where7-2.353.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=52
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.353.2>
        52, 54, 66, 68, "scan", 0, "sort", 0
        -- </where7-2.353.2>
    })

test:do_test(
    "where7-2.354.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=792
         OR (g='wvutsrq' AND f GLOB 'jklmn*')
  ]])
    end, {
        -- <where7-2.354.1>
        9, 72, "scan", 0, "sort", 0
        -- </where7-2.354.1>
    })

test:do_test(
    "where7-2.354.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=792
         OR (g='wvutsrq' AND f GLOB 'jklmn*')
  ]])
    end, {
        -- <where7-2.354.2>
        9, 72, "scan", 0, "sort", 0
        -- </where7-2.354.2>
    })

test:do_test(
    "where7-2.355.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR c=21021
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR f='zabcdefgh'
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR b=781
         OR a=64
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.355.1>
        1, 11, 25, 51, 61, 62, 63, 64, 65, 71, 73, 77, "scan", 0, "sort", 0
        -- </where7-2.355.1>
    })

test:do_test(
    "where7-2.355.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR c=21021
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR f='zabcdefgh'
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR b=781
         OR a=64
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.355.2>
        1, 11, 25, 51, 61, 62, 63, 64, 65, 71, 73, 77, "scan", 0, "sort", 0
        -- </where7-2.355.2>
    })

test:do_test(
    "where7-2.356.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='lkjihgf' AND f GLOB 'pqrst*')
         OR (d>=90.0 AND d<91.0 AND d IS NOT NULL)
         OR a=34
         OR (g='rqponml' AND f GLOB 'ijklm*')
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR b=319
         OR b=330
         OR ((a BETWEEN 28 AND 30) AND a!=29)
  ]])
    end, {
        -- <where7-2.356.1>
        28, 29, 30, 34, 36, 67, 90, "scan", 0, "sort", 0
        -- </where7-2.356.1>
    })

test:do_test(
    "where7-2.356.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='lkjihgf' AND f GLOB 'pqrst*')
         OR (d>=90.0 AND d<91.0 AND d IS NOT NULL)
         OR a=34
         OR (g='rqponml' AND f GLOB 'ijklm*')
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR b=319
         OR b=330
         OR ((a BETWEEN 28 AND 30) AND a!=29)
  ]])
    end, {
        -- <where7-2.356.2>
        28, 29, 30, 34, 36, 67, 90, "scan", 0, "sort", 0
        -- </where7-2.356.2>
    })

test:do_test(
    "where7-2.357.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='qponmlk' AND f GLOB 'pqrst*')
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR a=45
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.357.1>
        15, 41, 45, 67, 81, 93, "scan", 0, "sort", 0
        -- </where7-2.357.1>
    })

test:do_test(
    "where7-2.357.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='qponmlk' AND f GLOB 'pqrst*')
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR a=45
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.357.2>
        15, 41, 45, 67, 81, 93, "scan", 0, "sort", 0
        -- </where7-2.357.2>
    })

test:do_test(
    "where7-2.358.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR b=165
         OR b=836
  ]])
    end, {
        -- <where7-2.358.1>
        15, 53, 54, 76, "scan", 0, "sort", 0
        -- </where7-2.358.1>
    })

test:do_test(
    "where7-2.358.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR b=165
         OR b=836
  ]])
    end, {
        -- <where7-2.358.2>
        15, 53, 54, 76, "scan", 0, "sort", 0
        -- </where7-2.358.2>
    })

test:do_test(
    "where7-2.359.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1034
         OR f='vwxyzabcd'
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 57 AND 59) AND a!=58)
  ]])
    end, {
        -- <where7-2.359.1>
        21, 47, 57, 59, 73, 91, 94, 99, "scan", 0, "sort", 0
        -- </where7-2.359.1>
    })

test:do_test(
    "where7-2.359.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1034
         OR f='vwxyzabcd'
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 57 AND 59) AND a!=58)
  ]])
    end, {
        -- <where7-2.359.2>
        21, 47, 57, 59, 73, 91, 94, 99, "scan", 0, "sort", 0
        -- </where7-2.359.2>
    })

test:do_test(
    "where7-2.360.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=440
         OR a=19
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR c=22022
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR a=92
         OR b=1026
         OR b=608
  ]])
    end, {
        -- <where7-2.360.1>
        19, 40, 47, 64, 65, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.360.1>
    })

test:do_test(
    "where7-2.360.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=440
         OR a=19
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR c=22022
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR a=92
         OR b=1026
         OR b=608
  ]])
    end, {
        -- <where7-2.360.2>
        19, 40, 47, 64, 65, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.360.2>
    })

test:do_test(
    "where7-2.361.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=37
         OR b=88
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR c=23023
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR a=56
         OR ((a BETWEEN 13 AND 15) AND a!=14)
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR f='ijklmnopq'
         OR ((a BETWEEN 85 AND 87) AND a!=86)
  ]])
    end, {
        -- <where7-2.361.1>
        8, 13, 15, 16, 22, 34, 37, 42, 56, 60, 67, 68, 69, 85, 86, 87, 94, "scan", 0, "sort", 0
        -- </where7-2.361.1>
    })

test:do_test(
    "where7-2.361.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=37
         OR b=88
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR c=23023
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR a=56
         OR ((a BETWEEN 13 AND 15) AND a!=14)
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR f='ijklmnopq'
         OR ((a BETWEEN 85 AND 87) AND a!=86)
  ]])
    end, {
        -- <where7-2.361.2>
        8, 13, 15, 16, 22, 34, 37, 42, 56, 60, 67, 68, 69, 85, 86, 87, 94, "scan", 0, "sort", 0
        -- </where7-2.361.2>
    })

test:do_test(
    "where7-2.362.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR a=74
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 42 AND 44) AND a!=43)
  ]])
    end, {
        -- <where7-2.362.1>
        20, 22, 24, 42, 44, 74, 97, "scan", 0, "sort", 0
        -- </where7-2.362.1>
    })

test:do_test(
    "where7-2.362.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR a=74
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 42 AND 44) AND a!=43)
  ]])
    end, {
        -- <where7-2.362.2>
        20, 22, 24, 42, 44, 74, 97, "scan", 0, "sort", 0
        -- </where7-2.362.2>
    })

test:do_test(
    "where7-2.363.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='uvwxyzabc'
         OR b=869
         OR ((a BETWEEN 49 AND 51) AND a!=50)
  ]])
    end, {
        -- <where7-2.363.1>
        20, 46, 49, 51, 72, 79, 98, "scan", 0, "sort", 0
        -- </where7-2.363.1>
    })

test:do_test(
    "where7-2.363.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='uvwxyzabc'
         OR b=869
         OR ((a BETWEEN 49 AND 51) AND a!=50)
  ]])
    end, {
        -- <where7-2.363.2>
        20, 46, 49, 51, 72, 79, 98, "scan", 0, "sort", 0
        -- </where7-2.363.2>
    })

test:do_test(
    "where7-2.364.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=682
         OR b=583
         OR b=685
         OR b=817
         OR ((a BETWEEN 34 AND 36) AND a!=35)
  ]])
    end, {
        -- <where7-2.364.1>
        34, 36, 53, 62, "scan", 0, "sort", 0
        -- </where7-2.364.1>
    })

test:do_test(
    "where7-2.364.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=682
         OR b=583
         OR b=685
         OR b=817
         OR ((a BETWEEN 34 AND 36) AND a!=35)
  ]])
    end, {
        -- <where7-2.364.2>
        34, 36, 53, 62, "scan", 0, "sort", 0
        -- </where7-2.364.2>
    })

test:do_test(
    "where7-2.365.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=583
         OR a=39
         OR b=627
         OR ((a BETWEEN 72 AND 74) AND a!=73)
  ]])
    end, {
        -- <where7-2.365.1>
        39, 53, 57, 72, 74, "scan", 0, "sort", 0
        -- </where7-2.365.1>
    })

test:do_test(
    "where7-2.365.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=583
         OR a=39
         OR b=627
         OR ((a BETWEEN 72 AND 74) AND a!=73)
  ]])
    end, {
        -- <where7-2.365.2>
        39, 53, 57, 72, 74, "scan", 0, "sort", 0
        -- </where7-2.365.2>
    })

test:do_test(
    "where7-2.366.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='jihgfed' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR b=212
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR a=20
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR b=627
  ]])
    end, {
        -- <where7-2.366.1>
        2, 4, 20, 24, 26, 53, 57, 68, 73, "scan", 0, "sort", 0
        -- </where7-2.366.1>
    })

test:do_test(
    "where7-2.366.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='jihgfed' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR b=212
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR a=20
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR b=627
  ]])
    end, {
        -- <where7-2.366.2>
        2, 4, 20, 24, 26, 53, 57, 68, 73, "scan", 0, "sort", 0
        -- </where7-2.366.2>
    })

test:do_test(
    "where7-2.367.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR b=157
         OR b=1026
  ]])
    end, {
        -- <where7-2.367.1>
        8, 34, 60, 77, 86, "scan", 0, "sort", 0
        -- </where7-2.367.1>
    })

test:do_test(
    "where7-2.367.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR b=157
         OR b=1026
  ]])
    end, {
        -- <where7-2.367.2>
        8, 34, 60, 77, 86, "scan", 0, "sort", 0
        -- </where7-2.367.2>
    })

test:do_test(
    "where7-2.368.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=553
         OR a=16
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (g='wvutsrq' AND f GLOB 'lmnop*')
         OR f='zabcdefgh'
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR (g='xwvutsr' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.368.1>
        5, 11, 16, 25, 31, 33, 51, 67, 77, 80, 82, "scan", 0, "sort", 0
        -- </where7-2.368.1>
    })

test:do_test(
    "where7-2.368.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=553
         OR a=16
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (g='wvutsrq' AND f GLOB 'lmnop*')
         OR f='zabcdefgh'
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR (g='xwvutsr' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.368.2>
        5, 11, 16, 25, 31, 33, 51, 67, 77, 80, 82, "scan", 0, "sort", 0
        -- </where7-2.368.2>
    })

test:do_test(
    "where7-2.369.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=858
         OR c=9009
         OR b=792
         OR b=88
         OR b=154
  ]])
    end, {
        -- <where7-2.369.1>
        8, 14, 25, 26, 27, 72, 78, "scan", 0, "sort", 0
        -- </where7-2.369.1>
    })

test:do_test(
    "where7-2.369.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=858
         OR c=9009
         OR b=792
         OR b=88
         OR b=154
  ]])
    end, {
        -- <where7-2.369.2>
        8, 14, 25, 26, 27, 72, 78, "scan", 0, "sort", 0
        -- </where7-2.369.2>
    })

test:do_test(
    "where7-2.370.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f IS NULL
         OR a=37
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR b=168
         OR b=22
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR b=506
  ]])
    end, {
        -- <where7-2.370.1>
        2, 21, 37, 46, 48, 55, 57, "scan", 0, "sort", 0
        -- </where7-2.370.1>
    })

test:do_test(
    "where7-2.370.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f IS NULL
         OR a=37
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR b=168
         OR b=22
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR b=506
  ]])
    end, {
        -- <where7-2.370.2>
        2, 21, 37, 46, 48, 55, 57, "scan", 0, "sort", 0
        -- </where7-2.370.2>
    })

test:do_test(
    "where7-2.371.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=29
         OR ((a BETWEEN 26 AND 28) AND a!=27)
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR b=209
         OR (f GLOB '?abcd*' AND f GLOB 'zabc*')
         OR b=146
  ]])
    end, {
        -- <where7-2.371.1>
        19, 25, 26, 28, 29, 42, 45, 51, 69, 71, 77, 97, "scan", 0, "sort", 0
        -- </where7-2.371.1>
    })

test:do_test(
    "where7-2.371.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=29
         OR ((a BETWEEN 26 AND 28) AND a!=27)
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR b=209
         OR (f GLOB '?abcd*' AND f GLOB 'zabc*')
         OR b=146
  ]])
    end, {
        -- <where7-2.371.2>
        19, 25, 26, 28, 29, 42, 45, 51, 69, 71, 77, 97, "scan", 0, "sort", 0
        -- </where7-2.371.2>
    })

test:do_test(
    "where7-2.372.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=63
         OR a=69
         OR b=333
         OR (d>=6.0 AND d<7.0 AND d IS NOT NULL)
         OR b=135
         OR b=25
         OR b=1037
         OR b=682
         OR c=27027
         OR a=46
  ]])
    end, {
        -- <where7-2.372.1>
        6, 46, 62, 63, 69, 79, 80, 81, "scan", 0, "sort", 0
        -- </where7-2.372.1>
    })

test:do_test(
    "where7-2.372.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=63
         OR a=69
         OR b=333
         OR (d>=6.0 AND d<7.0 AND d IS NOT NULL)
         OR b=135
         OR b=25
         OR b=1037
         OR b=682
         OR c=27027
         OR a=46
  ]])
    end, {
        -- <where7-2.372.2>
        6, 46, 62, 63, 69, 79, 80, 81, "scan", 0, "sort", 0
        -- </where7-2.372.2>
    })

test:do_test(
    "where7-2.373.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='edcbazy' AND f GLOB 'wxyza*')
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR b=113
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR ((a BETWEEN 59 AND 61) AND a!=60)
  ]])
    end, {
        -- <where7-2.373.1>
        40, 42, 52, 53, 55, 59, 61, 100, "scan", 0, "sort", 0
        -- </where7-2.373.1>
    })

test:do_test(
    "where7-2.373.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='edcbazy' AND f GLOB 'wxyza*')
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR b=113
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR ((a BETWEEN 59 AND 61) AND a!=60)
  ]])
    end, {
        -- <where7-2.373.2>
        40, 42, 52, 53, 55, 59, 61, 100, "scan", 0, "sort", 0
        -- </where7-2.373.2>
    })

test:do_test(
    "where7-2.374.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1026
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.374.1>
        48, "scan", 0, "sort", 0
        -- </where7-2.374.1>
    })

test:do_test(
    "where7-2.374.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1026
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.374.2>
        48, "scan", 0, "sort", 0
        -- </where7-2.374.2>
    })

test:do_test(
    "where7-2.375.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='rqponml' AND f GLOB 'ijklm*')
         OR a=99
         OR a=100
         OR b=429
         OR b=682
         OR b=495
         OR f='efghijklm'
         OR a=10
         OR f='mnopqrstu'
         OR b=946
         OR (d>=95.0 AND d<96.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.375.1>
        4, 10, 12, 30, 34, 38, 39, 45, 56, 62, 64, 82, 86, 90, 95, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.375.1>
    })

test:do_test(
    "where7-2.375.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='rqponml' AND f GLOB 'ijklm*')
         OR a=99
         OR a=100
         OR b=429
         OR b=682
         OR b=495
         OR f='efghijklm'
         OR a=10
         OR f='mnopqrstu'
         OR b=946
         OR (d>=95.0 AND d<96.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.375.2>
        4, 10, 12, 30, 34, 38, 39, 45, 56, 62, 64, 82, 86, 90, 95, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.375.2>
    })

test:do_test(
    "where7-2.376.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR c=23023
         OR b=462
         OR ((a BETWEEN 17 AND 19) AND a!=18)
  ]])
    end, {
        -- <where7-2.376.1>
        11, 17, 19, 42, 67, 68, 69, "scan", 0, "sort", 0
        -- </where7-2.376.1>
    })

test:do_test(
    "where7-2.376.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR c=23023
         OR b=462
         OR ((a BETWEEN 17 AND 19) AND a!=18)
  ]])
    end, {
        -- <where7-2.376.2>
        11, 17, 19, 42, 67, 68, 69, "scan", 0, "sort", 0
        -- </where7-2.376.2>
    })

test:do_test(
    "where7-2.377.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=539
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR c=6006
         OR a=18
         OR c=24024
         OR (g='wvutsrq' AND f GLOB 'jklmn*')
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR c=19019
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR ((a BETWEEN 44 AND 46) AND a!=45)
  ]])
    end, {
        -- <where7-2.377.1>
        9, 11, 16, 17, 18, 38, 43, 44, 46, 49, 55, 56, 57, 70, 71, 72, 87, "scan", 0, "sort", 0
        -- </where7-2.377.1>
    })

test:do_test(
    "where7-2.377.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=539
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR c=6006
         OR a=18
         OR c=24024
         OR (g='wvutsrq' AND f GLOB 'jklmn*')
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR c=19019
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR ((a BETWEEN 44 AND 46) AND a!=45)
  ]])
    end, {
        -- <where7-2.377.2>
        9, 11, 16, 17, 18, 38, 43, 44, 46, 49, 55, 56, 57, 70, 71, 72, 87, "scan", 0, "sort", 0
        -- </where7-2.377.2>
    })

test:do_test(
    "where7-2.378.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR a=20
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR b=121
         OR a=10
         OR b=792
  ]])
    end, {
        -- <where7-2.378.1>
        10, 11, 15, 20, 72, 94, "scan", 0, "sort", 0
        -- </where7-2.378.1>
    })

test:do_test(
    "where7-2.378.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR a=20
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR b=121
         OR a=10
         OR b=792
  ]])
    end, {
        -- <where7-2.378.2>
        10, 11, 15, 20, 72, 94, "scan", 0, "sort", 0
        -- </where7-2.378.2>
    })

test:do_test(
    "where7-2.379.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=99
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
  ]])
    end, {
        -- <where7-2.379.1>
        9, 14, 40, 66, 85, 87, 92, "scan", 0, "sort", 0
        -- </where7-2.379.1>
    })

test:do_test(
    "where7-2.379.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=99
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
  ]])
    end, {
        -- <where7-2.379.2>
        9, 14, 40, 66, 85, 87, 92, "scan", 0, "sort", 0
        -- </where7-2.379.2>
    })

test:do_test(
    "where7-2.380.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR ((a BETWEEN 79 AND 81) AND a!=80)
         OR b=715
         OR ((a BETWEEN 23 AND 25) AND a!=24)
  ]])
    end, {
        -- <where7-2.380.1>
        6, 23, 25, 32, 58, 65, 79, 81, 84, "scan", 0, "sort", 0
        -- </where7-2.380.1>
    })

test:do_test(
    "where7-2.380.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR ((a BETWEEN 79 AND 81) AND a!=80)
         OR b=715
         OR ((a BETWEEN 23 AND 25) AND a!=24)
  ]])
    end, {
        -- <where7-2.380.2>
        6, 23, 25, 32, 58, 65, 79, 81, 84, "scan", 0, "sort", 0
        -- </where7-2.380.2>
    })

test:do_test(
    "where7-2.381.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
         OR a=46
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.381.1>
        28, 46, 97, "scan", 0, "sort", 0
        -- </where7-2.381.1>
    })

test:do_test(
    "where7-2.381.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
         OR a=46
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.381.2>
        28, 46, 97, "scan", 0, "sort", 0
        -- </where7-2.381.2>
    })

test:do_test(
    "where7-2.382.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ihgfedc' AND f GLOB 'defgh*')
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR b=1056
         OR b=146
  ]])
    end, {
        -- <where7-2.382.1>
        18, 81, 96, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.382.1>
    })

test:do_test(
    "where7-2.382.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ihgfedc' AND f GLOB 'defgh*')
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR b=1056
         OR b=146
  ]])
    end, {
        -- <where7-2.382.2>
        18, 81, 96, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.382.2>
    })

test:do_test(
    "where7-2.383.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=15
         OR b=388
         OR ((a BETWEEN 82 AND 84) AND a!=83)
         OR a=36
         OR b=737
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR a=75
  ]])
    end, {
        -- <where7-2.383.1>
        15, 21, 23, 36, 67, 75, 82, 84, 89, "scan", 0, "sort", 0
        -- </where7-2.383.1>
    })

test:do_test(
    "where7-2.383.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=15
         OR b=388
         OR ((a BETWEEN 82 AND 84) AND a!=83)
         OR a=36
         OR b=737
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR a=75
  ]])
    end, {
        -- <where7-2.383.2>
        15, 21, 23, 36, 67, 75, 82, 84, 89, "scan", 0, "sort", 0
        -- </where7-2.383.2>
    })

test:do_test(
    "where7-2.384.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=9009
         OR a=34
         OR (d>=95.0 AND d<96.0 AND d IS NOT NULL)
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=715
         OR b=619
         OR ((a BETWEEN 98 AND 100) AND a!=99)
  ]])
    end, {
        -- <where7-2.384.1>
        16, 25, 26, 27, 34, 65, 95, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.384.1>
    })

test:do_test(
    "where7-2.384.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=9009
         OR a=34
         OR (d>=95.0 AND d<96.0 AND d IS NOT NULL)
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=715
         OR b=619
         OR ((a BETWEEN 98 AND 100) AND a!=99)
  ]])
    end, {
        -- <where7-2.384.2>
        16, 25, 26, 27, 34, 65, 95, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.384.2>
    })

test:do_test(
    "where7-2.385.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR ((a BETWEEN 11 AND 13) AND a!=12)
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR b=242
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR b=300
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR ((a BETWEEN 93 AND 95) AND a!=94)
  ]])
    end, {
        -- <where7-2.385.1>
        1, 11, 13, 21, 22, 24, 26, 27, 32, 34, 39, 41, 53, 61, 74, 76, 79, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.385.1>
    })

test:do_test(
    "where7-2.385.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR ((a BETWEEN 11 AND 13) AND a!=12)
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR b=242
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR b=300
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR ((a BETWEEN 93 AND 95) AND a!=94)
  ]])
    end, {
        -- <where7-2.385.2>
        1, 11, 13, 21, 22, 24, 26, 27, 32, 34, 39, 41, 53, 61, 74, 76, 79, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.385.2>
    })

test:do_test(
    "where7-2.386.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=85
         OR (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR b=212
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR b=36
         OR b=231
         OR b=1048
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR c=19019
  ]])
    end, {
        -- <where7-2.386.1>
        21, 25, 27, 33, 43, 55, 56, 57, 69, 71, 85, 92, "scan", 0, "sort", 0
        -- </where7-2.386.1>
    })

test:do_test(
    "where7-2.386.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=85
         OR (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR b=212
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR b=36
         OR b=231
         OR b=1048
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR c=19019
  ]])
    end, {
        -- <where7-2.386.2>
        21, 25, 27, 33, 43, 55, 56, 57, 69, 71, 85, 92, "scan", 0, "sort", 0
        -- </where7-2.386.2>
    })

test:do_test(
    "where7-2.387.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 28 AND 30) AND a!=29)
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR b=1059
         OR b=630
  ]])
    end, {
        -- <where7-2.387.1>
        8, 28, 30, "scan", 0, "sort", 0
        -- </where7-2.387.1>
    })

test:do_test(
    "where7-2.387.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 28 AND 30) AND a!=29)
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR b=1059
         OR b=630
  ]])
    end, {
        -- <where7-2.387.2>
        8, 28, 30, "scan", 0, "sort", 0
        -- </where7-2.387.2>
    })

test:do_test(
    "where7-2.388.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='ghijklmno'
         OR f='nopqrstuv'
         OR b=297
  ]])
    end, {
        -- <where7-2.388.1>
        6, 13, 27, 32, 39, 58, 65, 84, 91, "scan", 0, "sort", 0
        -- </where7-2.388.1>
    })

test:do_test(
    "where7-2.388.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='ghijklmno'
         OR f='nopqrstuv'
         OR b=297
  ]])
    end, {
        -- <where7-2.388.2>
        6, 13, 27, 32, 39, 58, 65, 84, 91, "scan", 0, "sort", 0
        -- </where7-2.388.2>
    })

test:do_test(
    "where7-2.389.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1001
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR a=58
         OR b=333
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR b=572
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
  ]])
    end, {
        -- <where7-2.389.1>
        7, 15, 33, 43, 49, 50, 52, 58, 59, 68, 70, 85, 87, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.389.1>
    })

test:do_test(
    "where7-2.389.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1001
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR a=58
         OR b=333
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR b=572
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
  ]])
    end, {
        -- <where7-2.389.2>
        7, 15, 33, 43, 49, 50, 52, 58, 59, 68, 70, 85, 87, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.389.2>
    })

test:do_test(
    "where7-2.390.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1034
         OR f='lmnopqrst'
         OR (g='qponmlk' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.390.1>
        11, 37, 38, 63, 89, 94, "scan", 0, "sort", 0
        -- </where7-2.390.1>
    })

test:do_test(
    "where7-2.390.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1034
         OR f='lmnopqrst'
         OR (g='qponmlk' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.390.2>
        11, 37, 38, 63, 89, 94, "scan", 0, "sort", 0
        -- </where7-2.390.2>
    })

test:do_test(
    "where7-2.391.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=15015
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'hijkl*')
         OR b=58
         OR b=674
         OR b=979
  ]])
    end, {
        -- <where7-2.391.1>
        43, 44, 45, 59, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.391.1>
    })

test:do_test(
    "where7-2.391.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=15015
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'hijkl*')
         OR b=58
         OR b=674
         OR b=979
  ]])
    end, {
        -- <where7-2.391.2>
        43, 44, 45, 59, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.391.2>
    })

test:do_test(
    "where7-2.392.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 60 AND 62) AND a!=61)
         OR b=660
         OR b=341
  ]])
    end, {
        -- <where7-2.392.1>
        31, 60, 62, "scan", 0, "sort", 0
        -- </where7-2.392.1>
    })

test:do_test(
    "where7-2.392.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 60 AND 62) AND a!=61)
         OR b=660
         OR b=341
  ]])
    end, {
        -- <where7-2.392.2>
        31, 60, 62, "scan", 0, "sort", 0
        -- </where7-2.392.2>
    })

test:do_test(
    "where7-2.393.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=528
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR b=630
         OR a=19
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR f='wxyzabcde'
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR b=377
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR a=77
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.393.1>
        8, 19, 22, 43, 44, 48, 64, 74, 77, 100, "scan", 0, "sort", 0
        -- </where7-2.393.1>
    })

test:do_test(
    "where7-2.393.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=528
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR b=630
         OR a=19
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR f='wxyzabcde'
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR b=377
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR a=77
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.393.2>
        8, 19, 22, 43, 44, 48, 64, 74, 77, 100, "scan", 0, "sort", 0
        -- </where7-2.393.2>
    })

test:do_test(
    "where7-2.394.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=506
         OR a=70
  ]])
    end, {
        -- <where7-2.394.1>
        46, 70, "scan", 0, "sort", 0
        -- </where7-2.394.1>
    })

test:do_test(
    "where7-2.394.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=506
         OR a=70
  ]])
    end, {
        -- <where7-2.394.2>
        46, 70, "scan", 0, "sort", 0
        -- </where7-2.394.2>
    })

test:do_test(
    "where7-2.395.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=64
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'cdefg*')
         OR c=14014
         OR b=586
         OR c=27027
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.395.1>
        26, 28, 40, 41, 42, 52, 57, 64, 74, 78, 79, 80, 81, 86, "scan", 0, "sort", 0
        -- </where7-2.395.1>
    })

test:do_test(
    "where7-2.395.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=64
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'cdefg*')
         OR c=14014
         OR b=586
         OR c=27027
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.395.2>
        26, 28, 40, 41, 42, 52, 57, 64, 74, 78, 79, 80, 81, 86, "scan", 0, "sort", 0
        -- </where7-2.395.2>
    })

test:do_test(
    "where7-2.396.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=46
         OR b=297
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR b=275
         OR b=91
         OR b=1015
         OR c=12012
         OR a=23
         OR b=278
  ]])
    end, {
        -- <where7-2.396.1>
        23, 25, 27, 34, 35, 36, 46, 57, 59, 75, "scan", 0, "sort", 0
        -- </where7-2.396.1>
    })

test:do_test(
    "where7-2.396.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=46
         OR b=297
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR b=275
         OR b=91
         OR b=1015
         OR c=12012
         OR a=23
         OR b=278
  ]])
    end, {
        -- <where7-2.396.2>
        23, 25, 27, 34, 35, 36, 46, 57, 59, 75, "scan", 0, "sort", 0
        -- </where7-2.396.2>
    })

test:do_test(
    "where7-2.397.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR a=23
         OR b=737
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR ((a BETWEEN 68 AND 70) AND a!=69)
  ]])
    end, {
        -- <where7-2.397.1>
        18, 20, 23, 25, 27, 61, 67, 68, 69, 70, 71, 98, "scan", 0, "sort", 0
        -- </where7-2.397.1>
    })

test:do_test(
    "where7-2.397.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR a=23
         OR b=737
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR ((a BETWEEN 68 AND 70) AND a!=69)
  ]])
    end, {
        -- <where7-2.397.2>
        18, 20, 23, 25, 27, 61, 67, 68, 69, 70, 71, 98, "scan", 0, "sort", 0
        -- </where7-2.397.2>
    })

test:do_test(
    "where7-2.398.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=814
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR b=377
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.398.1>
        71, 74, 79, "scan", 0, "sort", 0
        -- </where7-2.398.1>
    })

test:do_test(
    "where7-2.398.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=814
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR b=377
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.398.2>
        71, 74, 79, "scan", 0, "sort", 0
        -- </where7-2.398.2>
    })

test:do_test(
    "where7-2.399.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=18
         OR b=1059
         OR (f GLOB '?abcd*' AND f GLOB 'zabc*')
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR b=795
  ]])
    end, {
        -- <where7-2.399.1>
        9, 18, 25, 46, 51, 53, 77, "scan", 0, "sort", 0
        -- </where7-2.399.1>
    })

test:do_test(
    "where7-2.399.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=18
         OR b=1059
         OR (f GLOB '?abcd*' AND f GLOB 'zabc*')
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR b=795
  ]])
    end, {
        -- <where7-2.399.2>
        9, 18, 25, 46, 51, 53, 77, "scan", 0, "sort", 0
        -- </where7-2.399.2>
    })

test:do_test(
    "where7-2.400.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR a=93
         OR a=11
         OR f='nopqrstuv'
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR a=17
         OR b=366
  ]])
    end, {
        -- <where7-2.400.1>
        11, 13, 17, 22, 24, 27, 37, 39, 63, 65, 89, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.400.1>
    })

test:do_test(
    "where7-2.400.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR a=93
         OR a=11
         OR f='nopqrstuv'
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR a=17
         OR b=366
  ]])
    end, {
        -- <where7-2.400.2>
        11, 13, 17, 22, 24, 27, 37, 39, 63, 65, 89, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.400.2>
    })

test:do_test(
    "where7-2.401.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=685
         OR a=33
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR b=715
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR a=6
         OR ((a BETWEEN 59 AND 61) AND a!=60)
  ]])
    end, {
        -- <where7-2.401.1>
        6, 16, 33, 37, 39, 40, 41, 42, 59, 61, 65, 80, 82, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.401.1>
    })

test:do_test(
    "where7-2.401.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=685
         OR a=33
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR b=715
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR a=6
         OR ((a BETWEEN 59 AND 61) AND a!=60)
  ]])
    end, {
        -- <where7-2.401.2>
        6, 16, 33, 37, 39, 40, 41, 42, 59, 61, 65, 80, 82, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.401.2>
    })

test:do_test(
    "where7-2.402.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=89
         OR b=1037
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
  ]])
    end, {
        -- <where7-2.402.1>
        60, 89, "scan", 0, "sort", 0
        -- </where7-2.402.1>
    })

test:do_test(
    "where7-2.402.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=89
         OR b=1037
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
  ]])
    end, {
        -- <where7-2.402.2>
        60, 89, "scan", 0, "sort", 0
        -- </where7-2.402.2>
    })

test:do_test(
    "where7-2.403.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR b=663
         OR b=531
         OR b=146
         OR b=102
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR a=26
  ]])
    end, {
        -- <where7-2.403.1>
        26, 28, 44, 46, 87, 89, 97, "scan", 0, "sort", 0
        -- </where7-2.403.1>
    })

test:do_test(
    "where7-2.403.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR b=663
         OR b=531
         OR b=146
         OR b=102
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR a=26
  ]])
    end, {
        -- <where7-2.403.2>
        26, 28, 44, 46, 87, 89, 97, "scan", 0, "sort", 0
        -- </where7-2.403.2>
    })

test:do_test(
    "where7-2.404.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'stuvw*')
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR (g='lkjihgf' AND f GLOB 'mnopq*')
         OR b=726
         OR ((a BETWEEN 73 AND 75) AND a!=74)
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR c=2002
         OR c=15015
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR b=201
  ]])
    end, {
        -- <where7-2.404.1>
        4, 5, 6, 12, 35, 43, 44, 45, 64, 66, 70, 73, 75, "scan", 0, "sort", 0
        -- </where7-2.404.1>
    })

test:do_test(
    "where7-2.404.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'stuvw*')
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR (g='lkjihgf' AND f GLOB 'mnopq*')
         OR b=726
         OR ((a BETWEEN 73 AND 75) AND a!=74)
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR c=2002
         OR c=15015
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR b=201
  ]])
    end, {
        -- <where7-2.404.2>
        4, 5, 6, 12, 35, 43, 44, 45, 64, 66, 70, 73, 75, "scan", 0, "sort", 0
        -- </where7-2.404.2>
    })

test:do_test(
    "where7-2.405.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR b=924
         OR f='lmnopqrst'
         OR b=1048
  ]])
    end, {
        -- <where7-2.405.1>
        11, 37, 63, 72, 84, 89, "scan", 0, "sort", 0
        -- </where7-2.405.1>
    })

test:do_test(
    "where7-2.405.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR b=924
         OR f='lmnopqrst'
         OR b=1048
  ]])
    end, {
        -- <where7-2.405.2>
        11, 37, 63, 72, 84, 89, "scan", 0, "sort", 0
        -- </where7-2.405.2>
    })

test:do_test(
    "where7-2.406.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR b=198
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
         OR ((a BETWEEN 12 AND 14) AND a!=13)
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=286
         OR ((a BETWEEN 65 AND 67) AND a!=66)
  ]])
    end, {
        -- <where7-2.406.1>
        12, 14, 18, 20, 22, 26, 58, 63, 65, 67, "scan", 0, "sort", 0
        -- </where7-2.406.1>
    })

test:do_test(
    "where7-2.406.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR b=198
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
         OR ((a BETWEEN 12 AND 14) AND a!=13)
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=286
         OR ((a BETWEEN 65 AND 67) AND a!=66)
  ]])
    end, {
        -- <where7-2.406.2>
        12, 14, 18, 20, 22, 26, 58, 63, 65, 67, "scan", 0, "sort", 0
        -- </where7-2.406.2>
    })

test:do_test(
    "where7-2.407.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=242
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR f='bcdefghij'
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR a=38
         OR b=187
  ]])
    end, {
        -- <where7-2.407.1>
        1, 17, 19, 22, 27, 38, 53, 57, 59, 79, 88, 99, "scan", 0, "sort", 0
        -- </where7-2.407.1>
    })

test:do_test(
    "where7-2.407.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=242
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR f='bcdefghij'
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR a=38
         OR b=187
  ]])
    end, {
        -- <where7-2.407.2>
        1, 17, 19, 22, 27, 38, 53, 57, 59, 79, 88, 99, "scan", 0, "sort", 0
        -- </where7-2.407.2>
    })

test:do_test(
    "where7-2.408.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR b=630
         OR a=55
         OR c=26026
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.408.1>
        10, 23, 55, 68, 76, 77, 78, "scan", 0, "sort", 0
        -- </where7-2.408.1>
    })

test:do_test(
    "where7-2.408.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR b=630
         OR a=55
         OR c=26026
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.408.2>
        10, 23, 55, 68, 76, 77, 78, "scan", 0, "sort", 0
        -- </where7-2.408.2>
    })

test:do_test(
    "where7-2.409.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='uvwxyzabc'
         OR f='xyzabcdef'
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR ((a BETWEEN 51 AND 53) AND a!=52)
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR b=69
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
  ]])
    end, {
        -- <where7-2.409.1>
        8, 20, 23, 31, 34, 46, 49, 51, 53, 60, 70, 72, 75, 79, 86, 98, "scan", 0, "sort", 0
        -- </where7-2.409.1>
    })

test:do_test(
    "where7-2.409.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='uvwxyzabc'
         OR f='xyzabcdef'
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR ((a BETWEEN 51 AND 53) AND a!=52)
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR b=69
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
  ]])
    end, {
        -- <where7-2.409.2>
        8, 20, 23, 31, 34, 46, 49, 51, 53, 60, 70, 72, 75, 79, 86, 98, "scan", 0, "sort", 0
        -- </where7-2.409.2>
    })

test:do_test(
    "where7-2.410.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1026
         OR b=454
         OR ((a BETWEEN 92 AND 94) AND a!=93)
         OR b=179
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.410.1>
        16, 26, 42, 52, 68, 78, 92, 94, "scan", 0, "sort", 0
        -- </where7-2.410.1>
    })

test:do_test(
    "where7-2.410.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1026
         OR b=454
         OR ((a BETWEEN 92 AND 94) AND a!=93)
         OR b=179
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.410.2>
        16, 26, 42, 52, 68, 78, 92, 94, "scan", 0, "sort", 0
        -- </where7-2.410.2>
    })

test:do_test(
    "where7-2.411.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 6 AND 8) AND a!=7)
         OR b=619
         OR a=20
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
         OR b=946
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR a=64
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR b=1001
         OR b=858
  ]])
    end, {
        -- <where7-2.411.1>
        6, 8, 13, 17, 19, 20, 61, 64, 78, 86, 91, "scan", 0, "sort", 0
        -- </where7-2.411.1>
    })

test:do_test(
    "where7-2.411.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 6 AND 8) AND a!=7)
         OR b=619
         OR a=20
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
         OR b=946
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR a=64
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR b=1001
         OR b=858
  ]])
    end, {
        -- <where7-2.411.2>
        6, 8, 13, 17, 19, 20, 61, 64, 78, 86, 91, "scan", 0, "sort", 0
        -- </where7-2.411.2>
    })

test:do_test(
    "where7-2.412.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=902
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR a=86
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.412.1>
        27, 82, 86, 97, "scan", 0, "sort", 0
        -- </where7-2.412.1>
    })

test:do_test(
    "where7-2.412.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=902
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR a=86
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.412.2>
        27, 82, 86, 97, "scan", 0, "sort", 0
        -- </where7-2.412.2>
    })

test:do_test(
    "where7-2.413.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR a=32
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR c=32032
  ]])
    end, {
        -- <where7-2.413.1>
        4, 32, 38, 56, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.413.1>
    })

test:do_test(
    "where7-2.413.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR a=32
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR c=32032
  ]])
    end, {
        -- <where7-2.413.2>
        4, 32, 38, 56, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.413.2>
    })

test:do_test(
    "where7-2.414.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=168
         OR c=2002
         OR b=77
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.414.1>
        4, 5, 6, 7, 16, 27, 42, 68, 94, "scan", 0, "sort", 0
        -- </where7-2.414.1>
    })

test:do_test(
    "where7-2.414.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=168
         OR c=2002
         OR b=77
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.414.2>
        4, 5, 6, 7, 16, 27, 42, 68, 94, "scan", 0, "sort", 0
        -- </where7-2.414.2>
    })

test:do_test(
    "where7-2.415.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='abcdefghi'
         OR b=506
  ]])
    end, {
        -- <where7-2.415.1>
        26, 46, 52, 78, "scan", 0, "sort", 0
        -- </where7-2.415.1>
    })

test:do_test(
    "where7-2.415.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='abcdefghi'
         OR b=506
  ]])
    end, {
        -- <where7-2.415.2>
        26, 46, 52, 78, "scan", 0, "sort", 0
        -- </where7-2.415.2>
    })

test:do_test(
    "where7-2.416.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=264
         OR c=34034
         OR a=96
  ]])
    end, {
        -- <where7-2.416.1>
        24, 96, 100, "scan", 0, "sort", 0
        -- </where7-2.416.1>
    })

test:do_test(
    "where7-2.416.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=264
         OR c=34034
         OR a=96
  ]])
    end, {
        -- <where7-2.416.2>
        24, 96, 100, "scan", 0, "sort", 0
        -- </where7-2.416.2>
    })

test:do_test(
    "where7-2.417.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=32.0 AND d<33.0 AND d IS NOT NULL)
         OR a=27
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
  ]])
    end, {
        -- <where7-2.417.1>
        19, 27, 32, 55, 57, "scan", 0, "sort", 0
        -- </where7-2.417.1>
    })

test:do_test(
    "where7-2.417.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=32.0 AND d<33.0 AND d IS NOT NULL)
         OR a=27
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
  ]])
    end, {
        -- <where7-2.417.2>
        19, 27, 32, 55, 57, "scan", 0, "sort", 0
        -- </where7-2.417.2>
    })

test:do_test(
    "where7-2.418.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR b=77
  ]])
    end, {
        -- <where7-2.418.1>
        7, 74, "scan", 0, "sort", 0
        -- </where7-2.418.1>
    })

test:do_test(
    "where7-2.418.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR b=77
  ]])
    end, {
        -- <where7-2.418.2>
        7, 74, "scan", 0, "sort", 0
        -- </where7-2.418.2>
    })

test:do_test(
    "where7-2.419.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=27027
         OR f='vwxyzabcd'
         OR b=1048
         OR a=96
         OR a=99
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR b=561
         OR b=352
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR a=95
  ]])
    end, {
        -- <where7-2.419.1>
        18, 21, 32, 37, 47, 51, 56, 58, 73, 79, 80, 81, 95, 96, 99, "scan", 0, "sort", 0
        -- </where7-2.419.1>
    })

test:do_test(
    "where7-2.419.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=27027
         OR f='vwxyzabcd'
         OR b=1048
         OR a=96
         OR a=99
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR b=561
         OR b=352
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR a=95
  ]])
    end, {
        -- <where7-2.419.2>
        18, 21, 32, 37, 47, 51, 56, 58, 73, 79, 80, 81, 95, 96, 99, "scan", 0, "sort", 0
        -- </where7-2.419.2>
    })

test:do_test(
    "where7-2.420.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=275
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR f='ghijklmno'
         OR b=619
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=476
         OR a=83
         OR ((a BETWEEN 47 AND 49) AND a!=48)
  ]])
    end, {
        -- <where7-2.420.1>
        6, 10, 12, 25, 32, 47, 49, 58, 83, 84, 91, 93, 99, "scan", 0, "sort", 0
        -- </where7-2.420.1>
    })

test:do_test(
    "where7-2.420.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=275
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR f='ghijklmno'
         OR b=619
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=476
         OR a=83
         OR ((a BETWEEN 47 AND 49) AND a!=48)
  ]])
    end, {
        -- <where7-2.420.2>
        6, 10, 12, 25, 32, 47, 49, 58, 83, 84, 91, 93, 99, "scan", 0, "sort", 0
        -- </where7-2.420.2>
    })

test:do_test(
    "where7-2.421.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=542
         OR a=17
         OR f='jklmnopqr'
         OR ((a BETWEEN 5 AND 7) AND a!=6)
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
         OR a=23
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.421.1>
        5, 7, 9, 17, 23, 25, 35, 39, 61, 87, "scan", 0, "sort", 0
        -- </where7-2.421.1>
    })

test:do_test(
    "where7-2.421.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=542
         OR a=17
         OR f='jklmnopqr'
         OR ((a BETWEEN 5 AND 7) AND a!=6)
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
         OR a=23
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.421.2>
        5, 7, 9, 17, 23, 25, 35, 39, 61, 87, "scan", 0, "sort", 0
        -- </where7-2.421.2>
    })

test:do_test(
    "where7-2.422.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR b=363
         OR b=454
  ]])
    end, {
        -- <where7-2.422.1>
        33, 74, "scan", 0, "sort", 0
        -- </where7-2.422.1>
    })

test:do_test(
    "where7-2.422.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR b=363
         OR b=454
  ]])
    end, {
        -- <where7-2.422.2>
        33, 74, "scan", 0, "sort", 0
        -- </where7-2.422.2>
    })

test:do_test(
    "where7-2.423.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1059
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR b=47
         OR b=660
         OR ((a BETWEEN 34 AND 36) AND a!=35)
         OR a=84
  ]])
    end, {
        -- <where7-2.423.1>
        34, 35, 36, 60, 76, 84, "scan", 0, "sort", 0
        -- </where7-2.423.1>
    })

test:do_test(
    "where7-2.423.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1059
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR b=47
         OR b=660
         OR ((a BETWEEN 34 AND 36) AND a!=35)
         OR a=84
  ]])
    end, {
        -- <where7-2.423.2>
        34, 35, 36, 60, 76, 84, "scan", 0, "sort", 0
        -- </where7-2.423.2>
    })

test:do_test(
    "where7-2.424.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='ghijklmno'
         OR b=1012
  ]])
    end, {
        -- <where7-2.424.1>
        6, 32, 58, 84, 92, "scan", 0, "sort", 0
        -- </where7-2.424.1>
    })

test:do_test(
    "where7-2.424.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='ghijklmno'
         OR b=1012
  ]])
    end, {
        -- <where7-2.424.2>
        6, 32, 58, 84, 92, "scan", 0, "sort", 0
        -- </where7-2.424.2>
    })

test:do_test(
    "where7-2.425.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=597
         OR f='lmnopqrst'
         OR a=24
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR b=1023
         OR a=53
         OR a=78
         OR f='efghijklm'
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.425.1>
        4, 11, 24, 30, 31, 33, 37, 53, 56, 63, 78, 82, 85, 89, 93, 96, "scan", 0, "sort", 0
        -- </where7-2.425.1>
    })

test:do_test(
    "where7-2.425.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=597
         OR f='lmnopqrst'
         OR a=24
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR b=1023
         OR a=53
         OR a=78
         OR f='efghijklm'
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.425.2>
        4, 11, 24, 30, 31, 33, 37, 53, 56, 63, 78, 82, 85, 89, 93, 96, "scan", 0, "sort", 0
        -- </where7-2.425.2>
    })

test:do_test(
    "where7-2.426.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=198
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR b=388
  ]])
    end, {
        -- <where7-2.426.1>
        18, 94, "scan", 0, "sort", 0
        -- </where7-2.426.1>
    })

test:do_test(
    "where7-2.426.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=198
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR b=388
  ]])
    end, {
        -- <where7-2.426.2>
        18, 94, "scan", 0, "sort", 0
        -- </where7-2.426.2>
    })

test:do_test(
    "where7-2.427.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='tuvwxyzab'
         OR b=388
         OR ((a BETWEEN 84 AND 86) AND a!=85)
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR b=957
         OR b=663
         OR b=847
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.427.1>
        19, 45, 71, 73, 77, 84, 86, 87, 96, 97, "scan", 0, "sort", 0
        -- </where7-2.427.1>
    })

test:do_test(
    "where7-2.427.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='tuvwxyzab'
         OR b=388
         OR ((a BETWEEN 84 AND 86) AND a!=85)
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR b=957
         OR b=663
         OR b=847
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.427.2>
        19, 45, 71, 73, 77, 84, 86, 87, 96, 97, "scan", 0, "sort", 0
        -- </where7-2.427.2>
    })

test:do_test(
    "where7-2.428.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR a=56
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
  ]])
    end, {
        -- <where7-2.428.1>
        56, 81, 84, "scan", 0, "sort", 0
        -- </where7-2.428.1>
    })

test:do_test(
    "where7-2.428.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR a=56
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
  ]])
    end, {
        -- <where7-2.428.2>
        56, 81, 84, "scan", 0, "sort", 0
        -- </where7-2.428.2>
    })

test:do_test(
    "where7-2.429.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c>=34035
         OR b=168
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
  ]])
    end, {
        -- <where7-2.429.1>
        1, 27, 53, 79, 89, "scan", 0, "sort", 0
        -- </where7-2.429.1>
    })

test:do_test(
    "where7-2.429.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c>=34035
         OR b=168
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
  ]])
    end, {
        -- <where7-2.429.2>
        1, 27, 53, 79, 89, "scan", 0, "sort", 0
        -- </where7-2.429.2>
    })

test:do_test(
    "where7-2.430.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 79 AND 81) AND a!=80)
         OR b=564
         OR c=6006
         OR b=979
  ]])
    end, {
        -- <where7-2.430.1>
        16, 17, 18, 79, 81, 89, "scan", 0, "sort", 0
        -- </where7-2.430.1>
    })

test:do_test(
    "where7-2.430.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 79 AND 81) AND a!=80)
         OR b=564
         OR c=6006
         OR b=979
  ]])
    end, {
        -- <where7-2.430.2>
        16, 17, 18, 79, 81, 89, "scan", 0, "sort", 0
        -- </where7-2.430.2>
    })

test:do_test(
    "where7-2.431.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR f='rstuvwxyz'
         OR (g='qponmlk' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.431.1>
        17, 29, 39, 40, 43, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.431.1>
    })

test:do_test(
    "where7-2.431.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR f='rstuvwxyz'
         OR (g='qponmlk' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.431.2>
        17, 29, 39, 40, 43, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.431.2>
    })

test:do_test(
    "where7-2.432.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=58
         OR b=484
         OR (d>=68.0 AND d<69.0 AND d IS NOT NULL)
         OR b=671
         OR a=69
  ]])
    end, {
        -- <where7-2.432.1>
        44, 61, 68, 69, "scan", 0, "sort", 0
        -- </where7-2.432.1>
    })

test:do_test(
    "where7-2.432.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=58
         OR b=484
         OR (d>=68.0 AND d<69.0 AND d IS NOT NULL)
         OR b=671
         OR a=69
  ]])
    end, {
        -- <where7-2.432.2>
        44, 61, 68, 69, "scan", 0, "sort", 0
        -- </where7-2.432.2>
    })

test:do_test(
    "where7-2.433.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='mnopqrstu'
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR b=861
         OR b=77
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.433.1>
        7, 12, 16, 38, 42, 64, 68, 73, 90, 94, "scan", 0, "sort", 0
        -- </where7-2.433.1>
    })

test:do_test(
    "where7-2.433.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='mnopqrstu'
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR b=861
         OR b=77
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.433.2>
        7, 12, 16, 38, 42, 64, 68, 73, 90, 94, "scan", 0, "sort", 0
        -- </where7-2.433.2>
    })

test:do_test(
    "where7-2.434.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=113
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR b=113
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR ((a BETWEEN 62 AND 64) AND a!=63)
         OR c=6006
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
         OR b=946
         OR a=86
  ]])
    end, {
        -- <where7-2.434.1>
        4, 14, 16, 17, 18, 51, 62, 64, 86, "scan", 0, "sort", 0
        -- </where7-2.434.1>
    })

test:do_test(
    "where7-2.434.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=113
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR b=113
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR ((a BETWEEN 62 AND 64) AND a!=63)
         OR c=6006
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
         OR b=946
         OR a=86
  ]])
    end, {
        -- <where7-2.434.2>
        4, 14, 16, 17, 18, 51, 62, 64, 86, "scan", 0, "sort", 0
        -- </where7-2.434.2>
    })

test:do_test(
    "where7-2.435.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'hijkl*')
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR c=22022
         OR ((a BETWEEN 79 AND 81) AND a!=80)
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR c=25025
  ]])
    end, {
        -- <where7-2.435.1>
        8, 10, 64, 65, 66, 73, 74, 75, 79, 81, 85, "scan", 0, "sort", 0
        -- </where7-2.435.1>
    })

test:do_test(
    "where7-2.435.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'hijkl*')
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR c=22022
         OR ((a BETWEEN 79 AND 81) AND a!=80)
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR c=25025
  ]])
    end, {
        -- <where7-2.435.2>
        8, 10, 64, 65, 66, 73, 74, 75, 79, 81, 85, "scan", 0, "sort", 0
        -- </where7-2.435.2>
    })

test:do_test(
    "where7-2.436.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 74 AND 76) AND a!=75)
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR b=47
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR a=92
         OR b=795
         OR b=25
         OR c=7007
         OR a=93
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
  ]])
    end, {
        -- <where7-2.436.1>
        14, 18, 19, 20, 21, 40, 44, 46, 66, 74, 76, 92, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.436.1>
    })

test:do_test(
    "where7-2.436.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 74 AND 76) AND a!=75)
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR b=47
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR a=92
         OR b=795
         OR b=25
         OR c=7007
         OR a=93
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
  ]])
    end, {
        -- <where7-2.436.2>
        14, 18, 19, 20, 21, 40, 44, 46, 66, 74, 76, 92, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.436.2>
    })

test:do_test(
    "where7-2.437.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR a=13
         OR (g='fedcbaz' AND f GLOB 'qrstu*')
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR c=29029
         OR b=311
         OR b=366
         OR a=94
         OR a=72
  ]])
    end, {
        -- <where7-2.437.1>
        6, 13, 66, 72, 85, 86, 87, 94, "scan", 0, "sort", 0
        -- </where7-2.437.1>
    })

test:do_test(
    "where7-2.437.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR a=13
         OR (g='fedcbaz' AND f GLOB 'qrstu*')
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR c=29029
         OR b=311
         OR b=366
         OR a=94
         OR a=72
  ]])
    end, {
        -- <where7-2.437.2>
        6, 13, 66, 72, 85, 86, 87, 94, "scan", 0, "sort", 0
        -- </where7-2.437.2>
    })

test:do_test(
    "where7-2.438.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=26026
         OR a=96
         OR a=22
         OR b=341
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR b=872
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR ((a BETWEEN 25 AND 27) AND a!=26)
  ]])
    end, {
        -- <where7-2.438.1>
        2, 22, 25, 27, 31, 76, 77, 78, 96, "scan", 0, "sort", 0
        -- </where7-2.438.1>
    })

test:do_test(
    "where7-2.438.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=26026
         OR a=96
         OR a=22
         OR b=341
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR b=872
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR ((a BETWEEN 25 AND 27) AND a!=26)
  ]])
    end, {
        -- <where7-2.438.2>
        2, 22, 25, 27, 31, 76, 77, 78, 96, "scan", 0, "sort", 0
        -- </where7-2.438.2>
    })

test:do_test(
    "where7-2.439.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=41
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR (g='onmlkji' AND f GLOB 'zabcd*')
         OR b=913
  ]])
    end, {
        -- <where7-2.439.1>
        6, 23, 36, 41, 51, 63, 65, 82, 83, "scan", 0, "sort", 0
        -- </where7-2.439.1>
    })

test:do_test(
    "where7-2.439.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=41
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR (g='onmlkji' AND f GLOB 'zabcd*')
         OR b=913
  ]])
    end, {
        -- <where7-2.439.2>
        6, 23, 36, 41, 51, 63, 65, 82, 83, "scan", 0, "sort", 0
        -- </where7-2.439.2>
    })

test:do_test(
    "where7-2.440.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 42 AND 44) AND a!=43)
         OR a=90
  ]])
    end, {
        -- <where7-2.440.1>
        42, 44, 90, "scan", 0, "sort", 0
        -- </where7-2.440.1>
    })

test:do_test(
    "where7-2.440.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 42 AND 44) AND a!=43)
         OR a=90
  ]])
    end, {
        -- <where7-2.440.2>
        42, 44, 90, "scan", 0, "sort", 0
        -- </where7-2.440.2>
    })

test:do_test(
    "where7-2.441.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR b=484
  ]])
    end, {
        -- <where7-2.441.1>
        21, 44, "scan", 0, "sort", 0
        -- </where7-2.441.1>
    })

test:do_test(
    "where7-2.441.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR b=484
  ]])
    end, {
        -- <where7-2.441.2>
        21, 44, "scan", 0, "sort", 0
        -- </where7-2.441.2>
    })

test:do_test(
    "where7-2.442.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR b=377
         OR b=363
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR b=737
         OR (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR b=506
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR a=16
  ]])
    end, {
        -- <where7-2.442.1>
        16, 22, 25, 33, 46, 55, 57, 67, 100, "scan", 0, "sort", 0
        -- </where7-2.442.1>
    })

test:do_test(
    "where7-2.442.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR b=377
         OR b=363
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR b=737
         OR (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR b=506
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR a=16
  ]])
    end, {
        -- <where7-2.442.2>
        16, 22, 25, 33, 46, 55, 57, 67, 100, "scan", 0, "sort", 0
        -- </where7-2.442.2>
    })

test:do_test(
    "where7-2.443.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='jihgfed' AND f GLOB 'zabcd*')
         OR b=102
         OR b=212
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR b=487
         OR (g='ihgfedc' AND f GLOB 'efghi*')
  ]])
    end, {
        -- <where7-2.443.1>
        37, 77, 82, "scan", 0, "sort", 0
        -- </where7-2.443.1>
    })

test:do_test(
    "where7-2.443.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='jihgfed' AND f GLOB 'zabcd*')
         OR b=102
         OR b=212
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR b=487
         OR (g='ihgfedc' AND f GLOB 'efghi*')
  ]])
    end, {
        -- <where7-2.443.2>
        37, 77, 82, "scan", 0, "sort", 0
        -- </where7-2.443.2>
    })

test:do_test(
    "where7-2.444.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=154
         OR a=51
         OR b=520
  ]])
    end, {
        -- <where7-2.444.1>
        14, 51, "scan", 0, "sort", 0
        -- </where7-2.444.1>
    })

test:do_test(
    "where7-2.444.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=154
         OR a=51
         OR b=520
  ]])
    end, {
        -- <where7-2.444.2>
        14, 51, "scan", 0, "sort", 0
        -- </where7-2.444.2>
    })

test:do_test(
    "where7-2.445.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=872
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR b=957
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR a=67
         OR a=72
  ]])
    end, {
        -- <where7-2.445.1>
        21, 42, 47, 58, 60, 67, 72, 73, 87, 99, "scan", 0, "sort", 0
        -- </where7-2.445.1>
    })

test:do_test(
    "where7-2.445.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=872
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR b=957
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR a=67
         OR a=72
  ]])
    end, {
        -- <where7-2.445.2>
        21, 42, 47, 58, 60, 67, 72, 73, 87, 99, "scan", 0, "sort", 0
        -- </where7-2.445.2>
    })

test:do_test(
    "where7-2.446.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=66
         OR b=102
         OR b=396
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR ((a BETWEEN 7 AND 9) AND a!=8)
         OR b=759
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR f='ghijklmno'
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.446.1>
        6, 7, 9, 14, 32, 36, 58, 69, 84, 90, 92, 97, 100, "scan", 0, "sort", 0
        -- </where7-2.446.1>
    })

test:do_test(
    "where7-2.446.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=66
         OR b=102
         OR b=396
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR ((a BETWEEN 7 AND 9) AND a!=8)
         OR b=759
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR f='ghijklmno'
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.446.2>
        6, 7, 9, 14, 32, 36, 58, 69, 84, 90, 92, 97, 100, "scan", 0, "sort", 0
        -- </where7-2.446.2>
    })

test:do_test(
    "where7-2.447.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 69 AND 71) AND a!=70)
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR a=72
         OR b=1100
         OR b=102
         OR b=135
  ]])
    end, {
        -- <where7-2.447.1>
        24, 48, 50, 69, 71, 72, 76, 100, "scan", 0, "sort", 0
        -- </where7-2.447.1>
    })

test:do_test(
    "where7-2.447.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 69 AND 71) AND a!=70)
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR a=72
         OR b=1100
         OR b=102
         OR b=135
  ]])
    end, {
        -- <where7-2.447.2>
        24, 48, 50, 69, 71, 72, 76, 100, "scan", 0, "sort", 0
        -- </where7-2.447.2>
    })

test:do_test(
    "where7-2.448.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=99
         OR a=76
  ]])
    end, {
        -- <where7-2.448.1>
        9, 76, "scan", 0, "sort", 0
        -- </where7-2.448.1>
    })

test:do_test(
    "where7-2.448.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=99
         OR a=76
  ]])
    end, {
        -- <where7-2.448.2>
        9, 76, "scan", 0, "sort", 0
        -- </where7-2.448.2>
    })

test:do_test(
    "where7-2.449.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=891
         OR b=806
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR b=861
         OR ((a BETWEEN 82 AND 84) AND a!=83)
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.449.1>
        19, 34, 81, 82, 84, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.449.1>
    })

test:do_test(
    "where7-2.449.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=891
         OR b=806
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR b=861
         OR ((a BETWEEN 82 AND 84) AND a!=83)
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.449.2>
        19, 34, 81, 82, 84, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.449.2>
    })

test:do_test(
    "where7-2.450.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1034
         OR b=91
  ]])
    end, {
        -- <where7-2.450.1>
        94, "scan", 0, "sort", 0
        -- </where7-2.450.1>
    })

test:do_test(
    "where7-2.450.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1034
         OR b=91
  ]])
    end, {
        -- <where7-2.450.2>
        94, "scan", 0, "sort", 0
        -- </where7-2.450.2>
    })

test:do_test(
    "where7-2.451.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=47
         OR a=91
         OR d>1e10
         OR (g='srqponm' AND f GLOB 'cdefg*')
  ]])
    end, {
        -- <where7-2.451.1>
        28, 91, "scan", 0, "sort", 0
        -- </where7-2.451.1>
    })

test:do_test(
    "where7-2.451.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=47
         OR a=91
         OR d>1e10
         OR (g='srqponm' AND f GLOB 'cdefg*')
  ]])
    end, {
        -- <where7-2.451.2>
        28, 91, "scan", 0, "sort", 0
        -- </where7-2.451.2>
    })

test:do_test(
    "where7-2.452.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1023
         OR f='zabcdefgh'
         OR b=451
         OR b=443
         OR c>=34035
         OR b=58
  ]])
    end, {
        -- <where7-2.452.1>
        25, 41, 51, 77, 93, "scan", 0, "sort", 0
        -- </where7-2.452.1>
    })

test:do_test(
    "where7-2.452.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1023
         OR f='zabcdefgh'
         OR b=451
         OR b=443
         OR c>=34035
         OR b=58
  ]])
    end, {
        -- <where7-2.452.2>
        25, 41, 51, 77, 93, "scan", 0, "sort", 0
        -- </where7-2.452.2>
    })

test:do_test(
    "where7-2.453.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=971
         OR b=36
         OR a=11
         OR f='hijklmnop'
  ]])
    end, {
        -- <where7-2.453.1>
        7, 11, 33, 59, 85, "scan", 0, "sort", 0
        -- </where7-2.453.1>
    })

test:do_test(
    "where7-2.453.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=971
         OR b=36
         OR a=11
         OR f='hijklmnop'
  ]])
    end, {
        -- <where7-2.453.2>
        7, 11, 33, 59, 85, "scan", 0, "sort", 0
        -- </where7-2.453.2>
    })

test:do_test(
    "where7-2.454.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR b=619
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR c=11011
         OR b=550
         OR b=1059
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR b=737
  ]])
    end, {
        -- <where7-2.454.1>
        3, 18, 29, 31, 32, 33, 50, 55, 67, 78, 81, 84, 91, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.454.1>
    })

test:do_test(
    "where7-2.454.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR b=619
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR c=11011
         OR b=550
         OR b=1059
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR b=737
  ]])
    end, {
        -- <where7-2.454.2>
        3, 18, 29, 31, 32, 33, 50, 55, 67, 78, 81, 84, 91, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.454.2>
    })

test:do_test(
    "where7-2.455.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='edcbazy' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR a=78
         OR a=27
         OR b=792
         OR b=946
         OR c=22022
         OR a=23
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR b=388
  ]])
    end, {
        -- <where7-2.455.1>
        13, 23, 27, 39, 59, 61, 64, 65, 66, 72, 78, 80, 86, 91, 99, "scan", 0, "sort", 0
        -- </where7-2.455.1>
    })

test:do_test(
    "where7-2.455.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='edcbazy' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR a=78
         OR a=27
         OR b=792
         OR b=946
         OR c=22022
         OR a=23
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR b=388
  ]])
    end, {
        -- <where7-2.455.2>
        13, 23, 27, 39, 59, 61, 64, 65, 66, 72, 78, 80, 86, 91, 99, "scan", 0, "sort", 0
        -- </where7-2.455.2>
    })

test:do_test(
    "where7-2.456.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=32032
         OR f IS NULL
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR b=825
  ]])
    end, {
        -- <where7-2.456.1>
        4, 37, 39, 74, 75, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.456.1>
    })

test:do_test(
    "where7-2.456.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=32032
         OR f IS NULL
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR b=825
  ]])
    end, {
        -- <where7-2.456.2>
        4, 37, 39, 74, 75, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.456.2>
    })

test:do_test(
    "where7-2.457.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=84.0 AND d<85.0 AND d IS NOT NULL)
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR ((a BETWEEN 5 AND 7) AND a!=6)
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR b=1078
         OR b=198
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR b=55
         OR b=517
         OR b=740
  ]])
    end, {
        -- <where7-2.457.1>
        5, 7, 18, 21, 47, 54, 67, 73, 84, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.457.1>
    })

test:do_test(
    "where7-2.457.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=84.0 AND d<85.0 AND d IS NOT NULL)
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR ((a BETWEEN 5 AND 7) AND a!=6)
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR b=1078
         OR b=198
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR b=55
         OR b=517
         OR b=740
  ]])
    end, {
        -- <where7-2.457.2>
        5, 7, 18, 21, 47, 54, 67, 73, 84, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.457.2>
    })

test:do_test(
    "where7-2.458.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='wvutsrq' AND f GLOB 'ijklm*')
         OR c=25025
         OR b=550
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.458.1>
        8, 22, 50, 53, 73, 74, 75, "scan", 0, "sort", 0
        -- </where7-2.458.1>
    })

test:do_test(
    "where7-2.458.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='wvutsrq' AND f GLOB 'ijklm*')
         OR c=25025
         OR b=550
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.458.2>
        8, 22, 50, 53, 73, 74, 75, "scan", 0, "sort", 0
        -- </where7-2.458.2>
    })

test:do_test(
    "where7-2.459.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=432
         OR f='opqrstuvw'
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.459.1>
        14, 40, 66, 68, 92, "scan", 0, "sort", 0
        -- </where7-2.459.1>
    })

test:do_test(
    "where7-2.459.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=432
         OR f='opqrstuvw'
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.459.2>
        14, 40, 66, 68, 92, "scan", 0, "sort", 0
        -- </where7-2.459.2>
    })

test:do_test(
    "where7-2.460.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 14 AND 16) AND a!=15)
         OR b=847
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR b=583
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR b=938
  ]])
    end, {
        -- <where7-2.460.1>
        11, 14, 16, 26, 37, 40, 42, 53, 63, 65, 75, 77, 89, "scan", 0, "sort", 0
        -- </where7-2.460.1>
    })

test:do_test(
    "where7-2.460.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 14 AND 16) AND a!=15)
         OR b=847
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR b=583
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR b=938
  ]])
    end, {
        -- <where7-2.460.2>
        11, 14, 16, 26, 37, 40, 42, 53, 63, 65, 75, 77, 89, "scan", 0, "sort", 0
        -- </where7-2.460.2>
    })

test:do_test(
    "where7-2.461.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=671
         OR a=56
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR b=157
         OR a=83
         OR ((a BETWEEN 73 AND 75) AND a!=74)
         OR c=21021
         OR b=319
         OR b=187
         OR ((a BETWEEN 65 AND 67) AND a!=66)
         OR b=839
  ]])
    end, {
        -- <where7-2.461.1>
        17, 29, 49, 56, 61, 62, 63, 65, 67, 73, 75, 83, "scan", 0, "sort", 0
        -- </where7-2.461.1>
    })

test:do_test(
    "where7-2.461.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=671
         OR a=56
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR b=157
         OR a=83
         OR ((a BETWEEN 73 AND 75) AND a!=74)
         OR c=21021
         OR b=319
         OR b=187
         OR ((a BETWEEN 65 AND 67) AND a!=66)
         OR b=839
  ]])
    end, {
        -- <where7-2.461.2>
        17, 29, 49, 56, 61, 62, 63, 65, 67, 73, 75, 83, "scan", 0, "sort", 0
        -- </where7-2.461.2>
    })

test:do_test(
    "where7-2.462.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR b=586
         OR d<0.0
         OR c=9009
  ]])
    end, {
        -- <where7-2.462.1>
        25, 26, 27, 72, "scan", 0, "sort", 0
        -- </where7-2.462.1>
    })

test:do_test(
    "where7-2.462.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR b=586
         OR d<0.0
         OR c=9009
  ]])
    end, {
        -- <where7-2.462.2>
        25, 26, 27, 72, "scan", 0, "sort", 0
        -- </where7-2.462.2>
    })

test:do_test(
    "where7-2.463.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=82
         OR a=34
         OR f='jklmnopqr'
         OR a=82
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=454
         OR b=355
         OR c=21021
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=30
  ]])
    end, {
        -- <where7-2.463.1>
        9, 16, 30, 34, 35, 61, 62, 63, 65, 82, 87, "scan", 0, "sort", 0
        -- </where7-2.463.1>
    })

test:do_test(
    "where7-2.463.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=82
         OR a=34
         OR f='jklmnopqr'
         OR a=82
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=454
         OR b=355
         OR c=21021
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=30
  ]])
    end, {
        -- <where7-2.463.2>
        9, 16, 30, 34, 35, 61, 62, 63, 65, 82, 87, "scan", 0, "sort", 0
        -- </where7-2.463.2>
    })

test:do_test(
    "where7-2.464.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 44 AND 46) AND a!=45)
         OR a=53
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR b=594
         OR b=80
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR d>1e10
  ]])
    end, {
        -- <where7-2.464.1>
        18, 20, 23, 44, 46, 49, 53, 54, "scan", 0, "sort", 0
        -- </where7-2.464.1>
    })

test:do_test(
    "where7-2.464.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 44 AND 46) AND a!=45)
         OR a=53
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR b=594
         OR b=80
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR d>1e10
  ]])
    end, {
        -- <where7-2.464.2>
        18, 20, 23, 44, 46, 49, 53, 54, "scan", 0, "sort", 0
        -- </where7-2.464.2>
    })

test:do_test(
    "where7-2.465.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='opqrstuvw'
         OR a=7
  ]])
    end, {
        -- <where7-2.465.1>
        7, 14, 40, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.465.1>
    })

test:do_test(
    "where7-2.465.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='opqrstuvw'
         OR a=7
  ]])
    end, {
        -- <where7-2.465.2>
        7, 14, 40, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.465.2>
    })

test:do_test(
    "where7-2.466.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=627
         OR ((a BETWEEN 75 AND 77) AND a!=76)
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR a=90
         OR (d>=33.0 AND d<34.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.466.1>
        33, 43, 45, 57, 75, 77, 90, "scan", 0, "sort", 0
        -- </where7-2.466.1>
    })

test:do_test(
    "where7-2.466.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=627
         OR ((a BETWEEN 75 AND 77) AND a!=76)
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR a=90
         OR (d>=33.0 AND d<34.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.466.2>
        33, 43, 45, 57, 75, 77, 90, "scan", 0, "sort", 0
        -- </where7-2.466.2>
    })

test:do_test(
    "where7-2.467.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=59
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR f='wxyzabcde'
         OR (f GLOB '?abcd*' AND f GLOB 'zabc*')
         OR a=70
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.467.1>
        5, 9, 14, 16, 22, 23, 25, 48, 51, 59, 69, 70, 71, 74, 77, 100, "scan", 0, "sort", 0
        -- </where7-2.467.1>
    })

test:do_test(
    "where7-2.467.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=59
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR f='wxyzabcde'
         OR (f GLOB '?abcd*' AND f GLOB 'zabc*')
         OR a=70
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.467.2>
        5, 9, 14, 16, 22, 23, 25, 48, 51, 59, 69, 70, 71, 74, 77, 100, "scan", 0, "sort", 0
        -- </where7-2.467.2>
    })

test:do_test(
    "where7-2.468.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=69
         OR (g='ihgfedc' AND f GLOB 'defgh*')
  ]])
    end, {
        -- <where7-2.468.1>
        69, 81, "scan", 0, "sort", 0
        -- </where7-2.468.1>
    })

test:do_test(
    "where7-2.468.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=69
         OR (g='ihgfedc' AND f GLOB 'defgh*')
  ]])
    end, {
        -- <where7-2.468.2>
        69, 81, "scan", 0, "sort", 0
        -- </where7-2.468.2>
    })

test:do_test(
    "where7-2.469.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=41
         OR a=43
         OR a=92
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR (g='mlkjihg' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.469.1>
        41, 43, 62, 92, 95, "scan", 0, "sort", 0
        -- </where7-2.469.1>
    })

test:do_test(
    "where7-2.469.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=41
         OR a=43
         OR a=92
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR (g='mlkjihg' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.469.2>
        41, 43, 62, 92, 95, "scan", 0, "sort", 0
        -- </where7-2.469.2>
    })

test:do_test(
    "where7-2.470.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=300
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR b=935
         OR b=190
  ]])
    end, {
        -- <where7-2.470.1>
        52, 85, "scan", 0, "sort", 0
        -- </where7-2.470.1>
    })

test:do_test(
    "where7-2.470.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=300
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR b=935
         OR b=190
  ]])
    end, {
        -- <where7-2.470.2>
        52, 85, "scan", 0, "sort", 0
        -- </where7-2.470.2>
    })

test:do_test(
    "where7-2.471.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='fghijklmn'
         OR f='fghijklmn'
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR b=465
         OR b=586
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR b=88
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR b=726
         OR ((a BETWEEN 51 AND 53) AND a!=52)
  ]])
    end, {
        -- <where7-2.471.1>
        4, 5, 8, 20, 30, 31, 32, 51, 53, 57, 66, 83, "scan", 0, "sort", 0
        -- </where7-2.471.1>
    })

test:do_test(
    "where7-2.471.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='fghijklmn'
         OR f='fghijklmn'
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR b=465
         OR b=586
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR b=88
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR b=726
         OR ((a BETWEEN 51 AND 53) AND a!=52)
  ]])
    end, {
        -- <where7-2.471.2>
        4, 5, 8, 20, 30, 31, 32, 51, 53, 57, 66, 83, "scan", 0, "sort", 0
        -- </where7-2.471.2>
    })

test:do_test(
    "where7-2.472.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR (f GLOB '?abcd*' AND f GLOB 'zabc*')
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR b=814
         OR a=20
         OR 1000000<b
         OR b=792
  ]])
    end, {
        -- <where7-2.472.1>
        20, 25, 34, 51, 72, 74, 77, 85, 97, 100, "scan", 0, "sort", 0
        -- </where7-2.472.1>
    })

test:do_test(
    "where7-2.472.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR (f GLOB '?abcd*' AND f GLOB 'zabc*')
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR b=814
         OR a=20
         OR 1000000<b
         OR b=792
  ]])
    end, {
        -- <where7-2.472.2>
        20, 25, 34, 51, 72, 74, 77, 85, 97, 100, "scan", 0, "sort", 0
        -- </where7-2.472.2>
    })

test:do_test(
    "where7-2.473.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR c=1001
         OR b=484
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
         OR c<=10
         OR a=92
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR ((a BETWEEN 0 AND 2) AND a!=1)
         OR b=1026
  ]])
    end, {
        -- <where7-2.473.1>
        1, 2, 3, 25, 44, 53, 55, 65, 72, 92, "scan", 0, "sort", 0
        -- </where7-2.473.1>
    })

test:do_test(
    "where7-2.473.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR c=1001
         OR b=484
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
         OR c<=10
         OR a=92
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR ((a BETWEEN 0 AND 2) AND a!=1)
         OR b=1026
  ]])
    end, {
        -- <where7-2.473.2>
        1, 2, 3, 25, 44, 53, 55, 65, 72, 92, "scan", 0, "sort", 0
        -- </where7-2.473.2>
    })

test:do_test(
    "where7-2.474.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=54
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR b=993
         OR c=22022
         OR a=68
         OR ((a BETWEEN 99 AND 101) AND a!=100)
         OR a=62
         OR (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR b=1015
  ]])
    end, {
        -- <where7-2.474.1>
        3, 29, 54, 55, 62, 64, 65, 66, 68, 81, 99, "scan", 0, "sort", 0
        -- </where7-2.474.1>
    })

test:do_test(
    "where7-2.474.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=54
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR b=993
         OR c=22022
         OR a=68
         OR ((a BETWEEN 99 AND 101) AND a!=100)
         OR a=62
         OR (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR b=1015
  ]])
    end, {
        -- <where7-2.474.2>
        3, 29, 54, 55, 62, 64, 65, 66, 68, 81, 99, "scan", 0, "sort", 0
        -- </where7-2.474.2>
    })

test:do_test(
    "where7-2.475.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=319
         OR a=50
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR a=96
  ]])
    end, {
        -- <where7-2.475.1>
        10, 29, 50, 55, 92, 96, "scan", 0, "sort", 0
        -- </where7-2.475.1>
    })

test:do_test(
    "where7-2.475.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=319
         OR a=50
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR a=96
  ]])
    end, {
        -- <where7-2.475.2>
        10, 29, 50, 55, 92, 96, "scan", 0, "sort", 0
        -- </where7-2.475.2>
    })

test:do_test(
    "where7-2.476.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=971
         OR c=18018
         OR b=564
         OR b=583
         OR b=80
  ]])
    end, {
        -- <where7-2.476.1>
        52, 53, 54, "scan", 0, "sort", 0
        -- </where7-2.476.1>
    })

test:do_test(
    "where7-2.476.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=971
         OR c=18018
         OR b=564
         OR b=583
         OR b=80
  ]])
    end, {
        -- <where7-2.476.2>
        52, 53, 54, "scan", 0, "sort", 0
        -- </where7-2.476.2>
    })

test:do_test(
    "where7-2.477.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=35.0 AND d<36.0 AND d IS NOT NULL)
         OR b=1026
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.477.1>
        14, 16, 35, "scan", 0, "sort", 0
        -- </where7-2.477.1>
    })

test:do_test(
    "where7-2.477.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=35.0 AND d<36.0 AND d IS NOT NULL)
         OR b=1026
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.477.2>
        14, 16, 35, "scan", 0, "sort", 0
        -- </where7-2.477.2>
    })

test:do_test(
    "where7-2.478.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR b=407
         OR b=454
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR b=627
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
  ]])
    end, {
        -- <where7-2.478.1>
        9, 13, 34, 35, 37, 39, 46, 57, 61, 65, 87, 91, "scan", 0, "sort", 0
        -- </where7-2.478.1>
    })

test:do_test(
    "where7-2.478.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR b=407
         OR b=454
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR b=627
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
  ]])
    end, {
        -- <where7-2.478.2>
        9, 13, 34, 35, 37, 39, 46, 57, 61, 65, 87, 91, "scan", 0, "sort", 0
        -- </where7-2.478.2>
    })

test:do_test(
    "where7-2.479.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR c=34034
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR a=67
  ]])
    end, {
        -- <where7-2.479.1>
        6, 18, 20, 24, 26, 32, 58, 67, 79, 84, 100, "scan", 0, "sort", 0
        -- </where7-2.479.1>
    })

test:do_test(
    "where7-2.479.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR c=34034
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR a=67
  ]])
    end, {
        -- <where7-2.479.2>
        6, 18, 20, 24, 26, 32, 58, 67, 79, 84, 100, "scan", 0, "sort", 0
        -- </where7-2.479.2>
    })

test:do_test(
    "where7-2.480.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=97
         OR b=575
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
  ]])
    end, {
        -- <where7-2.480.1>
        1, 2, 4, 16, 42, 68, 81, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.480.1>
    })

test:do_test(
    "where7-2.480.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=97
         OR b=575
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
  ]])
    end, {
        -- <where7-2.480.2>
        1, 2, 4, 16, 42, 68, 81, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.480.2>
    })

test:do_test(
    "where7-2.481.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=561
         OR b=773
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR b=201
         OR a=99
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR b=946
         OR b=993
         OR (g='fedcbaz' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.481.1>
        19, 23, 36, 38, 46, 51, 86, 94, 99, "scan", 0, "sort", 0
        -- </where7-2.481.1>
    })

test:do_test(
    "where7-2.481.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=561
         OR b=773
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR b=201
         OR a=99
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR b=946
         OR b=993
         OR (g='fedcbaz' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.481.2>
        19, 23, 36, 38, 46, 51, 86, 94, 99, "scan", 0, "sort", 0
        -- </where7-2.481.2>
    })

test:do_test(
    "where7-2.482.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=806
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR b=916
         OR b<0
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR b=154
         OR c=10010
         OR b=451
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.482.1>
        14, 24, 26, 28, 29, 30, 41, 62, 72, "scan", 0, "sort", 0
        -- </where7-2.482.1>
    })

test:do_test(
    "where7-2.482.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=806
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR b=916
         OR b<0
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR b=154
         OR c=10010
         OR b=451
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.482.2>
        14, 24, 26, 28, 29, 30, 41, 62, 72, "scan", 0, "sort", 0
        -- </where7-2.482.2>
    })

test:do_test(
    "where7-2.483.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=836
         OR d>1e10
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR f='pqrstuvwx'
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR f='abcdefghi'
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR a=33
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR b=476
  ]])
    end, {
        -- <where7-2.483.1>
        3, 5, 15, 19, 20, 21, 26, 33, 41, 52, 57, 67, 76, 78, 88, 90, 93, "scan", 0, "sort", 0
        -- </where7-2.483.1>
    })

test:do_test(
    "where7-2.483.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=836
         OR d>1e10
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR f='pqrstuvwx'
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR f='abcdefghi'
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR a=33
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR b=476
  ]])
    end, {
        -- <where7-2.483.2>
        3, 5, 15, 19, 20, 21, 26, 33, 41, 52, 57, 67, 76, 78, 88, 90, 93, "scan", 0, "sort", 0
        -- </where7-2.483.2>
    })

test:do_test(
    "where7-2.484.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=48
         OR a=92
         OR a=1
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR b=905
         OR ((a BETWEEN 51 AND 53) AND a!=52)
  ]])
    end, {
        -- <where7-2.484.1>
        1, 4, 7, 28, 30, 37, 48, 51, 53, 56, 82, 92, "scan", 0, "sort", 0
        -- </where7-2.484.1>
    })

test:do_test(
    "where7-2.484.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=48
         OR a=92
         OR a=1
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR b=905
         OR ((a BETWEEN 51 AND 53) AND a!=52)
  ]])
    end, {
        -- <where7-2.484.2>
        1, 4, 7, 28, 30, 37, 48, 51, 53, 56, 82, 92, "scan", 0, "sort", 0
        -- </where7-2.484.2>
    })

test:do_test(
    "where7-2.485.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR b=212
         OR a=42
         OR a=92
  ]])
    end, {
        -- <where7-2.485.1>
        4, 17, 42, 92, "scan", 0, "sort", 0
        -- </where7-2.485.1>
    })

test:do_test(
    "where7-2.485.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR b=212
         OR a=42
         OR a=92
  ]])
    end, {
        -- <where7-2.485.2>
        4, 17, 42, 92, "scan", 0, "sort", 0
        -- </where7-2.485.2>
    })

test:do_test(
    "where7-2.486.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=740
         OR b=564
         OR (g='onmlkji' AND f GLOB 'zabcd*')
         OR a=11
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR b=322
         OR (d>=6.0 AND d<7.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR b=902
         OR c>=34035
  ]])
    end, {
        -- <where7-2.486.1>
        6, 11, 22, 44, 46, 51, 82, "scan", 0, "sort", 0
        -- </where7-2.486.1>
    })

test:do_test(
    "where7-2.486.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=740
         OR b=564
         OR (g='onmlkji' AND f GLOB 'zabcd*')
         OR a=11
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR b=322
         OR (d>=6.0 AND d<7.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR b=902
         OR c>=34035
  ]])
    end, {
        -- <where7-2.486.2>
        6, 11, 22, 44, 46, 51, 82, "scan", 0, "sort", 0
        -- </where7-2.486.2>
    })

test:do_test(
    "where7-2.487.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 52 AND 54) AND a!=53)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR a=27
         OR a=48
         OR b=927
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR f='abcdefghi'
         OR b=91
         OR b=55
  ]])
    end, {
        -- <where7-2.487.1>
        5, 8, 26, 27, 48, 52, 54, 56, 58, 78, 89, 91, 96, "scan", 0, "sort", 0
        -- </where7-2.487.1>
    })

test:do_test(
    "where7-2.487.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 52 AND 54) AND a!=53)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR a=27
         OR a=48
         OR b=927
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
         OR f='abcdefghi'
         OR b=91
         OR b=55
  ]])
    end, {
        -- <where7-2.487.2>
        5, 8, 26, 27, 48, 52, 54, 56, 58, 78, 89, 91, 96, "scan", 0, "sort", 0
        -- </where7-2.487.2>
    })

test:do_test(
    "where7-2.488.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='srqponm' AND f GLOB 'efghi*')
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR a=20
         OR b=11
  ]])
    end, {
        -- <where7-2.488.1>
        1, 20, 30, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.488.1>
    })

test:do_test(
    "where7-2.488.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='srqponm' AND f GLOB 'efghi*')
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR a=20
         OR b=11
  ]])
    end, {
        -- <where7-2.488.2>
        1, 20, 30, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.488.2>
    })

test:do_test(
    "where7-2.489.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR b=55
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR a=50
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.489.1>
        5, 13, 27, 50, 51, 52, 64, 73, "scan", 0, "sort", 0
        -- </where7-2.489.1>
    })

test:do_test(
    "where7-2.489.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR b=55
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR a=50
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.489.2>
        5, 13, 27, 50, 51, 52, 64, 73, "scan", 0, "sort", 0
        -- </where7-2.489.2>
    })

test:do_test(
    "where7-2.490.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='rqponml' AND f GLOB 'ijklm*')
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
  ]])
    end, {
        -- <where7-2.490.1>
        22, 34, 48, 74, 100, "scan", 0, "sort", 0
        -- </where7-2.490.1>
    })

test:do_test(
    "where7-2.490.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='rqponml' AND f GLOB 'ijklm*')
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
  ]])
    end, {
        -- <where7-2.490.2>
        22, 34, 48, 74, 100, "scan", 0, "sort", 0
        -- </where7-2.490.2>
    })

test:do_test(
    "where7-2.491.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=704
         OR b=924
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR b=113
  ]])
    end, {
        -- <where7-2.491.1>
        64, 84, 90, "scan", 0, "sort", 0
        -- </where7-2.491.1>
    })

test:do_test(
    "where7-2.491.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=704
         OR b=924
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR b=113
  ]])
    end, {
        -- <where7-2.491.2>
        64, 84, 90, "scan", 0, "sort", 0
        -- </where7-2.491.2>
    })

test:do_test(
    "where7-2.492.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=289
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.492.1>
        14, 16, 20, 22, "scan", 0, "sort", 0
        -- </where7-2.492.1>
    })

test:do_test(
    "where7-2.492.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=289
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.492.2>
        14, 16, 20, 22, "scan", 0, "sort", 0
        -- </where7-2.492.2>
    })

test:do_test(
    "where7-2.493.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=935
         OR b=1001
         OR ((a BETWEEN 78 AND 80) AND a!=79)
         OR a=31
         OR a=56
  ]])
    end, {
        -- <where7-2.493.1>
        31, 56, 78, 80, 85, 91, "scan", 0, "sort", 0
        -- </where7-2.493.1>
    })

test:do_test(
    "where7-2.493.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=935
         OR b=1001
         OR ((a BETWEEN 78 AND 80) AND a!=79)
         OR a=31
         OR a=56
  ]])
    end, {
        -- <where7-2.493.2>
        31, 56, 78, 80, 85, 91, "scan", 0, "sort", 0
        -- </where7-2.493.2>
    })

test:do_test(
    "where7-2.494.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR b=726
         OR f='abcdefghi'
         OR b=179
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR b=539
         OR b=66
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
  ]])
    end, {
        -- <where7-2.494.1>
        6, 9, 19, 26, 35, 49, 52, 60, 61, 66, 78, 86, 87, 88, "scan", 0, "sort", 0
        -- </where7-2.494.1>
    })

test:do_test(
    "where7-2.494.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR b=726
         OR f='abcdefghi'
         OR b=179
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR b=539
         OR b=66
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
  ]])
    end, {
        -- <where7-2.494.2>
        6, 9, 19, 26, 35, 49, 52, 60, 61, 66, 78, 86, 87, 88, "scan", 0, "sort", 0
        -- </where7-2.494.2>
    })

test:do_test(
    "where7-2.495.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=179
         OR b=685
  ]])
    end, {
        -- <where7-2.495.1>
        "scan", 0, "sort", 0
        -- </where7-2.495.1>
    })

test:do_test(
    "where7-2.495.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=179
         OR b=685
  ]])
    end, {
        -- <where7-2.495.2>
        "scan", 0, "sort", 0
        -- </where7-2.495.2>
    })

test:do_test(
    "where7-2.496.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=201
         OR b=682
         OR b=443
         OR b=836
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR ((a BETWEEN 51 AND 53) AND a!=52)
         OR b=110
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
  ]])
    end, {
        -- <where7-2.496.1>
        2, 10, 11, 13, 28, 39, 51, 53, 54, 62, 65, 76, 80, 91, "scan", 0, "sort", 0
        -- </where7-2.496.1>
    })

test:do_test(
    "where7-2.496.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=201
         OR b=682
         OR b=443
         OR b=836
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR ((a BETWEEN 51 AND 53) AND a!=52)
         OR b=110
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
  ]])
    end, {
        -- <where7-2.496.2>
        2, 10, 11, 13, 28, 39, 51, 53, 54, 62, 65, 76, 80, 91, "scan", 0, "sort", 0
        -- </where7-2.496.2>
    })

test:do_test(
    "where7-2.497.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR b=462
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR a=22
         OR b=594
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
  ]])
    end, {
        -- <where7-2.497.1>
        4, 6, 18, 22, 24, 42, 44, 50, 54, 57, 61, 70, 74, 76, 96, "scan", 0, "sort", 0
        -- </where7-2.497.1>
    })

test:do_test(
    "where7-2.497.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR b=462
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR a=22
         OR b=594
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
  ]])
    end, {
        -- <where7-2.497.2>
        4, 6, 18, 22, 24, 42, 44, 50, 54, 57, 61, 70, 74, 76, 96, "scan", 0, "sort", 0
        -- </where7-2.497.2>
    })

test:do_test(
    "where7-2.498.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR f='vwxyzabcd'
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
         OR a=37
         OR a=50
  ]])
    end, {
        -- <where7-2.498.1>
        1, 10, 13, 21, 22, 37, 47, 50, 73, 99, "scan", 0, "sort", 0
        -- </where7-2.498.1>
    })

test:do_test(
    "where7-2.498.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR f='vwxyzabcd'
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
         OR a=37
         OR a=50
  ]])
    end, {
        -- <where7-2.498.2>
        1, 10, 13, 21, 22, 37, 47, 50, 73, 99, "scan", 0, "sort", 0
        -- </where7-2.498.2>
    })

test:do_test(
    "where7-2.499.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 83 AND 85) AND a!=84)
         OR b=784
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
         OR b=825
         OR a=80
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR b=531
         OR a=100
  ]])
    end, {
        -- <where7-2.499.1>
        20, 23, 46, 72, 75, 80, 83, 85, 97, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.499.1>
    })

test:do_test(
    "where7-2.499.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 83 AND 85) AND a!=84)
         OR b=784
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
         OR b=825
         OR a=80
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR b=531
         OR a=100
  ]])
    end, {
        -- <where7-2.499.2>
        20, 23, 46, 72, 75, 80, 83, 85, 97, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.499.2>
    })

test:do_test(
    "where7-2.500.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR b=220
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.500.1>
        19, 20, 53, "scan", 0, "sort", 0
        -- </where7-2.500.1>
    })

test:do_test(
    "where7-2.500.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR b=220
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.500.2>
        19, 20, 53, "scan", 0, "sort", 0
        -- </where7-2.500.2>
    })

test:do_test(
    "where7-2.501.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=92
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR b=990
  ]])
    end, {
        -- <where7-2.501.1>
        9, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.501.1>
    })

test:do_test(
    "where7-2.501.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=92
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR b=990
  ]])
    end, {
        -- <where7-2.501.2>
        9, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.501.2>
    })

test:do_test(
    "where7-2.502.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 77 AND 79) AND a!=78)
         OR b=894
         OR c=28028
         OR b=905
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR b=1037
  ]])
    end, {
        -- <where7-2.502.1>
        26, 45, 52, 70, 77, 78, 79, 82, 83, 84, "scan", 0, "sort", 0
        -- </where7-2.502.1>
    })

test:do_test(
    "where7-2.502.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 77 AND 79) AND a!=78)
         OR b=894
         OR c=28028
         OR b=905
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR b=1037
  ]])
    end, {
        -- <where7-2.502.2>
        26, 45, 52, 70, 77, 78, 79, 82, 83, 84, "scan", 0, "sort", 0
        -- </where7-2.502.2>
    })

test:do_test(
    "where7-2.503.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR b=773
         OR f='defghijkl'
  ]])
    end, {
        -- <where7-2.503.1>
        3, 29, 55, 72, 81, "scan", 0, "sort", 0
        -- </where7-2.503.1>
    })

test:do_test(
    "where7-2.503.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR b=773
         OR f='defghijkl'
  ]])
    end, {
        -- <where7-2.503.2>
        3, 29, 55, 72, 81, "scan", 0, "sort", 0
        -- </where7-2.503.2>
    })

test:do_test(
    "where7-2.504.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='wvutsrq' AND f GLOB 'mnopq*')
         OR b=861
         OR (g='rqponml' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.504.1>
        12, 37, "scan", 0, "sort", 0
        -- </where7-2.504.1>
    })

test:do_test(
    "where7-2.504.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='wvutsrq' AND f GLOB 'mnopq*')
         OR b=861
         OR (g='rqponml' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.504.2>
        12, 37, "scan", 0, "sort", 0
        -- </where7-2.504.2>
    })

test:do_test(
    "where7-2.505.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=704
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR b=25
         OR (g='jihgfed' AND f GLOB 'zabcd*')
         OR b=487
         OR (g='hgfedcb' AND f GLOB 'fghij*')
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (d>=84.0 AND d<85.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.505.1>
        10, 23, 51, 64, 77, 79, 83, 84, 89, "scan", 0, "sort", 0
        -- </where7-2.505.1>
    })

test:do_test(
    "where7-2.505.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=704
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR b=25
         OR (g='jihgfed' AND f GLOB 'zabcd*')
         OR b=487
         OR (g='hgfedcb' AND f GLOB 'fghij*')
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (d>=84.0 AND d<85.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.505.2>
        10, 23, 51, 64, 77, 79, 83, 84, 89, "scan", 0, "sort", 0
        -- </where7-2.505.2>
    })

test:do_test(
    "where7-2.506.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=19
         OR (g='onmlkji' AND f GLOB 'xyzab*')
         OR b=674
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR b=355
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR c=28028
         OR b=649
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR (g='srqponm' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.506.1>
        17, 19, 31, 41, 49, 59, 60, 72, 74, 82, 83, 84, "scan", 0, "sort", 0
        -- </where7-2.506.1>
    })

test:do_test(
    "where7-2.506.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=19
         OR (g='onmlkji' AND f GLOB 'xyzab*')
         OR b=674
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR b=355
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR c=28028
         OR b=649
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR (g='srqponm' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.506.2>
        17, 19, 31, 41, 49, 59, 60, 72, 74, 82, 83, 84, "scan", 0, "sort", 0
        -- </where7-2.506.2>
    })

test:do_test(
    "where7-2.507.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 76 AND 78) AND a!=77)
         OR a=1
         OR a=22
         OR b=836
         OR c=24024
  ]])
    end, {
        -- <where7-2.507.1>
        1, 22, 70, 71, 72, 76, 78, "scan", 0, "sort", 0
        -- </where7-2.507.1>
    })

test:do_test(
    "where7-2.507.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 76 AND 78) AND a!=77)
         OR a=1
         OR a=22
         OR b=836
         OR c=24024
  ]])
    end, {
        -- <where7-2.507.2>
        1, 22, 70, 71, 72, 76, 78, "scan", 0, "sort", 0
        -- </where7-2.507.2>
    })

test:do_test(
    "where7-2.508.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=135
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 39 AND 41) AND a!=40)
  ]])
    end, {
        -- <where7-2.508.1>
        20, 39, 41, "scan", 0, "sort", 0
        -- </where7-2.508.1>
    })

test:do_test(
    "where7-2.508.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=135
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 39 AND 41) AND a!=40)
  ]])
    end, {
        -- <where7-2.508.2>
        20, 39, 41, "scan", 0, "sort", 0
        -- </where7-2.508.2>
    })

test:do_test(
    "where7-2.509.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'ijklm*')
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
  ]])
    end, {
        -- <where7-2.509.1>
        9, 35, 61, 86, 87, "scan", 0, "sort", 0
        -- </where7-2.509.1>
    })

test:do_test(
    "where7-2.509.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'ijklm*')
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
  ]])
    end, {
        -- <where7-2.509.2>
        9, 35, 61, 86, 87, "scan", 0, "sort", 0
        -- </where7-2.509.2>
    })

test:do_test(
    "where7-2.510.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='jihgfed' AND f GLOB 'wxyza*')
         OR f='ghijklmno'
  ]])
    end, {
        -- <where7-2.510.1>
        6, 32, 58, 74, 84, "scan", 0, "sort", 0
        -- </where7-2.510.1>
    })

test:do_test(
    "where7-2.510.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='jihgfed' AND f GLOB 'wxyza*')
         OR f='ghijklmno'
  ]])
    end, {
        -- <where7-2.510.2>
        6, 32, 58, 74, 84, "scan", 0, "sort", 0
        -- </where7-2.510.2>
    })

test:do_test(
    "where7-2.511.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=825
         OR b=902
         OR a=40
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR a=30
         OR a=10
         OR a=73
  ]])
    end, {
        -- <where7-2.511.1>
        10, 28, 30, 40, 73, 75, 82, "scan", 0, "sort", 0
        -- </where7-2.511.1>
    })

test:do_test(
    "where7-2.511.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=825
         OR b=902
         OR a=40
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR a=30
         OR a=10
         OR a=73
  ]])
    end, {
        -- <where7-2.511.2>
        10, 28, 30, 40, 73, 75, 82, "scan", 0, "sort", 0
        -- </where7-2.511.2>
    })

test:do_test(
    "where7-2.512.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 22 AND 24) AND a!=23)
         OR a=5
         OR b=432
         OR b=979
         OR b=762
         OR b=352
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR c=27027
         OR c=20020
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.512.1>
        5, 22, 23, 24, 32, 36, 38, 58, 59, 60, 79, 80, 81, 89, "scan", 0, "sort", 0
        -- </where7-2.512.1>
    })

test:do_test(
    "where7-2.512.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 22 AND 24) AND a!=23)
         OR a=5
         OR b=432
         OR b=979
         OR b=762
         OR b=352
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR c=27027
         OR c=20020
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.512.2>
        5, 22, 23, 24, 32, 36, 38, 58, 59, 60, 79, 80, 81, 89, "scan", 0, "sort", 0
        -- </where7-2.512.2>
    })

test:do_test(
    "where7-2.513.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?lmno*' AND f GLOB 'klmn*')
         OR ((a BETWEEN 5 AND 7) AND a!=6)
         OR b=99
         OR a=54
  ]])
    end, {
        -- <where7-2.513.1>
        5, 7, 9, 10, 36, 54, 62, 88, "scan", 0, "sort", 0
        -- </where7-2.513.1>
    })

test:do_test(
    "where7-2.513.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?lmno*' AND f GLOB 'klmn*')
         OR ((a BETWEEN 5 AND 7) AND a!=6)
         OR b=99
         OR a=54
  ]])
    end, {
        -- <where7-2.513.2>
        5, 7, 9, 10, 36, 54, 62, 88, "scan", 0, "sort", 0
        -- </where7-2.513.2>
    })

test:do_test(
    "where7-2.514.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=300
         OR (g='mlkjihg' AND f GLOB 'klmno*')
         OR b=319
         OR f='fghijklmn'
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR ((a BETWEEN 42 AND 44) AND a!=43)
  ]])
    end, {
        -- <where7-2.514.1>
        5, 29, 31, 42, 44, 57, 62, 73, 83, "scan", 0, "sort", 0
        -- </where7-2.514.1>
    })

test:do_test(
    "where7-2.514.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=300
         OR (g='mlkjihg' AND f GLOB 'klmno*')
         OR b=319
         OR f='fghijklmn'
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR ((a BETWEEN 42 AND 44) AND a!=43)
  ]])
    end, {
        -- <where7-2.514.2>
        5, 29, 31, 42, 44, 57, 62, 73, 83, "scan", 0, "sort", 0
        -- </where7-2.514.2>
    })

test:do_test(
    "where7-2.515.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=671
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR b=1004
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR ((a BETWEEN 5 AND 7) AND a!=6)
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR b=748
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.515.1>
        5, 7, 18, 20, 23, 27, 61, 68, 82, 86, 88, "scan", 0, "sort", 0
        -- </where7-2.515.1>
    })

test:do_test(
    "where7-2.515.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=671
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR b=1004
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR ((a BETWEEN 5 AND 7) AND a!=6)
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR b=748
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.515.2>
        5, 7, 18, 20, 23, 27, 61, 68, 82, 86, 88, "scan", 0, "sort", 0
        -- </where7-2.515.2>
    })

test:do_test(
    "where7-2.516.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=47
         OR b=784
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR a=16
         OR a=25
         OR b=572
  ]])
    end, {
        -- <where7-2.516.1>
        16, 21, 23, 25, 47, 52, "scan", 0, "sort", 0
        -- </where7-2.516.1>
    })

test:do_test(
    "where7-2.516.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=47
         OR b=784
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR a=16
         OR a=25
         OR b=572
  ]])
    end, {
        -- <where7-2.516.2>
        16, 21, 23, 25, 47, 52, "scan", 0, "sort", 0
        -- </where7-2.516.2>
    })

test:do_test(
    "where7-2.517.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='jihgfed' AND f GLOB 'wxyza*')
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR b=110
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR c=26026
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR b=850
         OR a=6
  ]])
    end, {
        -- <where7-2.517.1>
        6, 10, 67, 69, 74, 76, 77, 78, 91, "scan", 0, "sort", 0
        -- </where7-2.517.1>
    })

test:do_test(
    "where7-2.517.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='jihgfed' AND f GLOB 'wxyza*')
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR b=110
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR c=26026
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR b=850
         OR a=6
  ]])
    end, {
        -- <where7-2.517.2>
        6, 10, 67, 69, 74, 76, 77, 78, 91, "scan", 0, "sort", 0
        -- </where7-2.517.2>
    })

test:do_test(
    "where7-2.518.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 74 AND 76) AND a!=75)
         OR ((a BETWEEN 1 AND 3) AND a!=2)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR (g='mlkjihg' AND f GLOB 'klmno*')
         OR b=135
         OR a=28
         OR ((a BETWEEN 1 AND 3) AND a!=2)
         OR b=737
  ]])
    end, {
        -- <where7-2.518.1>
        1, 3, 19, 28, 62, 67, 74, 76, "scan", 0, "sort", 0
        -- </where7-2.518.1>
    })

test:do_test(
    "where7-2.518.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 74 AND 76) AND a!=75)
         OR ((a BETWEEN 1 AND 3) AND a!=2)
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR (g='mlkjihg' AND f GLOB 'klmno*')
         OR b=135
         OR a=28
         OR ((a BETWEEN 1 AND 3) AND a!=2)
         OR b=737
  ]])
    end, {
        -- <where7-2.518.2>
        1, 3, 19, 28, 62, 67, 74, 76, "scan", 0, "sort", 0
        -- </where7-2.518.2>
    })

test:do_test(
    "where7-2.519.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=242
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR ((a BETWEEN 20 AND 22) AND a!=21)
  ]])
    end, {
        -- <where7-2.519.1>
        20, 22, "scan", 0, "sort", 0
        -- </where7-2.519.1>
    })

test:do_test(
    "where7-2.519.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=242
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR ((a BETWEEN 20 AND 22) AND a!=21)
  ]])
    end, {
        -- <where7-2.519.2>
        20, 22, "scan", 0, "sort", 0
        -- </where7-2.519.2>
    })

test:do_test(
    "where7-2.520.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=528
         OR a=41
         OR f='cdefghijk'
         OR a=98
         OR b=759
         OR a=43
         OR b=286
         OR f='hijklmnop'
  ]])
    end, {
        -- <where7-2.520.1>
        2, 7, 26, 28, 33, 41, 43, 48, 54, 59, 69, 80, 85, 98, "scan", 0, "sort", 0
        -- </where7-2.520.1>
    })

test:do_test(
    "where7-2.520.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=528
         OR a=41
         OR f='cdefghijk'
         OR a=98
         OR b=759
         OR a=43
         OR b=286
         OR f='hijklmnop'
  ]])
    end, {
        -- <where7-2.520.2>
        2, 7, 26, 28, 33, 41, 43, 48, 54, 59, 69, 80, 85, 98, "scan", 0, "sort", 0
        -- </where7-2.520.2>
    })

test:do_test(
    "where7-2.521.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'ghijk*')
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR a=52
  ]])
    end, {
        -- <where7-2.521.1>
        6, 15, 52, 61, "scan", 0, "sort", 0
        -- </where7-2.521.1>
    })

test:do_test(
    "where7-2.521.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'ghijk*')
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR a=52
  ]])
    end, {
        -- <where7-2.521.2>
        6, 15, 52, 61, "scan", 0, "sort", 0
        -- </where7-2.521.2>
    })

test:do_test(
    "where7-2.522.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ihgfedc' AND f GLOB 'abcde*')
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR a=86
         OR c=33033
         OR c=2002
         OR a=92
  ]])
    end, {
        -- <where7-2.522.1>
        2, 4, 5, 6, 78, 86, 92, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.522.1>
    })

test:do_test(
    "where7-2.522.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ihgfedc' AND f GLOB 'abcde*')
         OR ((a BETWEEN 2 AND 4) AND a!=3)
         OR a=86
         OR c=33033
         OR c=2002
         OR a=92
  ]])
    end, {
        -- <where7-2.522.2>
        2, 4, 5, 6, 78, 86, 92, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.522.2>
    })

test:do_test(
    "where7-2.523.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 47 AND 49) AND a!=48)
         OR b=517
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR f='defghijkl'
         OR b=707
         OR c>=34035
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR a=80
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.523.1>
        3, 23, 29, 31, 33, 35, 47, 49, 55, 63, 67, 69, 80, 81, "scan", 0, "sort", 0
        -- </where7-2.523.1>
    })

test:do_test(
    "where7-2.523.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 47 AND 49) AND a!=48)
         OR b=517
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR f='defghijkl'
         OR b=707
         OR c>=34035
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR a=80
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.523.2>
        3, 23, 29, 31, 33, 35, 47, 49, 55, 63, 67, 69, 80, 81, "scan", 0, "sort", 0
        -- </where7-2.523.2>
    })

test:do_test(
    "where7-2.524.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR b=209
         OR b=399
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
  ]])
    end, {
        -- <where7-2.524.1>
        19, 96, 97, "scan", 0, "sort", 0
        -- </where7-2.524.1>
    })

test:do_test(
    "where7-2.524.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR b=209
         OR b=399
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
  ]])
    end, {
        -- <where7-2.524.2>
        19, 96, 97, "scan", 0, "sort", 0
        -- </where7-2.524.2>
    })

test:do_test(
    "where7-2.525.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 18 AND 20) AND a!=19)
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR b=597
         OR a=95
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR b=432
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.525.1>
        18, 20, 24, 38, 50, 55, 76, 92, 95, "scan", 0, "sort", 0
        -- </where7-2.525.1>
    })

test:do_test(
    "where7-2.525.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 18 AND 20) AND a!=19)
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR b=597
         OR a=95
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR b=432
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.525.2>
        18, 20, 24, 38, 50, 55, 76, 92, 95, "scan", 0, "sort", 0
        -- </where7-2.525.2>
    })

test:do_test(
    "where7-2.526.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR b=157
         OR ((a BETWEEN 78 AND 80) AND a!=79)
         OR a=3
         OR b=663
         OR a=2
         OR c=21021
         OR b=330
         OR b=231
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.526.1>
        2, 3, 21, 27, 30, 61, 62, 63, 78, 80, 88, "scan", 0, "sort", 0
        -- </where7-2.526.1>
    })

test:do_test(
    "where7-2.526.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR b=157
         OR ((a BETWEEN 78 AND 80) AND a!=79)
         OR a=3
         OR b=663
         OR a=2
         OR c=21021
         OR b=330
         OR b=231
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
  ]])
    end, {
        -- <where7-2.526.2>
        2, 3, 21, 27, 30, 61, 62, 63, 78, 80, 88, "scan", 0, "sort", 0
        -- </where7-2.526.2>
    })

test:do_test(
    "where7-2.527.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'fghij*')
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR f IS NULL
  ]])
    end, {
        -- <where7-2.527.1>
        64, 66, 83, "scan", 0, "sort", 0
        -- </where7-2.527.1>
    })

test:do_test(
    "where7-2.527.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'fghij*')
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR f IS NULL
  ]])
    end, {
        -- <where7-2.527.2>
        64, 66, 83, "scan", 0, "sort", 0
        -- </where7-2.527.2>
    })

test:do_test(
    "where7-2.528.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 99 AND 101) AND a!=100)
         OR (g='fedcbaz' AND f GLOB 'pqrst*')
         OR 1000000<b
         OR (g='jihgfed' AND f GLOB 'xyzab*')
         OR b=990
  ]])
    end, {
        -- <where7-2.528.1>
        75, 90, 93, 99, "scan", 0, "sort", 0
        -- </where7-2.528.1>
    })

test:do_test(
    "where7-2.528.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 99 AND 101) AND a!=100)
         OR (g='fedcbaz' AND f GLOB 'pqrst*')
         OR 1000000<b
         OR (g='jihgfed' AND f GLOB 'xyzab*')
         OR b=990
  ]])
    end, {
        -- <where7-2.528.2>
        75, 90, 93, 99, "scan", 0, "sort", 0
        -- </where7-2.528.2>
    })

test:do_test(
    "where7-2.529.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=165
         OR a=69
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
  ]])
    end, {
        -- <where7-2.529.1>
        15, 44, 69, "scan", 0, "sort", 0
        -- </where7-2.529.1>
    })

test:do_test(
    "where7-2.529.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=165
         OR a=69
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
  ]])
    end, {
        -- <where7-2.529.2>
        15, 44, 69, "scan", 0, "sort", 0
        -- </where7-2.529.2>
    })

test:do_test(
    "where7-2.530.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='nmlkjih' AND f GLOB 'defgh*')
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR b=784
         OR b=583
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR b=814
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR b=619
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.530.1>
        46, 53, 54, 55, 58, 74, 80, "scan", 0, "sort", 0
        -- </where7-2.530.1>
    })

test:do_test(
    "where7-2.530.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='nmlkjih' AND f GLOB 'defgh*')
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR b=784
         OR b=583
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR b=814
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR b=619
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.530.2>
        46, 53, 54, 55, 58, 74, 80, "scan", 0, "sort", 0
        -- </where7-2.530.2>
    })

test:do_test(
    "where7-2.531.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=86
         OR b=484
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR b=418
         OR b=509
         OR a=42
         OR b=825
         OR a=91
         OR b=1023
         OR b=814
         OR ((a BETWEEN 99 AND 101) AND a!=100)
  ]])
    end, {
        -- <where7-2.531.1>
        38, 42, 44, 74, 75, 79, 86, 91, 93, 99, "scan", 0, "sort", 0
        -- </where7-2.531.1>
    })

test:do_test(
    "where7-2.531.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=86
         OR b=484
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR b=418
         OR b=509
         OR a=42
         OR b=825
         OR a=91
         OR b=1023
         OR b=814
         OR ((a BETWEEN 99 AND 101) AND a!=100)
  ]])
    end, {
        -- <where7-2.531.2>
        38, 42, 44, 74, 75, 79, 86, 91, 93, 99, "scan", 0, "sort", 0
        -- </where7-2.531.2>
    })

test:do_test(
    "where7-2.532.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR b=231
         OR a=81
         OR a=72
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR b=396
  ]])
    end, {
        -- <where7-2.532.1>
        21, 24, 26, 36, 72, 81, 86, "scan", 0, "sort", 0
        -- </where7-2.532.1>
    })

test:do_test(
    "where7-2.532.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR b=231
         OR a=81
         OR a=72
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR b=396
  ]])
    end, {
        -- <where7-2.532.2>
        21, 24, 26, 36, 72, 81, 86, "scan", 0, "sort", 0
        -- </where7-2.532.2>
    })

test:do_test(
    "where7-2.533.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR a=63
         OR ((a BETWEEN 70 AND 72) AND a!=71)
         OR a=71
         OR b=22
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR a=53
  ]])
    end, {
        -- <where7-2.533.1>
        2, 21, 53, 59, 61, 63, 70, 71, 72, 74, 76, 78, "scan", 0, "sort", 0
        -- </where7-2.533.1>
    })

test:do_test(
    "where7-2.533.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR a=63
         OR ((a BETWEEN 70 AND 72) AND a!=71)
         OR a=71
         OR b=22
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR a=53
  ]])
    end, {
        -- <where7-2.533.2>
        2, 21, 53, 59, 61, 63, 70, 71, 72, 74, 76, 78, "scan", 0, "sort", 0
        -- </where7-2.533.2>
    })

test:do_test(
    "where7-2.534.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=861
         OR b=649
         OR b=146
         OR f='abcdefghi'
  ]])
    end, {
        -- <where7-2.534.1>
        26, 52, 59, 78, "scan", 0, "sort", 0
        -- </where7-2.534.1>
    })

test:do_test(
    "where7-2.534.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=861
         OR b=649
         OR b=146
         OR f='abcdefghi'
  ]])
    end, {
        -- <where7-2.534.2>
        26, 52, 59, 78, "scan", 0, "sort", 0
        -- </where7-2.534.2>
    })

test:do_test(
    "where7-2.535.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR c=5005
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR a=93
         OR c=24024
         OR b=619
         OR b=234
         OR b=55
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.535.1>
        5, 9, 13, 14, 15, 21, 35, 47, 50, 52, 56, 61, 70, 71, 72, 73, 87, 93, 99, "scan", 0, "sort", 0
        -- </where7-2.535.1>
    })

test:do_test(
    "where7-2.535.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR c=5005
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR a=93
         OR c=24024
         OR b=619
         OR b=234
         OR b=55
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.535.2>
        5, 9, 13, 14, 15, 21, 35, 47, 50, 52, 56, 61, 70, 71, 72, 73, 87, 93, 99, "scan", 0, "sort", 0
        -- </where7-2.535.2>
    })

test:do_test(
    "where7-2.536.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=355
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=806
         OR b=462
         OR b=531
         OR (g='lkjihgf' AND f GLOB 'lmnop*')
         OR f='mnopqrstu'
  ]])
    end, {
        -- <where7-2.536.1>
        12, 38, 42, 49, 63, 64, 69, 90, "scan", 0, "sort", 0
        -- </where7-2.536.1>
    })

test:do_test(
    "where7-2.536.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=355
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=806
         OR b=462
         OR b=531
         OR (g='lkjihgf' AND f GLOB 'lmnop*')
         OR f='mnopqrstu'
  ]])
    end, {
        -- <where7-2.536.2>
        12, 38, 42, 49, 63, 64, 69, 90, "scan", 0, "sort", 0
        -- </where7-2.536.2>
    })

test:do_test(
    "where7-2.537.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 60 AND 62) AND a!=61)
         OR f='pqrstuvwx'
         OR (g='nmlkjih' AND f GLOB 'efghi*')
         OR b=495
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR a=75
  ]])
    end, {
        -- <where7-2.537.1>
        15, 41, 45, 56, 60, 62, 67, 70, 75, 93, "scan", 0, "sort", 0
        -- </where7-2.537.1>
    })

test:do_test(
    "where7-2.537.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 60 AND 62) AND a!=61)
         OR f='pqrstuvwx'
         OR (g='nmlkjih' AND f GLOB 'efghi*')
         OR b=495
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR a=75
  ]])
    end, {
        -- <where7-2.537.2>
        15, 41, 45, 56, 60, 62, 67, 70, 75, 93, "scan", 0, "sort", 0
        -- </where7-2.537.2>
    })

test:do_test(
    "where7-2.538.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'efghi*')
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR b=748
         OR b=913
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR a=22
  ]])
    end, {
        -- <where7-2.538.1>
        4, 5, 21, 22, 68, 83, "scan", 0, "sort", 0
        -- </where7-2.538.1>
    })

test:do_test(
    "where7-2.538.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'efghi*')
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR b=748
         OR b=913
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR a=22
  ]])
    end, {
        -- <where7-2.538.2>
        4, 5, 21, 22, 68, 83, "scan", 0, "sort", 0
        -- </where7-2.538.2>
    })

test:do_test(
    "where7-2.539.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=19
         OR b=902
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR b=168
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR a=50
         OR f='uvwxyzabc'
         OR b=836
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR a=50
  ]])
    end, {
        -- <where7-2.539.1>
        19, 20, 46, 50, 63, 65, 67, 72, 76, 77, 79, 82, 98, "scan", 0, "sort", 0
        -- </where7-2.539.1>
    })

test:do_test(
    "where7-2.539.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=19
         OR b=902
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR b=168
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR a=50
         OR f='uvwxyzabc'
         OR b=836
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR a=50
  ]])
    end, {
        -- <where7-2.539.2>
        19, 20, 46, 50, 63, 65, 67, 72, 76, 77, 79, 82, 98, "scan", 0, "sort", 0
        -- </where7-2.539.2>
    })

test:do_test(
    "where7-2.540.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=12012
         OR b=993
         OR b=839
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR a=87
  ]])
    end, {
        -- <where7-2.540.1>
        30, 32, 34, 35, 36, 87, "scan", 0, "sort", 0
        -- </where7-2.540.1>
    })

test:do_test(
    "where7-2.540.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=12012
         OR b=993
         OR b=839
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR a=87
  ]])
    end, {
        -- <where7-2.540.2>
        30, 32, 34, 35, 36, 87, "scan", 0, "sort", 0
        -- </where7-2.540.2>
    })

test:do_test(
    "where7-2.541.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=814
         OR c=30030
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR a=16
         OR b=1048
         OR b=113
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR b=729
         OR a=54
  ]])
    end, {
        -- <where7-2.541.1>
        3, 16, 34, 40, 54, 61, 74, 88, 89, 90, "scan", 0, "sort", 0
        -- </where7-2.541.1>
    })

test:do_test(
    "where7-2.541.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=814
         OR c=30030
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR a=16
         OR b=1048
         OR b=113
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR b=729
         OR a=54
  ]])
    end, {
        -- <where7-2.541.2>
        3, 16, 34, 40, 54, 61, 74, 88, 89, 90, "scan", 0, "sort", 0
        -- </where7-2.541.2>
    })

test:do_test(
    "where7-2.542.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=399
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR b=814
         OR c=22022
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR a=1
         OR b=311
         OR b=121
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR b=198
  ]])
    end, {
        -- <where7-2.542.1>
        1, 6, 8, 11, 18, 32, 37, 58, 63, 64, 65, 66, 71, 74, 84, 89, "scan", 0, "sort", 0
        -- </where7-2.542.1>
    })

test:do_test(
    "where7-2.542.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=399
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR b=814
         OR c=22022
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR a=1
         OR b=311
         OR b=121
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR b=198
  ]])
    end, {
        -- <where7-2.542.2>
        1, 6, 8, 11, 18, 32, 37, 58, 63, 64, 65, 66, 71, 74, 84, 89, "scan", 0, "sort", 0
        -- </where7-2.542.2>
    })

test:do_test(
    "where7-2.543.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=146
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR a=57
  ]])
    end, {
        -- <where7-2.543.1>
        52, 57, "scan", 0, "sort", 0
        -- </where7-2.543.1>
    })

test:do_test(
    "where7-2.543.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=146
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR a=57
  ]])
    end, {
        -- <where7-2.543.2>
        52, 57, "scan", 0, "sort", 0
        -- </where7-2.543.2>
    })

test:do_test(
    "where7-2.544.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR f='fghijklmn'
         OR a=70
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.544.1>
        4, 5, 31, 57, 70, 83, 100, "scan", 0, "sort", 0
        -- </where7-2.544.1>
    })

test:do_test(
    "where7-2.544.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR f='fghijklmn'
         OR a=70
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.544.2>
        4, 5, 31, 57, 70, 83, 100, "scan", 0, "sort", 0
        -- </where7-2.544.2>
    })

test:do_test(
    "where7-2.545.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=42
         OR b=333
         OR (d>=35.0 AND d<36.0 AND d IS NOT NULL)
         OR b=1089
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR a=22
         OR b=594
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
  ]])
    end, {
        -- <where7-2.545.1>
        5, 12, 15, 22, 31, 35, 42, 54, 57, 83, 99, "scan", 0, "sort", 0
        -- </where7-2.545.1>
    })

test:do_test(
    "where7-2.545.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=42
         OR b=333
         OR (d>=35.0 AND d<36.0 AND d IS NOT NULL)
         OR b=1089
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR a=22
         OR b=594
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
  ]])
    end, {
        -- <where7-2.545.2>
        5, 12, 15, 22, 31, 35, 42, 54, 57, 83, 99, "scan", 0, "sort", 0
        -- </where7-2.545.2>
    })

test:do_test(
    "where7-2.546.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=113
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR f='mnopqrstu'
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR b=902
  ]])
    end, {
        -- <where7-2.546.1>
        3, 5, 12, 16, 17, 25, 26, 38, 52, 64, 67, 69, 78, 82, 90, "scan", 0, "sort", 0
        -- </where7-2.546.1>
    })

test:do_test(
    "where7-2.546.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=113
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR f='mnopqrstu'
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR b=902
  ]])
    end, {
        -- <where7-2.546.2>
        3, 5, 12, 16, 17, 25, 26, 38, 52, 64, 67, 69, 78, 82, 90, "scan", 0, "sort", 0
        -- </where7-2.546.2>
    })

test:do_test(
    "where7-2.547.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='onmlkji' AND f GLOB 'zabcd*')
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR a=13
  ]])
    end, {
        -- <where7-2.547.1>
        13, 15, 41, 51, 67, 93, "scan", 0, "sort", 0
        -- </where7-2.547.1>
    })

test:do_test(
    "where7-2.547.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='onmlkji' AND f GLOB 'zabcd*')
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR a=13
  ]])
    end, {
        -- <where7-2.547.2>
        13, 15, 41, 51, 67, 93, "scan", 0, "sort", 0
        -- </where7-2.547.2>
    })

test:do_test(
    "where7-2.548.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='edcbazy' AND f GLOB 'wxyza*')
         OR b=410
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR b=418
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.548.1>
        38, 65, 82, 88, 100, "scan", 0, "sort", 0
        -- </where7-2.548.1>
    })

test:do_test(
    "where7-2.548.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='edcbazy' AND f GLOB 'wxyza*')
         OR b=410
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR b=418
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.548.2>
        38, 65, 82, 88, 100, "scan", 0, "sort", 0
        -- </where7-2.548.2>
    })

test:do_test(
    "where7-2.549.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=5
         OR a=95
         OR a=56
         OR a=46
         OR (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR (d>=41.0 AND d<42.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.549.1>
        5, 10, 41, 46, 56, 61, 95, 100, "scan", 0, "sort", 0
        -- </where7-2.549.1>
    })

test:do_test(
    "where7-2.549.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=5
         OR a=95
         OR a=56
         OR a=46
         OR (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR (d>=41.0 AND d<42.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.549.2>
        5, 10, 41, 46, 56, 61, 95, 100, "scan", 0, "sort", 0
        -- </where7-2.549.2>
    })

test:do_test(
    "where7-2.550.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR a=13
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR a=9
         OR a=27
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR b=484
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR b=594
  ]])
    end, {
        -- <where7-2.550.1>
        9, 13, 27, 37, 44, 54, 75, 87, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.550.1>
    })

test:do_test(
    "where7-2.550.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR a=13
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR a=9
         OR a=27
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR b=484
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR b=594
  ]])
    end, {
        -- <where7-2.550.2>
        9, 13, 27, 37, 44, 54, 75, 87, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.550.2>
    })

test:do_test(
    "where7-2.551.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=539
         OR b=418
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR b=759
  ]])
    end, {
        -- <where7-2.551.1>
        15, 38, 49, 69, "scan", 0, "sort", 0
        -- </where7-2.551.1>
    })

test:do_test(
    "where7-2.551.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=539
         OR b=418
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR b=759
  ]])
    end, {
        -- <where7-2.551.2>
        15, 38, 49, 69, "scan", 0, "sort", 0
        -- </where7-2.551.2>
    })

test:do_test(
    "where7-2.552.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1001
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR c=34034
         OR a=84
  ]])
    end, {
        -- <where7-2.552.1>
        8, 54, 84, 91, 100, "scan", 0, "sort", 0
        -- </where7-2.552.1>
    })

test:do_test(
    "where7-2.552.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1001
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR c=34034
         OR a=84
  ]])
    end, {
        -- <where7-2.552.2>
        8, 54, 84, 91, 100, "scan", 0, "sort", 0
        -- </where7-2.552.2>
    })

test:do_test(
    "where7-2.553.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=795
         OR b=671
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR ((a BETWEEN 71 AND 73) AND a!=72)
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR b=322
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR c=34034
         OR b=410
  ]])
    end, {
        -- <where7-2.553.1>
        15, 38, 41, 60, 61, 63, 67, 71, 73, 93, 100, "scan", 0, "sort", 0
        -- </where7-2.553.1>
    })

test:do_test(
    "where7-2.553.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=795
         OR b=671
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR ((a BETWEEN 71 AND 73) AND a!=72)
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR b=322
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR c=34034
         OR b=410
  ]])
    end, {
        -- <where7-2.553.2>
        15, 38, 41, 60, 61, 63, 67, 71, 73, 93, 100, "scan", 0, "sort", 0
        -- </where7-2.553.2>
    })

test:do_test(
    "where7-2.554.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=13013
         OR (g='fedcbaz' AND f GLOB 'qrstu*')
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=47
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=828
  ]])
    end, {
        -- <where7-2.554.1>
        37, 38, 39, 42, 61, 69, 79, 94, "scan", 0, "sort", 0
        -- </where7-2.554.1>
    })

test:do_test(
    "where7-2.554.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=13013
         OR (g='fedcbaz' AND f GLOB 'qrstu*')
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=47
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=828
  ]])
    end, {
        -- <where7-2.554.2>
        37, 38, 39, 42, 61, 69, 79, 94, "scan", 0, "sort", 0
        -- </where7-2.554.2>
    })

test:do_test(
    "where7-2.555.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=451
         OR b=836
         OR (g='onmlkji' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.555.1>
        41, 48, 76, "scan", 0, "sort", 0
        -- </where7-2.555.1>
    })

test:do_test(
    "where7-2.555.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=451
         OR b=836
         OR (g='onmlkji' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.555.2>
        41, 48, 76, "scan", 0, "sort", 0
        -- </where7-2.555.2>
    })

test:do_test(
    "where7-2.556.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=575
         OR b=748
         OR b=520
         OR b=154
         OR a=70
         OR f='efghijklm'
  ]])
    end, {
        -- <where7-2.556.1>
        4, 14, 30, 56, 68, 70, 82, "scan", 0, "sort", 0
        -- </where7-2.556.1>
    })

test:do_test(
    "where7-2.556.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=575
         OR b=748
         OR b=520
         OR b=154
         OR a=70
         OR f='efghijklm'
  ]])
    end, {
        -- <where7-2.556.2>
        4, 14, 30, 56, 68, 70, 82, "scan", 0, "sort", 0
        -- </where7-2.556.2>
    })

test:do_test(
    "where7-2.557.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='tuvwxyzab'
         OR (g='nmlkjih' AND f GLOB 'efghi*')
  ]])
    end, {
        -- <where7-2.557.1>
        19, 45, 56, 71, 97, "scan", 0, "sort", 0
        -- </where7-2.557.1>
    })

test:do_test(
    "where7-2.557.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='tuvwxyzab'
         OR (g='nmlkjih' AND f GLOB 'efghi*')
  ]])
    end, {
        -- <where7-2.557.2>
        19, 45, 56, 71, 97, "scan", 0, "sort", 0
        -- </where7-2.557.2>
    })

test:do_test(
    "where7-2.558.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=806
         OR a=47
         OR d<0.0
         OR b=982
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'jklmn*')
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR e IS NULL
         OR c=32032
         OR b=795
  ]])
    end, {
        -- <where7-2.558.1>
        2, 12, 47, 87, 94, 95, 96, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.558.1>
    })

test:do_test(
    "where7-2.558.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=806
         OR a=47
         OR d<0.0
         OR b=982
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'jklmn*')
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR e IS NULL
         OR c=32032
         OR b=795
  ]])
    end, {
        -- <where7-2.558.2>
        2, 12, 47, 87, 94, 95, 96, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.558.2>
    })

test:do_test(
    "where7-2.559.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=62
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.559.1>
        23, 49, 62, 75, 89, 91, 99, "scan", 0, "sort", 0
        -- </where7-2.559.1>
    })

test:do_test(
    "where7-2.559.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=62
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.559.2>
        23, 49, 62, 75, 89, 91, 99, "scan", 0, "sort", 0
        -- </where7-2.559.2>
    })

test:do_test(
    "where7-2.560.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR b=1056
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR b=729
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR b=220
         OR b=498
         OR ((a BETWEEN 96 AND 98) AND a!=97)
  ]])
    end, {
        -- <where7-2.560.1>
        9, 20, 57, 73, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.560.1>
    })

test:do_test(
    "where7-2.560.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR b=1056
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR b=729
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR b=220
         OR b=498
         OR ((a BETWEEN 96 AND 98) AND a!=97)
  ]])
    end, {
        -- <where7-2.560.2>
        9, 20, 57, 73, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.560.2>
    })

test:do_test(
    "where7-2.561.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=44
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR (g='wvutsrq' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.561.1>
        4, 10, 38, "scan", 0, "sort", 0
        -- </where7-2.561.1>
    })

test:do_test(
    "where7-2.561.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=44
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR (g='wvutsrq' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.561.2>
        4, 10, 38, "scan", 0, "sort", 0
        -- </where7-2.561.2>
    })

test:do_test(
    "where7-2.562.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=883
         OR b=311
         OR b=880
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR a=88
         OR b=154
         OR a=94
         OR a=37
         OR c=31031
  ]])
    end, {
        -- <where7-2.562.1>
        14, 37, 41, 57, 59, 80, 88, 91, 92, 93, 94, "scan", 0, "sort", 0
        -- </where7-2.562.1>
    })

test:do_test(
    "where7-2.562.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=883
         OR b=311
         OR b=880
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR a=88
         OR b=154
         OR a=94
         OR a=37
         OR c=31031
  ]])
    end, {
        -- <where7-2.562.2>
        14, 37, 41, 57, 59, 80, 88, 91, 92, 93, 94, "scan", 0, "sort", 0
        -- </where7-2.562.2>
    })

test:do_test(
    "where7-2.563.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='onmlkji' AND f GLOB 'xyzab*')
         OR a=10
         OR b=190
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR b=385
         OR a=82
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR c=22022
  ]])
    end, {
        -- <where7-2.563.1>
        8, 10, 35, 49, 55, 63, 64, 65, 66, 67, 69, 82, 90, "scan", 0, "sort", 0
        -- </where7-2.563.1>
    })

test:do_test(
    "where7-2.563.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='onmlkji' AND f GLOB 'xyzab*')
         OR a=10
         OR b=190
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR b=385
         OR a=82
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR c=22022
  ]])
    end, {
        -- <where7-2.563.2>
        8, 10, 35, 49, 55, 63, 64, 65, 66, 67, 69, 82, 90, "scan", 0, "sort", 0
        -- </where7-2.563.2>
    })

test:do_test(
    "where7-2.564.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1070
         OR a=33
         OR b=363
         OR a=47
  ]])
    end, {
        -- <where7-2.564.1>
        33, 47, "scan", 0, "sort", 0
        -- </where7-2.564.1>
    })

test:do_test(
    "where7-2.564.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1070
         OR a=33
         OR b=363
         OR a=47
  ]])
    end, {
        -- <where7-2.564.2>
        33, 47, "scan", 0, "sort", 0
        -- </where7-2.564.2>
    })

test:do_test(
    "where7-2.565.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=1001
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=49
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR c=33033
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR g IS NULL
         OR b=220
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.565.1>
        1, 2, 3, 18, 20, 33, 35, 49, 60, 62, 63, 65, 70, 81, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.565.1>
    })

test:do_test(
    "where7-2.565.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=1001
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=49
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR c=33033
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR g IS NULL
         OR b=220
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.565.2>
        1, 2, 3, 18, 20, 33, 35, 49, 60, 62, 63, 65, 70, 81, 97, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.565.2>
    })

test:do_test(
    "where7-2.566.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR b=212
         OR b=418
         OR ((a BETWEEN 31 AND 33) AND a!=32)
  ]])
    end, {
        -- <where7-2.566.1>
        31, 33, 38, 71, "scan", 0, "sort", 0
        -- </where7-2.566.1>
    })

test:do_test(
    "where7-2.566.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR b=212
         OR b=418
         OR ((a BETWEEN 31 AND 33) AND a!=32)
  ]])
    end, {
        -- <where7-2.566.2>
        31, 33, 38, 71, "scan", 0, "sort", 0
        -- </where7-2.566.2>
    })

test:do_test(
    "where7-2.567.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=344
         OR f='nopqrstuv'
         OR b=704
         OR a=84
  ]])
    end, {
        -- <where7-2.567.1>
        13, 39, 64, 65, 84, 91, "scan", 0, "sort", 0
        -- </where7-2.567.1>
    })

test:do_test(
    "where7-2.567.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=344
         OR f='nopqrstuv'
         OR b=704
         OR a=84
  ]])
    end, {
        -- <where7-2.567.2>
        13, 39, 64, 65, 84, 91, "scan", 0, "sort", 0
        -- </where7-2.567.2>
    })

test:do_test(
    "where7-2.568.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 30 AND 32) AND a!=31)
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.568.1>
        5, 30, 32, "scan", 0, "sort", 0
        -- </where7-2.568.1>
    })

test:do_test(
    "where7-2.568.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 30 AND 32) AND a!=31)
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.568.2>
        5, 30, 32, "scan", 0, "sort", 0
        -- </where7-2.568.2>
    })

test:do_test(
    "where7-2.569.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'jklmn*')
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
  ]])
    end, {
        -- <where7-2.569.1>
        26, 52, 78, 87, "scan", 0, "sort", 0
        -- </where7-2.569.1>
    })

test:do_test(
    "where7-2.569.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'jklmn*')
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
  ]])
    end, {
        -- <where7-2.569.2>
        26, 52, 78, 87, "scan", 0, "sort", 0
        -- </where7-2.569.2>
    })

test:do_test(
    "where7-2.570.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 0 AND 2) AND a!=1)
         OR b=1100
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR b=421
         OR b=465
         OR b=894
         OR c=13013
         OR b=47
         OR b=674
         OR ((a BETWEEN 0 AND 2) AND a!=1)
  ]])
    end, {
        -- <where7-2.570.1>
        2, 37, 38, 39, 70, 100, "scan", 0, "sort", 0
        -- </where7-2.570.1>
    })

test:do_test(
    "where7-2.570.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 0 AND 2) AND a!=1)
         OR b=1100
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR b=421
         OR b=465
         OR b=894
         OR c=13013
         OR b=47
         OR b=674
         OR ((a BETWEEN 0 AND 2) AND a!=1)
  ]])
    end, {
        -- <where7-2.570.2>
        2, 37, 38, 39, 70, 100, "scan", 0, "sort", 0
        -- </where7-2.570.2>
    })

test:do_test(
    "where7-2.571.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=18018
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR b=410
         OR b=858
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.571.1>
        49, 52, 53, 54, 78, "scan", 0, "sort", 0
        -- </where7-2.571.1>
    })

test:do_test(
    "where7-2.571.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=18018
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR b=410
         OR b=858
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.571.2>
        49, 52, 53, 54, 78, "scan", 0, "sort", 0
        -- </where7-2.571.2>
    })

test:do_test(
    "where7-2.572.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR b=781
  ]])
    end, {
        -- <where7-2.572.1>
        47, 71, "scan", 0, "sort", 0
        -- </where7-2.572.1>
    })

test:do_test(
    "where7-2.572.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR b=781
  ]])
    end, {
        -- <where7-2.572.2>
        47, 71, "scan", 0, "sort", 0
        -- </where7-2.572.2>
    })

test:do_test(
    "where7-2.573.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1070
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR a=54
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR a=9
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.573.1>
        7, 9, 25, 33, 47, 50, 52, 54, 59, 63, 85, "scan", 0, "sort", 0
        -- </where7-2.573.1>
    })

test:do_test(
    "where7-2.573.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1070
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR a=54
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR a=9
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.573.2>
        7, 9, 25, 33, 47, 50, 52, 54, 59, 63, 85, "scan", 0, "sort", 0
        -- </where7-2.573.2>
    })

test:do_test(
    "where7-2.574.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=55
         OR a=62
         OR a=63
         OR (g='onmlkji' AND f GLOB 'yzabc*')
         OR (g='rqponml' AND f GLOB 'ijklm*')
         OR ((a BETWEEN 99 AND 101) AND a!=100)
  ]])
    end, {
        -- <where7-2.574.1>
        34, 50, 55, 62, 63, 99, "scan", 0, "sort", 0
        -- </where7-2.574.1>
    })

test:do_test(
    "where7-2.574.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=55
         OR a=62
         OR a=63
         OR (g='onmlkji' AND f GLOB 'yzabc*')
         OR (g='rqponml' AND f GLOB 'ijklm*')
         OR ((a BETWEEN 99 AND 101) AND a!=100)
  ]])
    end, {
        -- <where7-2.574.2>
        34, 50, 55, 62, 63, 99, "scan", 0, "sort", 0
        -- </where7-2.574.2>
    })

test:do_test(
    "where7-2.575.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=421
         OR b=146
         OR b=22
         OR f='efghijklm'
  ]])
    end, {
        -- <where7-2.575.1>
        2, 4, 30, 56, 82, "scan", 0, "sort", 0
        -- </where7-2.575.1>
    })

test:do_test(
    "where7-2.575.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=421
         OR b=146
         OR b=22
         OR f='efghijklm'
  ]])
    end, {
        -- <where7-2.575.2>
        2, 4, 30, 56, 82, "scan", 0, "sort", 0
        -- </where7-2.575.2>
    })

test:do_test(
    "where7-2.576.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=553
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR (d>=59.0 AND d<60.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=583
         OR a=56
  ]])
    end, {
        -- <where7-2.576.1>
        21, 23, 48, 53, 56, 59, 61, "scan", 0, "sort", 0
        -- </where7-2.576.1>
    })

test:do_test(
    "where7-2.576.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=553
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR (d>=59.0 AND d<60.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=583
         OR a=56
  ]])
    end, {
        -- <where7-2.576.2>
        21, 23, 48, 53, 56, 59, 61, "scan", 0, "sort", 0
        -- </where7-2.576.2>
    })

test:do_test(
    "where7-2.577.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=83
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR a=1
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR b=245
  ]])
    end, {
        -- <where7-2.577.1>
        1, 17, 19, 29, 49, 51, 77, 83, "scan", 0, "sort", 0
        -- </where7-2.577.1>
    })

test:do_test(
    "where7-2.577.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=83
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR a=1
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR b=245
  ]])
    end, {
        -- <where7-2.577.2>
        1, 17, 19, 29, 49, 51, 77, 83, "scan", 0, "sort", 0
        -- </where7-2.577.2>
    })

test:do_test(
    "where7-2.578.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=3003
         OR b=619
         OR ((a BETWEEN 19 AND 21) AND a!=20)
  ]])
    end, {
        -- <where7-2.578.1>
        7, 8, 9, 19, 21, "scan", 0, "sort", 0
        -- </where7-2.578.1>
    })

test:do_test(
    "where7-2.578.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=3003
         OR b=619
         OR ((a BETWEEN 19 AND 21) AND a!=20)
  ]])
    end, {
        -- <where7-2.578.2>
        7, 8, 9, 19, 21, "scan", 0, "sort", 0
        -- </where7-2.578.2>
    })

test:do_test(
    "where7-2.579.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=924
         OR a=92
         OR a=63
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
  ]])
    end, {
        -- <where7-2.579.1>
        60, 63, 84, 92, 97, "scan", 0, "sort", 0
        -- </where7-2.579.1>
    })

test:do_test(
    "where7-2.579.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=924
         OR a=92
         OR a=63
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
  ]])
    end, {
        -- <where7-2.579.2>
        60, 63, 84, 92, 97, "scan", 0, "sort", 0
        -- </where7-2.579.2>
    })

test:do_test(
    "where7-2.580.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=440
         OR f='vwxyzabcd'
         OR b=190
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR b=88
         OR b=58
  ]])
    end, {
        -- <where7-2.580.1>
        8, 11, 21, 37, 40, 42, 47, 63, 73, 89, 99, "scan", 0, "sort", 0
        -- </where7-2.580.1>
    })

test:do_test(
    "where7-2.580.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=440
         OR f='vwxyzabcd'
         OR b=190
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR b=88
         OR b=58
  ]])
    end, {
        -- <where7-2.580.2>
        8, 11, 21, 37, 40, 42, 47, 63, 73, 89, 99, "scan", 0, "sort", 0
        -- </where7-2.580.2>
    })

test:do_test(
    "where7-2.581.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=495
         OR c=24024
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR b=1001
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR d>1e10
         OR b=531
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR b=1089
  ]])
    end, {
        -- <where7-2.581.1>
        25, 45, 49, 70, 71, 72, 82, 91, 99, "scan", 0, "sort", 0
        -- </where7-2.581.1>
    })

test:do_test(
    "where7-2.581.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=495
         OR c=24024
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR b=1001
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR d>1e10
         OR b=531
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR b=1089
  ]])
    end, {
        -- <where7-2.581.2>
        25, 45, 49, 70, 71, 72, 82, 91, 99, "scan", 0, "sort", 0
        -- </where7-2.581.2>
    })

test:do_test(
    "where7-2.582.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR (d>=41.0 AND d<42.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.582.1>
        41, 48, "scan", 0, "sort", 0
        -- </where7-2.582.1>
    })

test:do_test(
    "where7-2.582.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR (d>=41.0 AND d<42.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.582.2>
        41, 48, "scan", 0, "sort", 0
        -- </where7-2.582.2>
    })

test:do_test(
    "where7-2.583.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR d>1e10
         OR b=22
         OR c=5005
         OR ((a BETWEEN 22 AND 24) AND a!=23)
  ]])
    end, {
        -- <where7-2.583.1>
        1, 2, 13, 14, 15, 22, 24, 52, 71, 73, 80, 82, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.583.1>
    })

test:do_test(
    "where7-2.583.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR d>1e10
         OR b=22
         OR c=5005
         OR ((a BETWEEN 22 AND 24) AND a!=23)
  ]])
    end, {
        -- <where7-2.583.2>
        1, 2, 13, 14, 15, 22, 24, 52, 71, 73, 80, 82, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.583.2>
    })

test:do_test(
    "where7-2.584.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 9 AND 11) AND a!=10)
         OR b=1078
         OR b=806
         OR b=605
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
  ]])
    end, {
        -- <where7-2.584.1>
        9, 11, 15, 23, 25, 41, 55, 67, 76, 93, 98, "scan", 0, "sort", 0
        -- </where7-2.584.1>
    })

test:do_test(
    "where7-2.584.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 9 AND 11) AND a!=10)
         OR b=1078
         OR b=806
         OR b=605
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
  ]])
    end, {
        -- <where7-2.584.2>
        9, 11, 15, 23, 25, 41, 55, 67, 76, 93, 98, "scan", 0, "sort", 0
        -- </where7-2.584.2>
    })

test:do_test(
    "where7-2.585.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 84 AND 86) AND a!=85)
         OR b=572
         OR c=10010
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR a=29
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
  ]])
    end, {
        -- <where7-2.585.1>
        7, 28, 29, 30, 33, 52, 59, 68, 84, 85, 86, "scan", 0, "sort", 0
        -- </where7-2.585.1>
    })

test:do_test(
    "where7-2.585.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 84 AND 86) AND a!=85)
         OR b=572
         OR c=10010
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR a=29
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
  ]])
    end, {
        -- <where7-2.585.2>
        7, 28, 29, 30, 33, 52, 59, 68, 84, 85, 86, "scan", 0, "sort", 0
        -- </where7-2.585.2>
    })

test:do_test(
    "where7-2.586.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 94 AND 96) AND a!=95)
         OR b=858
         OR b=806
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.586.1>
        63, 77, 78, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.586.1>
    })

test:do_test(
    "where7-2.586.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 94 AND 96) AND a!=95)
         OR b=858
         OR b=806
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.586.2>
        63, 77, 78, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.586.2>
    })

test:do_test(
    "where7-2.587.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='vwxyzabcd'
         OR a=72
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR b=935
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR c=13013
  ]])
    end, {
        -- <where7-2.587.1>
        21, 36, 37, 38, 39, 40, 47, 72, 73, 85, 99, "scan", 0, "sort", 0
        -- </where7-2.587.1>
    })

test:do_test(
    "where7-2.587.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='vwxyzabcd'
         OR a=72
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR b=935
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR c=13013
  ]])
    end, {
        -- <where7-2.587.2>
        21, 36, 37, 38, 39, 40, 47, 72, 73, 85, 99, "scan", 0, "sort", 0
        -- </where7-2.587.2>
    })

test:do_test(
    "where7-2.588.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=5005
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR b=143
         OR a=68
         OR a=77
         OR b=80
  ]])
    end, {
        -- <where7-2.588.1>
        13, 14, 15, 43, 44, 68, 77, 88, "scan", 0, "sort", 0
        -- </where7-2.588.1>
    })

test:do_test(
    "where7-2.588.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=5005
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR b=143
         OR a=68
         OR a=77
         OR b=80
  ]])
    end, {
        -- <where7-2.588.2>
        13, 14, 15, 43, 44, 68, 77, 88, "scan", 0, "sort", 0
        -- </where7-2.588.2>
    })

test:do_test(
    "where7-2.589.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR a=99
         OR ((a BETWEEN 12 AND 14) AND a!=13)
  ]])
    end, {
        -- <where7-2.589.1>
        5, 6, 12, 14, 68, 72, 76, 78, 99, "scan", 0, "sort", 0
        -- </where7-2.589.1>
    })

test:do_test(
    "where7-2.589.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'ghijk*')
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR a=99
         OR ((a BETWEEN 12 AND 14) AND a!=13)
  ]])
    end, {
        -- <where7-2.589.2>
        5, 6, 12, 14, 68, 72, 76, 78, 99, "scan", 0, "sort", 0
        -- </where7-2.589.2>
    })

test:do_test(
    "where7-2.590.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='qponmlk' AND f GLOB 'opqrs*')
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR b=971
         OR (g='xwvutsr' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.590.1>
        5, 13, 40, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.590.1>
    })

test:do_test(
    "where7-2.590.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='qponmlk' AND f GLOB 'opqrs*')
         OR ((a BETWEEN 88 AND 90) AND a!=89)
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR b=971
         OR (g='xwvutsr' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.590.2>
        5, 13, 40, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.590.2>
    })

test:do_test(
    "where7-2.591.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?lmno*' AND f GLOB 'klmn*')
         OR b=806
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR b=1015
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
  ]])
    end, {
        -- <where7-2.591.1>
        10, 13, 36, 39, 43, 62, 65, 68, 70, 88, 91, "scan", 0, "sort", 0
        -- </where7-2.591.1>
    })

test:do_test(
    "where7-2.591.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?lmno*' AND f GLOB 'klmn*')
         OR b=806
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR b=1015
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
  ]])
    end, {
        -- <where7-2.591.2>
        10, 13, 36, 39, 43, 62, 65, 68, 70, 88, 91, "scan", 0, "sort", 0
        -- </where7-2.591.2>
    })

test:do_test(
    "where7-2.592.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='nopqrstuv'
         OR b=993
         OR a=76
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR c=20020
         OR a=26
         OR b=1048
         OR b=561
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR a=56
  ]])
    end, {
        -- <where7-2.592.1>
        13, 26, 36, 39, 51, 55, 56, 57, 58, 59, 60, 65, 76, 79, 91, "scan", 0, "sort", 0
        -- </where7-2.592.1>
    })

test:do_test(
    "where7-2.592.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='nopqrstuv'
         OR b=993
         OR a=76
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR c=20020
         OR a=26
         OR b=1048
         OR b=561
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR ((a BETWEEN 55 AND 57) AND a!=56)
         OR a=56
  ]])
    end, {
        -- <where7-2.592.2>
        13, 26, 36, 39, 51, 55, 56, 57, 58, 59, 60, 65, 76, 79, 91, "scan", 0, "sort", 0
        -- </where7-2.592.2>
    })

test:do_test(
    "where7-2.593.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=781
         OR b=671
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR b=113
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=385
         OR (g='hgfedcb' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.593.1>
        15, 16, 35, 39, 41, 60, 61, 71, 83, "scan", 0, "sort", 0
        -- </where7-2.593.1>
    })

test:do_test(
    "where7-2.593.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=781
         OR b=671
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR b=113
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=385
         OR (g='hgfedcb' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.593.2>
        15, 16, 35, 39, 41, 60, 61, 71, 83, "scan", 0, "sort", 0
        -- </where7-2.593.2>
    })

test:do_test(
    "where7-2.594.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=410
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR b=674
         OR b=825
         OR b=704
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR a=76
         OR c=32032
         OR ((a BETWEEN 43 AND 45) AND a!=44)
  ]])
    end, {
        -- <where7-2.594.1>
        9, 43, 45, 58, 60, 61, 64, 75, 76, 85, 87, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.594.1>
    })

test:do_test(
    "where7-2.594.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=410
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR b=674
         OR b=825
         OR b=704
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR a=76
         OR c=32032
         OR ((a BETWEEN 43 AND 45) AND a!=44)
  ]])
    end, {
        -- <where7-2.594.2>
        9, 43, 45, 58, 60, 61, 64, 75, 76, 85, 87, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.594.2>
    })

test:do_test(
    "where7-2.595.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=869
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.595.1>
        43, 79, "scan", 0, "sort", 0
        -- </where7-2.595.1>
    })

test:do_test(
    "where7-2.595.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=869
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.595.2>
        43, 79, "scan", 0, "sort", 0
        -- </where7-2.595.2>
    })

test:do_test(
    "where7-2.596.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=869
         OR a=34
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.596.1>
        34, 79, 87, "scan", 0, "sort", 0
        -- </where7-2.596.1>
    })

test:do_test(
    "where7-2.596.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=869
         OR a=34
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.596.2>
        34, 79, 87, "scan", 0, "sort", 0
        -- </where7-2.596.2>
    })

test:do_test(
    "where7-2.597.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='rqponml' AND f GLOB 'hijkl*')
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR a=8
         OR a=72
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.597.1>
        8, 33, 44, 72, 90, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.597.1>
    })

test:do_test(
    "where7-2.597.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='rqponml' AND f GLOB 'hijkl*')
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR a=8
         OR a=72
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.597.2>
        8, 33, 44, 72, 90, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.597.2>
    })

test:do_test(
    "where7-2.598.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=20
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR b=341
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR b=814
         OR b=1026
         OR a=14
         OR a=13
         OR b=1037
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.598.1>
        13, 14, 20, 26, 31, 56, 58, 74, 76, "scan", 0, "sort", 0
        -- </where7-2.598.1>
    })

test:do_test(
    "where7-2.598.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=20
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR b=341
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR b=814
         OR b=1026
         OR a=14
         OR a=13
         OR b=1037
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.598.2>
        13, 14, 20, 26, 31, 56, 58, 74, 76, "scan", 0, "sort", 0
        -- </where7-2.598.2>
    })

test:do_test(
    "where7-2.599.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=443
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR b=839
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (g='onmlkji' AND f GLOB 'xyzab*')
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
         OR c=7007
  ]])
    end, {
        -- <where7-2.599.1>
        10, 13, 19, 20, 21, 49, 51, "scan", 0, "sort", 0
        -- </where7-2.599.1>
    })

test:do_test(
    "where7-2.599.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=443
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR b=839
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (g='onmlkji' AND f GLOB 'xyzab*')
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
         OR c=7007
  ]])
    end, {
        -- <where7-2.599.2>
        10, 13, 19, 20, 21, 49, 51, "scan", 0, "sort", 0
        -- </where7-2.599.2>
    })

test:do_test(
    "where7-2.600.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR a=21
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR f='zabcdefgh'
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR b=506
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR b=88
         OR b=190
  ]])
    end, {
        -- <where7-2.600.1>
        3, 8, 9, 14, 16, 21, 25, 42, 46, 51, 68, 77, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.600.1>
    })

test:do_test(
    "where7-2.600.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR a=21
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR f='zabcdefgh'
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR b=506
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR b=88
         OR b=190
  ]])
    end, {
        -- <where7-2.600.2>
        3, 8, 9, 14, 16, 21, 25, 42, 46, 51, 68, 77, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.600.2>
    })

test:do_test(
    "where7-2.601.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=41.0 AND d<42.0 AND d IS NOT NULL)
         OR f='bcdefghij'
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=762
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'yzabc*')
  ]])
    end, {
        -- <where7-2.601.1>
        1, 27, 30, 41, 53, 54, 61, 63, 68, 70, 76, 79, "scan", 0, "sort", 0
        -- </where7-2.601.1>
    })

test:do_test(
    "where7-2.601.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=41.0 AND d<42.0 AND d IS NOT NULL)
         OR f='bcdefghij'
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=762
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'yzabc*')
  ]])
    end, {
        -- <where7-2.601.2>
        1, 27, 30, 41, 53, 54, 61, 63, 68, 70, 76, 79, "scan", 0, "sort", 0
        -- </where7-2.601.2>
    })

test:do_test(
    "where7-2.602.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR f='qrstuvwxy'
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.602.1>
        16, 42, 56, 68, 85, 94, "scan", 0, "sort", 0
        -- </where7-2.602.1>
    })

test:do_test(
    "where7-2.602.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR f='qrstuvwxy'
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.602.2>
        16, 42, 56, 68, 85, 94, "scan", 0, "sort", 0
        -- </where7-2.602.2>
    })

test:do_test(
    "where7-2.603.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR a=21
         OR b<0
         OR f='bcdefghij'
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.603.1>
        1, 14, 16, 21, 27, 53, 57, 79, 89, "scan", 0, "sort", 0
        -- </where7-2.603.1>
    })

test:do_test(
    "where7-2.603.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR a=21
         OR b<0
         OR f='bcdefghij'
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.603.2>
        1, 14, 16, 21, 27, 53, 57, 79, 89, "scan", 0, "sort", 0
        -- </where7-2.603.2>
    })

test:do_test(
    "where7-2.604.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'fghij*')
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR b=1067
         OR b=231
         OR b=113
         OR b=22
         OR a=55
         OR b=663
  ]])
    end, {
        -- <where7-2.604.1>
        2, 21, 40, 55, 83, 97, "scan", 0, "sort", 0
        -- </where7-2.604.1>
    })

test:do_test(
    "where7-2.604.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'fghij*')
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR b=1067
         OR b=231
         OR b=113
         OR b=22
         OR a=55
         OR b=663
  ]])
    end, {
        -- <where7-2.604.2>
        2, 21, 40, 55, 83, 97, "scan", 0, "sort", 0
        -- </where7-2.604.2>
    })

test:do_test(
    "where7-2.605.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=1
         OR b=454
         OR b=396
         OR b=1059
         OR a=69
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR b=440
         OR b=825
  ]])
    end, {
        -- <where7-2.605.1>
        1, 21, 36, 40, 47, 69, 73, 75, 99, "scan", 0, "sort", 0
        -- </where7-2.605.1>
    })

test:do_test(
    "where7-2.605.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=1
         OR b=454
         OR b=396
         OR b=1059
         OR a=69
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR b=440
         OR b=825
  ]])
    end, {
        -- <where7-2.605.2>
        1, 21, 36, 40, 47, 69, 73, 75, 99, "scan", 0, "sort", 0
        -- </where7-2.605.2>
    })

test:do_test(
    "where7-2.606.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR b=308
         OR c<=10
         OR (g='xwvutsr' AND f GLOB 'hijkl*')
         OR f='ghijklmno'
         OR b=289
         OR a=5
         OR b=267
         OR b=949
         OR ((a BETWEEN 7 AND 9) AND a!=8)
  ]])
    end, {
        -- <where7-2.606.1>
        5, 6, 7, 9, 26, 28, 32, 58, 84, "scan", 0, "sort", 0
        -- </where7-2.606.1>
    })

test:do_test(
    "where7-2.606.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR b=308
         OR c<=10
         OR (g='xwvutsr' AND f GLOB 'hijkl*')
         OR f='ghijklmno'
         OR b=289
         OR a=5
         OR b=267
         OR b=949
         OR ((a BETWEEN 7 AND 9) AND a!=8)
  ]])
    end, {
        -- <where7-2.606.2>
        5, 6, 7, 9, 26, 28, 32, 58, 84, "scan", 0, "sort", 0
        -- </where7-2.606.2>
    })

test:do_test(
    "where7-2.607.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 95 AND 97) AND a!=96)
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR b=993
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR b=663
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR b=869
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR b=121
  ]])
    end, {
        -- <where7-2.607.1>
        11, 17, 24, 43, 45, 50, 76, 79, 81, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.607.1>
    })

test:do_test(
    "where7-2.607.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 95 AND 97) AND a!=96)
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR b=993
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR b=663
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR b=869
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR b=121
  ]])
    end, {
        -- <where7-2.607.2>
        11, 17, 24, 43, 45, 50, 76, 79, 81, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.607.2>
    })

test:do_test(
    "where7-2.608.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'efghi*')
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR (g='hgfedcb' AND f GLOB 'jklmn*')
         OR b=770
  ]])
    end, {
        -- <where7-2.608.1>
        4, 27, 70, 87, "scan", 0, "sort", 0
        -- </where7-2.608.1>
    })

test:do_test(
    "where7-2.608.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'efghi*')
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR (g='hgfedcb' AND f GLOB 'jklmn*')
         OR b=770
  ]])
    end, {
        -- <where7-2.608.2>
        4, 27, 70, 87, "scan", 0, "sort", 0
        -- </where7-2.608.2>
    })

test:do_test(
    "where7-2.609.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 80 AND 82) AND a!=81)
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR (g='gfedcba' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.609.1>
        19, 45, 57, 71, 80, 82, 90, 97, "scan", 0, "sort", 0
        -- </where7-2.609.1>
    })

test:do_test(
    "where7-2.609.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 80 AND 82) AND a!=81)
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR (g='gfedcba' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.609.2>
        19, 45, 57, 71, 80, 82, 90, 97, "scan", 0, "sort", 0
        -- </where7-2.609.2>
    })

test:do_test(
    "where7-2.610.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=22
         OR c=31031
         OR b=894
         OR a=31
         OR ((a BETWEEN 84 AND 86) AND a!=85)
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR a=94
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR a=21
         OR b=1001
  ]])
    end, {
        -- <where7-2.610.1>
        2, 21, 31, 84, 86, 91, 92, 93, 94, 95, "scan", 0, "sort", 0
        -- </where7-2.610.1>
    })

test:do_test(
    "where7-2.610.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=22
         OR c=31031
         OR b=894
         OR a=31
         OR ((a BETWEEN 84 AND 86) AND a!=85)
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR a=94
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR a=21
         OR b=1001
  ]])
    end, {
        -- <where7-2.610.2>
        2, 21, 31, 84, 86, 91, 92, 93, 94, 95, "scan", 0, "sort", 0
        -- </where7-2.610.2>
    })

test:do_test(
    "where7-2.611.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='onmlkji' AND f GLOB 'zabcd*')
         OR b=1092
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR a=77
         OR a=63
         OR b=762
         OR b=894
         OR b=685
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.611.1>
        13, 46, 51, 63, 77, 80, "scan", 0, "sort", 0
        -- </where7-2.611.1>
    })

test:do_test(
    "where7-2.611.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='onmlkji' AND f GLOB 'zabcd*')
         OR b=1092
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR a=77
         OR a=63
         OR b=762
         OR b=894
         OR b=685
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.611.2>
        13, 46, 51, 63, 77, 80, "scan", 0, "sort", 0
        -- </where7-2.611.2>
    })

test:do_test(
    "where7-2.612.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='wvutsrq' AND f GLOB 'klmno*')
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR b=231
  ]])
    end, {
        -- <where7-2.612.1>
        10, 21, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.612.1>
    })

test:do_test(
    "where7-2.612.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='wvutsrq' AND f GLOB 'klmno*')
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR b=231
  ]])
    end, {
        -- <where7-2.612.2>
        10, 21, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.612.2>
    })

test:do_test(
    "where7-2.613.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=828
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR ((a BETWEEN 8 AND 10) AND a!=9)
  ]])
    end, {
        -- <where7-2.613.1>
        8, 10, 26, 52, 78, "scan", 0, "sort", 0
        -- </where7-2.613.1>
    })

test:do_test(
    "where7-2.613.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=828
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR ((a BETWEEN 8 AND 10) AND a!=9)
  ]])
    end, {
        -- <where7-2.613.2>
        8, 10, 26, 52, 78, "scan", 0, "sort", 0
        -- </where7-2.613.2>
    })

test:do_test(
    "where7-2.614.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR b=520
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR a=21
  ]])
    end, {
        -- <where7-2.614.1>
        4, 6, 13, 21, 31, 33, 39, 47, 50, 65, 91, 100, "scan", 0, "sort", 0
        -- </where7-2.614.1>
    })

test:do_test(
    "where7-2.614.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR b=520
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR a=21
  ]])
    end, {
        -- <where7-2.614.2>
        4, 6, 13, 21, 31, 33, 39, 47, 50, 65, 91, 100, "scan", 0, "sort", 0
        -- </where7-2.614.2>
    })

test:do_test(
    "where7-2.615.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=553
         OR (g='lkjihgf' AND f GLOB 'lmnop*')
         OR b=1034
         OR b=418
         OR a=57
         OR f='mnopqrstu'
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.615.1>
        12, 38, 57, 63, 64, 90, 94, 99, "scan", 0, "sort", 0
        -- </where7-2.615.1>
    })

test:do_test(
    "where7-2.615.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=553
         OR (g='lkjihgf' AND f GLOB 'lmnop*')
         OR b=1034
         OR b=418
         OR a=57
         OR f='mnopqrstu'
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.615.2>
        12, 38, 57, 63, 64, 90, 94, 99, "scan", 0, "sort", 0
        -- </where7-2.615.2>
    })

test:do_test(
    "where7-2.616.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=43
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR b=418
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR b=594
         OR a=21
         OR a=78
         OR a=91
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.616.1>
        21, 38, 43, 47, 54, 70, 78, 80, 91, "scan", 0, "sort", 0
        -- </where7-2.616.1>
    })

test:do_test(
    "where7-2.616.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=43
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR b=418
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR b=594
         OR a=21
         OR a=78
         OR a=91
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.616.2>
        21, 38, 43, 47, 54, 70, 78, 80, 91, "scan", 0, "sort", 0
        -- </where7-2.616.2>
    })

test:do_test(
    "where7-2.617.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=671
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR ((a BETWEEN 95 AND 97) AND a!=96)
  ]])
    end, {
        -- <where7-2.617.1>
        48, 61, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.617.1>
    })

test:do_test(
    "where7-2.617.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=671
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR ((a BETWEEN 95 AND 97) AND a!=96)
  ]])
    end, {
        -- <where7-2.617.2>
        48, 61, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.617.2>
    })

test:do_test(
    "where7-2.618.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR b=726
         OR b=663
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR a=25
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.618.1>
        2, 13, 16, 25, 42, 66, 68, 94, "scan", 0, "sort", 0
        -- </where7-2.618.1>
    })

test:do_test(
    "where7-2.618.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=2.0 AND d<3.0 AND d IS NOT NULL)
         OR b=726
         OR b=663
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR a=25
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.618.2>
        2, 13, 16, 25, 42, 66, 68, 94, "scan", 0, "sort", 0
        -- </where7-2.618.2>
    })

test:do_test(
    "where7-2.619.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=806
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR b=275
         OR ((a BETWEEN 80 AND 82) AND a!=81)
  ]])
    end, {
        -- <where7-2.619.1>
        10, 12, 25, 50, 80, 82, "scan", 0, "sort", 0
        -- </where7-2.619.1>
    })

test:do_test(
    "where7-2.619.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=806
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR b=275
         OR ((a BETWEEN 80 AND 82) AND a!=81)
  ]])
    end, {
        -- <where7-2.619.2>
        10, 12, 25, 50, 80, 82, "scan", 0, "sort", 0
        -- </where7-2.619.2>
    })

test:do_test(
    "where7-2.620.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=24024
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR b=429
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR b=110
         OR a=39
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
  ]])
    end, {
        -- <where7-2.620.1>
        2, 10, 23, 39, 70, 71, 72, "scan", 0, "sort", 0
        -- </where7-2.620.1>
    })

test:do_test(
    "where7-2.620.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=24024
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR b=429
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR b=110
         OR a=39
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
  ]])
    end, {
        -- <where7-2.620.2>
        2, 10, 23, 39, 70, 71, 72, "scan", 0, "sort", 0
        -- </where7-2.620.2>
    })

test:do_test(
    "where7-2.621.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=66
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR b=198
         OR b=682
         OR c=23023
  ]])
    end, {
        -- <where7-2.621.1>
        18, 62, 66, 67, 68, 69, 70, "scan", 0, "sort", 0
        -- </where7-2.621.1>
    })

test:do_test(
    "where7-2.621.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=66
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR b=198
         OR b=682
         OR c=23023
  ]])
    end, {
        -- <where7-2.621.2>
        18, 62, 66, 67, 68, 69, 70, "scan", 0, "sort", 0
        -- </where7-2.621.2>
    })

test:do_test(
    "where7-2.622.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=806
         OR b=253
         OR a=36
  ]])
    end, {
        -- <where7-2.622.1>
        23, 36, "scan", 0, "sort", 0
        -- </where7-2.622.1>
    })

test:do_test(
    "where7-2.622.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=806
         OR b=253
         OR a=36
  ]])
    end, {
        -- <where7-2.622.2>
        23, 36, "scan", 0, "sort", 0
        -- </where7-2.622.2>
    })

test:do_test(
    "where7-2.623.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=509
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
         OR b=718
         OR a=4
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.623.1>
        4, 13, 16, 22, 24, 56, 58, 69, "scan", 0, "sort", 0
        -- </where7-2.623.1>
    })

test:do_test(
    "where7-2.623.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=509
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR (g='vutsrqp' AND f GLOB 'nopqr*')
         OR b=718
         OR a=4
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.623.2>
        4, 13, 16, 22, 24, 56, 58, 69, "scan", 0, "sort", 0
        -- </where7-2.623.2>
    })

test:do_test(
    "where7-2.624.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='jihgfed' AND f GLOB 'zabcd*')
         OR b=1026
         OR a=93
         OR c=18018
  ]])
    end, {
        -- <where7-2.624.1>
        52, 53, 54, 77, 93, "scan", 0, "sort", 0
        -- </where7-2.624.1>
    })

test:do_test(
    "where7-2.624.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='jihgfed' AND f GLOB 'zabcd*')
         OR b=1026
         OR a=93
         OR c=18018
  ]])
    end, {
        -- <where7-2.624.2>
        52, 53, 54, 77, 93, "scan", 0, "sort", 0
        -- </where7-2.624.2>
    })

test:do_test(
    "where7-2.625.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=388
         OR a=44
  ]])
    end, {
        -- <where7-2.625.1>
        44, "scan", 0, "sort", 0
        -- </where7-2.625.1>
    })

test:do_test(
    "where7-2.625.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=388
         OR a=44
  ]])
    end, {
        -- <where7-2.625.2>
        44, "scan", 0, "sort", 0
        -- </where7-2.625.2>
    })

test:do_test(
    "where7-2.626.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=9009
         OR b=542
         OR f='cdefghijk'
         OR b=319
         OR b=616
  ]])
    end, {
        -- <where7-2.626.1>
        2, 25, 26, 27, 28, 29, 54, 56, 80, "scan", 0, "sort", 0
        -- </where7-2.626.1>
    })

test:do_test(
    "where7-2.626.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=9009
         OR b=542
         OR f='cdefghijk'
         OR b=319
         OR b=616
  ]])
    end, {
        -- <where7-2.626.2>
        2, 25, 26, 27, 28, 29, 54, 56, 80, "scan", 0, "sort", 0
        -- </where7-2.626.2>
    })

test:do_test(
    "where7-2.627.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=990
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR b=531
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.627.1>
        6, 16, 32, 41, 42, 43, 57, 58, 67, 68, 84, 86, 90, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.627.1>
    })

test:do_test(
    "where7-2.627.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=990
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR b=531
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.627.2>
        6, 16, 32, 41, 42, 43, 57, 58, 67, 68, 84, 86, 90, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.627.2>
    })

test:do_test(
    "where7-2.628.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=60
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR b=627
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR b=883
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR f='yzabcdefg'
         OR (d>=59.0 AND d<60.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.628.1>
        24, 38, 50, 57, 59, 60, 73, 76, 78, 93, 99, "scan", 0, "sort", 0
        -- </where7-2.628.1>
    })

test:do_test(
    "where7-2.628.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=60
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR b=627
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR b=883
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR f='yzabcdefg'
         OR (d>=59.0 AND d<60.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.628.2>
        24, 38, 50, 57, 59, 60, 73, 76, 78, 93, 99, "scan", 0, "sort", 0
        -- </where7-2.628.2>
    })

test:do_test(
    "where7-2.629.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=696
         OR b=938
         OR a=18
         OR b=957
         OR c=18018
         OR c=3003
         OR ((a BETWEEN 33 AND 35) AND a!=34)
  ]])
    end, {
        -- <where7-2.629.1>
        7, 8, 9, 18, 33, 35, 52, 53, 54, 87, "scan", 0, "sort", 0
        -- </where7-2.629.1>
    })

test:do_test(
    "where7-2.629.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=696
         OR b=938
         OR a=18
         OR b=957
         OR c=18018
         OR c=3003
         OR ((a BETWEEN 33 AND 35) AND a!=34)
  ]])
    end, {
        -- <where7-2.629.2>
        7, 8, 9, 18, 33, 35, 52, 53, 54, 87, "scan", 0, "sort", 0
        -- </where7-2.629.2>
    })

test:do_test(
    "where7-2.630.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=29029
         OR a=73
  ]])
    end, {
        -- <where7-2.630.1>
        73, 85, 86, 87, "scan", 0, "sort", 0
        -- </where7-2.630.1>
    })

test:do_test(
    "where7-2.630.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=29029
         OR a=73
  ]])
    end, {
        -- <where7-2.630.2>
        73, 85, 86, 87, "scan", 0, "sort", 0
        -- </where7-2.630.2>
    })

test:do_test(
    "where7-2.631.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=28
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR b=69
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR b=781
         OR a=64
         OR b=91
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR a=16
         OR b=278
         OR a=26
  ]])
    end, {
        -- <where7-2.631.1>
        16, 26, 27, 28, 64, 71, 82, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.631.1>
    })

test:do_test(
    "where7-2.631.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=28
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR b=69
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR b=781
         OR a=64
         OR b=91
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR a=16
         OR b=278
         OR a=26
  ]])
    end, {
        -- <where7-2.631.2>
        16, 26, 27, 28, 64, 71, 82, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.631.2>
    })

test:do_test(
    "where7-2.632.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=70
         OR c=3003
  ]])
    end, {
        -- <where7-2.632.1>
        7, 8, 9, 70, "scan", 0, "sort", 0
        -- </where7-2.632.1>
    })

test:do_test(
    "where7-2.632.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=70
         OR c=3003
  ]])
    end, {
        -- <where7-2.632.2>
        7, 8, 9, 70, "scan", 0, "sort", 0
        -- </where7-2.632.2>
    })

test:do_test(
    "where7-2.633.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=31031
         OR a=76
         OR b=1023
         OR b=33
  ]])
    end, {
        -- <where7-2.633.1>
        3, 76, 91, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.633.1>
    })

test:do_test(
    "where7-2.633.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=31031
         OR a=76
         OR b=1023
         OR b=33
  ]])
    end, {
        -- <where7-2.633.2>
        3, 76, 91, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.633.2>
    })

test:do_test(
    "where7-2.634.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR b=1001
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.634.1>
        62, 86, 91, "scan", 0, "sort", 0
        -- </where7-2.634.1>
    })

test:do_test(
    "where7-2.634.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR b=1001
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.634.2>
        62, 86, 91, "scan", 0, "sort", 0
        -- </where7-2.634.2>
    })

test:do_test(
    "where7-2.635.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='yzabcdefg'
         OR ((a BETWEEN 48 AND 50) AND a!=49)
         OR a=100
         OR (g='rqponml' AND f GLOB 'ijklm*')
         OR a=62
         OR a=67
         OR b=605
         OR c=23023
         OR a=26
         OR b=982
         OR ((a BETWEEN 3 AND 5) AND a!=4)
  ]])
    end, {
        -- <where7-2.635.1>
        3, 5, 24, 26, 34, 48, 50, 55, 62, 67, 68, 69, 76, 100, "scan", 0, "sort", 0
        -- </where7-2.635.1>
    })

test:do_test(
    "where7-2.635.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='yzabcdefg'
         OR ((a BETWEEN 48 AND 50) AND a!=49)
         OR a=100
         OR (g='rqponml' AND f GLOB 'ijklm*')
         OR a=62
         OR a=67
         OR b=605
         OR c=23023
         OR a=26
         OR b=982
         OR ((a BETWEEN 3 AND 5) AND a!=4)
  ]])
    end, {
        -- <where7-2.635.2>
        3, 5, 24, 26, 34, 48, 50, 55, 62, 67, 68, 69, 76, 100, "scan", 0, "sort", 0
        -- </where7-2.635.2>
    })

test:do_test(
    "where7-2.636.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=220
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR f IS NULL
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR b=784
  ]])
    end, {
        -- <where7-2.636.1>
        20, 24, 25, 26, 27, "scan", 0, "sort", 0
        -- </where7-2.636.1>
    })

test:do_test(
    "where7-2.636.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=220
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR f IS NULL
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR b=784
  ]])
    end, {
        -- <where7-2.636.2>
        20, 24, 25, 26, 27, "scan", 0, "sort", 0
        -- </where7-2.636.2>
    })

test:do_test(
    "where7-2.637.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR b=751
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR a=67
         OR b=102
  ]])
    end, {
        -- <where7-2.637.1>
        10, 17, 43, 67, 69, 88, 95, "scan", 0, "sort", 0
        -- </where7-2.637.1>
    })

test:do_test(
    "where7-2.637.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR b=751
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR a=67
         OR b=102
  ]])
    end, {
        -- <where7-2.637.2>
        10, 17, 43, 67, 69, 88, 95, "scan", 0, "sort", 0
        -- </where7-2.637.2>
    })

test:do_test(
    "where7-2.638.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR b=256
         OR c=7007
         OR c=26026
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR a=66
  ]])
    end, {
        -- <where7-2.638.1>
        19, 20, 21, 66, 69, 74, 76, 77, 78, 80, 82, "scan", 0, "sort", 0
        -- </where7-2.638.1>
    })

test:do_test(
    "where7-2.638.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR b=256
         OR c=7007
         OR c=26026
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR a=66
  ]])
    end, {
        -- <where7-2.638.2>
        19, 20, 21, 66, 69, 74, 76, 77, 78, 80, 82, "scan", 0, "sort", 0
        -- </where7-2.638.2>
    })

test:do_test(
    "where7-2.639.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=2002
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR b=33
         OR b=817
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.639.1>
        3, 4, 5, 6, 8, 10, 21, 34, 41, 43, 45, 60, 81, 86, "scan", 0, "sort", 0
        -- </where7-2.639.1>
    })

test:do_test(
    "where7-2.639.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=2002
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR b=33
         OR b=817
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.639.2>
        3, 4, 5, 6, 8, 10, 21, 34, 41, 43, 45, 60, 81, 86, "scan", 0, "sort", 0
        -- </where7-2.639.2>
    })

test:do_test(
    "where7-2.640.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='srqponm' AND f GLOB 'cdefg*')
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR a=80
         OR a=53
         OR a=62
         OR a=49
         OR a=53
         OR a=56
         OR (d>=83.0 AND d<84.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.640.1>
        28, 49, 53, 56, 62, 80, 81, 83, "scan", 0, "sort", 0
        -- </where7-2.640.1>
    })

test:do_test(
    "where7-2.640.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='srqponm' AND f GLOB 'cdefg*')
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR a=80
         OR a=53
         OR a=62
         OR a=49
         OR a=53
         OR a=56
         OR (d>=83.0 AND d<84.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.640.2>
        28, 49, 53, 56, 62, 80, 81, 83, "scan", 0, "sort", 0
        -- </where7-2.640.2>
    })

test:do_test(
    "where7-2.641.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 7 AND 9) AND a!=8)
         OR b=652
         OR a=72
         OR b=209
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR a=38
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR d>1e10
  ]])
    end, {
        -- <where7-2.641.1>
        7, 9, 19, 23, 38, 66, 68, 72, "scan", 0, "sort", 0
        -- </where7-2.641.1>
    })

test:do_test(
    "where7-2.641.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 7 AND 9) AND a!=8)
         OR b=652
         OR a=72
         OR b=209
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR a=38
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR d>1e10
  ]])
    end, {
        -- <where7-2.641.2>
        7, 9, 19, 23, 38, 66, 68, 72, "scan", 0, "sort", 0
        -- </where7-2.641.2>
    })

test:do_test(
    "where7-2.642.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=949
         OR e IS NULL
  ]])
    end, {
        -- <where7-2.642.1>
        "scan", 0, "sort", 0
        -- </where7-2.642.1>
    })

test:do_test(
    "where7-2.642.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=949
         OR e IS NULL
  ]])
    end, {
        -- <where7-2.642.2>
        "scan", 0, "sort", 0
        -- </where7-2.642.2>
    })

test:do_test(
    "where7-2.643.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=179
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR b=509
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR f='bcdefghij'
  ]])
    end, {
        -- <where7-2.643.1>
        1, 26, 27, 29, 49, 53, 58, 60, 79, "scan", 0, "sort", 0
        -- </where7-2.643.1>
    })

test:do_test(
    "where7-2.643.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=179
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR b=509
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR f='bcdefghij'
  ]])
    end, {
        -- <where7-2.643.2>
        1, 26, 27, 29, 49, 53, 58, 60, 79, "scan", 0, "sort", 0
        -- </where7-2.643.2>
    })

test:do_test(
    "where7-2.644.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=23
         OR a=43
         OR c=19019
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR c=18018
  ]])
    end, {
        -- <where7-2.644.1>
        23, 43, 47, 52, 53, 54, 55, 56, 57, "scan", 0, "sort", 0
        -- </where7-2.644.1>
    })

test:do_test(
    "where7-2.644.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=23
         OR a=43
         OR c=19019
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR c=18018
  ]])
    end, {
        -- <where7-2.644.2>
        23, 43, 47, 52, 53, 54, 55, 56, 57, "scan", 0, "sort", 0
        -- </where7-2.644.2>
    })

test:do_test(
    "where7-2.645.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=36
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR b=231
  ]])
    end, {
        -- <where7-2.645.1>
        21, 22, 36, "scan", 0, "sort", 0
        -- </where7-2.645.1>
    })

test:do_test(
    "where7-2.645.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=36
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR b=231
  ]])
    end, {
        -- <where7-2.645.2>
        21, 22, 36, "scan", 0, "sort", 0
        -- </where7-2.645.2>
    })

test:do_test(
    "where7-2.646.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=21
         OR b=355
         OR a=97
  ]])
    end, {
        -- <where7-2.646.1>
        21, 97, "scan", 0, "sort", 0
        -- </where7-2.646.1>
    })

test:do_test(
    "where7-2.646.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=21
         OR b=355
         OR a=97
  ]])
    end, {
        -- <where7-2.646.2>
        21, 97, "scan", 0, "sort", 0
        -- </where7-2.646.2>
    })

test:do_test(
    "where7-2.647.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR b=421
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR b=704
         OR a=90
         OR a=78
         OR 1000000<b
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR ((a BETWEEN 53 AND 55) AND a!=54)
  ]])
    end, {
        -- <where7-2.647.1>
        28, 42, 53, 55, 64, 78, 80, 81, 90, "scan", 0, "sort", 0
        -- </where7-2.647.1>
    })

test:do_test(
    "where7-2.647.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR b=421
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR b=704
         OR a=90
         OR a=78
         OR 1000000<b
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR ((a BETWEEN 53 AND 55) AND a!=54)
  ]])
    end, {
        -- <where7-2.647.2>
        28, 42, 53, 55, 64, 78, 80, 81, 90, "scan", 0, "sort", 0
        -- </where7-2.647.2>
    })

test:do_test(
    "where7-2.648.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='fedcbaz' AND f GLOB 'pqrst*')
         OR ((a BETWEEN 93 AND 95) AND a!=94)
  ]])
    end, {
        -- <where7-2.648.1>
        93, 95, "scan", 0, "sort", 0
        -- </where7-2.648.1>
    })

test:do_test(
    "where7-2.648.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='fedcbaz' AND f GLOB 'pqrst*')
         OR ((a BETWEEN 93 AND 95) AND a!=94)
  ]])
    end, {
        -- <where7-2.648.2>
        93, 95, "scan", 0, "sort", 0
        -- </where7-2.648.2>
    })

test:do_test(
    "where7-2.649.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE d<0.0
         OR a=78
         OR b=539
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR e IS NULL
         OR a=48
         OR (g='nmlkjih' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.649.1>
        8, 10, 25, 27, 48, 49, 57, 78, "scan", 0, "sort", 0
        -- </where7-2.649.1>
    })

test:do_test(
    "where7-2.649.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE d<0.0
         OR a=78
         OR b=539
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR e IS NULL
         OR a=48
         OR (g='nmlkjih' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.649.2>
        8, 10, 25, 27, 48, 49, 57, 78, "scan", 0, "sort", 0
        -- </where7-2.649.2>
    })

test:do_test(
    "where7-2.650.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 94 AND 96) AND a!=95)
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR b=22
  ]])
    end, {
        -- <where7-2.650.1>
        2, 78, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.650.1>
    })

test:do_test(
    "where7-2.650.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 94 AND 96) AND a!=95)
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR b=22
  ]])
    end, {
        -- <where7-2.650.2>
        2, 78, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.650.2>
    })

test:do_test(
    "where7-2.651.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=275
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR f='ijklmnopq'
  ]])
    end, {
        -- <where7-2.651.1>
        8, 25, 34, 37, 53, 57, 59, 60, 86, 92, "scan", 0, "sort", 0
        -- </where7-2.651.1>
    })

test:do_test(
    "where7-2.651.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=275
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR f='ijklmnopq'
  ]])
    end, {
        -- <where7-2.651.2>
        8, 25, 34, 37, 53, 57, 59, 60, 86, 92, "scan", 0, "sort", 0
        -- </where7-2.651.2>
    })

test:do_test(
    "where7-2.652.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=256
         OR c=13013
         OR b=44
         OR f='jklmnopqr'
         OR b=883
  ]])
    end, {
        -- <where7-2.652.1>
        4, 9, 35, 37, 38, 39, 61, 87, "scan", 0, "sort", 0
        -- </where7-2.652.1>
    })

test:do_test(
    "where7-2.652.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=256
         OR c=13013
         OR b=44
         OR f='jklmnopqr'
         OR b=883
  ]])
    end, {
        -- <where7-2.652.2>
        4, 9, 35, 37, 38, 39, 61, 87, "scan", 0, "sort", 0
        -- </where7-2.652.2>
    })

test:do_test(
    "where7-2.653.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='zabcdefgh'
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR a=54
         OR b=770
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR a=81
         OR b=190
         OR a=2
  ]])
    end, {
        -- <where7-2.653.1>
        2, 3, 25, 51, 54, 70, 77, 81, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.653.1>
    })

test:do_test(
    "where7-2.653.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='zabcdefgh'
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR a=54
         OR b=770
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR a=81
         OR b=190
         OR a=2
  ]])
    end, {
        -- <where7-2.653.2>
        2, 3, 25, 51, 54, 70, 77, 81, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.653.2>
    })

test:do_test(
    "where7-2.654.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR c=12012
         OR a=16
         OR a=15
         OR ((a BETWEEN 70 AND 72) AND a!=71)
         OR a=69
         OR b=748
         OR a=61
         OR b=473
         OR ((a BETWEEN 69 AND 71) AND a!=70)
  ]])
    end, {
        -- <where7-2.654.1>
        12, 15, 16, 34, 35, 36, 43, 61, 68, 69, 70, 71, 72, "scan", 0, "sort", 0
        -- </where7-2.654.1>
    })

test:do_test(
    "where7-2.654.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR c=12012
         OR a=16
         OR a=15
         OR ((a BETWEEN 70 AND 72) AND a!=71)
         OR a=69
         OR b=748
         OR a=61
         OR b=473
         OR ((a BETWEEN 69 AND 71) AND a!=70)
  ]])
    end, {
        -- <where7-2.654.2>
        12, 15, 16, 34, 35, 36, 43, 61, 68, 69, 70, 71, 72, "scan", 0, "sort", 0
        -- </where7-2.654.2>
    })

test:do_test(
    "where7-2.655.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=223
         OR a=14
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR b=539
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR a=21
  ]])
    end, {
        -- <where7-2.655.1>
        14, 21, 33, 35, 41, 48, 49, 61, 74, 76, "scan", 0, "sort", 0
        -- </where7-2.655.1>
    })

test:do_test(
    "where7-2.655.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=223
         OR a=14
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR b=539
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR a=21
  ]])
    end, {
        -- <where7-2.655.2>
        14, 21, 33, 35, 41, 48, 49, 61, 74, 76, "scan", 0, "sort", 0
        -- </where7-2.655.2>
    })

test:do_test(
    "where7-2.656.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=99
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
         OR a=73
         OR a=56
         OR b=253
         OR b=880
  ]])
    end, {
        -- <where7-2.656.1>
        5, 23, 31, 56, 57, 73, 80, 83, 99, "scan", 0, "sort", 0
        -- </where7-2.656.1>
    })

test:do_test(
    "where7-2.656.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=99
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
         OR a=73
         OR a=56
         OR b=253
         OR b=880
  ]])
    end, {
        -- <where7-2.656.2>
        5, 23, 31, 56, 57, 73, 80, 83, 99, "scan", 0, "sort", 0
        -- </where7-2.656.2>
    })

test:do_test(
    "where7-2.657.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=927
         OR b=300
         OR b=223
         OR (g='wvutsrq' AND f GLOB 'jklmn*')
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=154
         OR b=759
  ]])
    end, {
        -- <where7-2.657.1>
        9, 14, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.657.1>
    })

test:do_test(
    "where7-2.657.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=927
         OR b=300
         OR b=223
         OR (g='wvutsrq' AND f GLOB 'jklmn*')
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=154
         OR b=759
  ]])
    end, {
        -- <where7-2.657.2>
        9, 14, 69, 95, "scan", 0, "sort", 0
        -- </where7-2.657.2>
    })

test:do_test(
    "where7-2.658.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=242
         OR b=905
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR a=24
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR b=1100
         OR b=850
         OR ((a BETWEEN 55 AND 57) AND a!=56)
  ]])
    end, {
        -- <where7-2.658.1>
        22, 24, 55, 57, 66, 67, 69, 86, 96, 100, "scan", 0, "sort", 0
        -- </where7-2.658.1>
    })

test:do_test(
    "where7-2.658.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=242
         OR b=905
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'ijklm*')
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR a=24
         OR ((a BETWEEN 67 AND 69) AND a!=68)
         OR b=1100
         OR b=850
         OR ((a BETWEEN 55 AND 57) AND a!=56)
  ]])
    end, {
        -- <where7-2.658.2>
        22, 24, 55, 57, 66, 67, 69, 86, 96, 100, "scan", 0, "sort", 0
        -- </where7-2.658.2>
    })

test:do_test(
    "where7-2.659.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=190
         OR a=72
         OR b=377
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR b=476
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
  ]])
    end, {
        -- <where7-2.659.1>
        2, 26, 52, 72, 78, 93, "scan", 0, "sort", 0
        -- </where7-2.659.1>
    })

test:do_test(
    "where7-2.659.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=190
         OR a=72
         OR b=377
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR b=476
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
  ]])
    end, {
        -- <where7-2.659.2>
        2, 26, 52, 72, 78, 93, "scan", 0, "sort", 0
        -- </where7-2.659.2>
    })

test:do_test(
    "where7-2.660.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=245
         OR b=638
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR f='opqrstuvw'
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR b=817
         OR a=85
         OR (g='lkjihgf' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.660.1>
        14, 40, 58, 62, 64, 66, 67, 85, 86, 92, "scan", 0, "sort", 0
        -- </where7-2.660.1>
    })

test:do_test(
    "where7-2.660.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=245
         OR b=638
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR f='opqrstuvw'
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR b=817
         OR a=85
         OR (g='lkjihgf' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.660.2>
        14, 40, 58, 62, 64, 66, 67, 85, 86, 92, "scan", 0, "sort", 0
        -- </where7-2.660.2>
    })

test:do_test(
    "where7-2.661.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 22 AND 24) AND a!=23)
         OR b=968
  ]])
    end, {
        -- <where7-2.661.1>
        22, 24, 88, "scan", 0, "sort", 0
        -- </where7-2.661.1>
    })

test:do_test(
    "where7-2.661.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 22 AND 24) AND a!=23)
         OR b=968
  ]])
    end, {
        -- <where7-2.661.2>
        22, 24, 88, "scan", 0, "sort", 0
        -- </where7-2.661.2>
    })

test:do_test(
    "where7-2.662.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 60 AND 62) AND a!=61)
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR b=22
         OR b=993
         OR f='tuvwxyzab'
  ]])
    end, {
        -- <where7-2.662.1>
        2, 19, 45, 60, 62, 71, 74, 76, 97, "scan", 0, "sort", 0
        -- </where7-2.662.1>
    })

test:do_test(
    "where7-2.662.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 60 AND 62) AND a!=61)
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR b=22
         OR b=993
         OR f='tuvwxyzab'
  ]])
    end, {
        -- <where7-2.662.2>
        2, 19, 45, 60, 62, 71, 74, 76, 97, "scan", 0, "sort", 0
        -- </where7-2.662.2>
    })

test:do_test(
    "where7-2.663.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 55 AND 57) AND a!=56)
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR c<=10
         OR ((a BETWEEN 75 AND 77) AND a!=76)
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR b=553
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR b=1045
  ]])
    end, {
        -- <where7-2.663.1>
        55, 57, 72, 73, 75, 77, 85, 95, "scan", 0, "sort", 0
        -- </where7-2.663.1>
    })

test:do_test(
    "where7-2.663.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 55 AND 57) AND a!=56)
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR c<=10
         OR ((a BETWEEN 75 AND 77) AND a!=76)
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR b=553
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR b=1045
  ]])
    end, {
        -- <where7-2.663.2>
        55, 57, 72, 73, 75, 77, 85, 95, "scan", 0, "sort", 0
        -- </where7-2.663.2>
    })

test:do_test(
    "where7-2.664.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=440
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
         OR a=89
         OR c=18018
         OR b=154
         OR b=506
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR a=78
         OR b=751
  ]])
    end, {
        -- <where7-2.664.1>
        1, 3, 5, 14, 27, 31, 40, 44, 46, 52, 53, 54, 57, 78, 79, 83, 89, "scan", 0, "sort", 0
        -- </where7-2.664.1>
    })

test:do_test(
    "where7-2.664.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=440
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR ((a BETWEEN 44 AND 46) AND a!=45)
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
         OR a=89
         OR c=18018
         OR b=154
         OR b=506
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR a=78
         OR b=751
  ]])
    end, {
        -- <where7-2.664.2>
        1, 3, 5, 14, 27, 31, 40, 44, 46, 52, 53, 54, 57, 78, 79, 83, 89, "scan", 0, "sort", 0
        -- </where7-2.664.2>
    })

test:do_test(
    "where7-2.665.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=407
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR b=209
         OR b=814
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR a=44
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR b=1092
  ]])
    end, {
        -- <where7-2.665.1>
        10, 19, 36, 37, 38, 44, 65, 74, 99, "scan", 0, "sort", 0
        -- </where7-2.665.1>
    })

test:do_test(
    "where7-2.665.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=407
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR b=209
         OR b=814
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR a=44
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR b=1092
  ]])
    end, {
        -- <where7-2.665.2>
        10, 19, 36, 37, 38, 44, 65, 74, 99, "scan", 0, "sort", 0
        -- </where7-2.665.2>
    })

test:do_test(
    "where7-2.666.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 24 AND 26) AND a!=25)
         OR b=1103
         OR b=190
         OR b=737
         OR a=97
         OR b=451
         OR b=583
         OR a=63
         OR c=8008
         OR ((a BETWEEN 45 AND 47) AND a!=46)
  ]])
    end, {
        -- <where7-2.666.1>
        22, 23, 24, 26, 41, 45, 47, 53, 63, 67, 97, "scan", 0, "sort", 0
        -- </where7-2.666.1>
    })

test:do_test(
    "where7-2.666.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 24 AND 26) AND a!=25)
         OR b=1103
         OR b=190
         OR b=737
         OR a=97
         OR b=451
         OR b=583
         OR a=63
         OR c=8008
         OR ((a BETWEEN 45 AND 47) AND a!=46)
  ]])
    end, {
        -- <where7-2.666.2>
        22, 23, 24, 26, 41, 45, 47, 53, 63, 67, 97, "scan", 0, "sort", 0
        -- </where7-2.666.2>
    })

test:do_test(
    "where7-2.667.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=12
         OR b=935
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR b=1070
         OR a=24
         OR a=95
         OR ((a BETWEEN 27 AND 29) AND a!=28)
         OR a=40
         OR b=935
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.667.1>
        12, 24, 27, 29, 40, 53, 85, 87, 95, "scan", 0, "sort", 0
        -- </where7-2.667.1>
    })

test:do_test(
    "where7-2.667.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=12
         OR b=935
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR b=1070
         OR a=24
         OR a=95
         OR ((a BETWEEN 27 AND 29) AND a!=28)
         OR a=40
         OR b=935
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.667.2>
        12, 24, 27, 29, 40, 53, 85, 87, 95, "scan", 0, "sort", 0
        -- </where7-2.667.2>
    })

test:do_test(
    "where7-2.668.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=858
         OR a=82
         OR b=209
         OR b=374
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR c=22022
  ]])
    end, {
        -- <where7-2.668.1>
        19, 34, 40, 64, 65, 66, 76, 78, 82, "scan", 0, "sort", 0
        -- </where7-2.668.1>
    })

test:do_test(
    "where7-2.668.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=858
         OR a=82
         OR b=209
         OR b=374
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR c=22022
  ]])
    end, {
        -- <where7-2.668.2>
        19, 34, 40, 64, 65, 66, 76, 78, 82, "scan", 0, "sort", 0
        -- </where7-2.668.2>
    })

test:do_test(
    "where7-2.669.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=27
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR b=121
         OR ((a BETWEEN 7 AND 9) AND a!=8)
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR a=67
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR c=1001
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR ((a BETWEEN 19 AND 21) AND a!=20)
  ]])
    end, {
        -- <where7-2.669.1>
        1, 2, 3, 7, 8, 9, 11, 19, 21, 27, 30, 32, 37, 50, 52, 67, "scan", 0, "sort", 0
        -- </where7-2.669.1>
    })

test:do_test(
    "where7-2.669.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=27
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR b=121
         OR ((a BETWEEN 7 AND 9) AND a!=8)
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR a=67
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR c=1001
         OR ((a BETWEEN 50 AND 52) AND a!=51)
         OR ((a BETWEEN 19 AND 21) AND a!=20)
  ]])
    end, {
        -- <where7-2.669.2>
        1, 2, 3, 7, 8, 9, 11, 19, 21, 27, 30, 32, 37, 50, 52, 67, "scan", 0, "sort", 0
        -- </where7-2.669.2>
    })

test:do_test(
    "where7-2.670.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=99
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.670.1>
        1, 9, 46, 57, 98, "scan", 0, "sort", 0
        -- </where7-2.670.1>
    })

test:do_test(
    "where7-2.670.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=99
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR (d>=98.0 AND d<99.0 AND d IS NOT NULL)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.670.2>
        1, 9, 46, 57, 98, "scan", 0, "sort", 0
        -- </where7-2.670.2>
    })

test:do_test(
    "where7-2.671.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=3
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR b=355
         OR b=814
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR a=81
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR b=542
         OR b=795
  ]])
    end, {
        -- <where7-2.671.1>
        3, 42, 62, 74, 79, 81, "scan", 0, "sort", 0
        -- </where7-2.671.1>
    })

test:do_test(
    "where7-2.671.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=3
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR b=355
         OR b=814
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR a=81
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR b=542
         OR b=795
  ]])
    end, {
        -- <where7-2.671.2>
        3, 42, 62, 74, 79, 81, "scan", 0, "sort", 0
        -- </where7-2.671.2>
    })

test:do_test(
    "where7-2.672.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR b=363
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR b=619
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR a=73
  ]])
    end, {
        -- <where7-2.672.1>
        1, 14, 31, 33, 56, 64, 66, 73, "scan", 0, "sort", 0
        -- </where7-2.672.1>
    })

test:do_test(
    "where7-2.672.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR b=363
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR ((a BETWEEN 64 AND 66) AND a!=65)
         OR b=619
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
         OR a=73
  ]])
    end, {
        -- <where7-2.672.2>
        1, 14, 31, 33, 56, 64, 66, 73, "scan", 0, "sort", 0
        -- </where7-2.672.2>
    })

test:do_test(
    "where7-2.673.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=935
         OR a=42
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR b=330
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
  ]])
    end, {
        -- <where7-2.673.1>
        9, 30, 35, 42, 55, 61, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.673.1>
    })

test:do_test(
    "where7-2.673.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=935
         OR a=42
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR b=330
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
  ]])
    end, {
        -- <where7-2.673.2>
        9, 30, 35, 42, 55, 61, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.673.2>
    })

test:do_test(
    "where7-2.674.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=79
         OR b=201
         OR b=99
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR a=64
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR a=89
  ]])
    end, {
        -- <where7-2.674.1>
        9, 16, 19, 21, 42, 64, 68, 79, 89, 94, "scan", 0, "sort", 0
        -- </where7-2.674.1>
    })

test:do_test(
    "where7-2.674.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=79
         OR b=201
         OR b=99
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR a=64
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR a=89
  ]])
    end, {
        -- <where7-2.674.2>
        9, 16, 19, 21, 42, 64, 68, 79, 89, 94, "scan", 0, "sort", 0
        -- </where7-2.674.2>
    })

test:do_test(
    "where7-2.675.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=784
         OR a=85
         OR b=663
         OR c=17017
         OR b=561
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR b=495
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR b=352
         OR ((a BETWEEN 39 AND 41) AND a!=40)
  ]])
    end, {
        -- <where7-2.675.1>
        32, 39, 41, 45, 49, 50, 51, 65, 68, 85, "scan", 0, "sort", 0
        -- </where7-2.675.1>
    })

test:do_test(
    "where7-2.675.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=784
         OR a=85
         OR b=663
         OR c=17017
         OR b=561
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR b=495
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR b=352
         OR ((a BETWEEN 39 AND 41) AND a!=40)
  ]])
    end, {
        -- <where7-2.675.2>
        32, 39, 41, 45, 49, 50, 51, 65, 68, 85, "scan", 0, "sort", 0
        -- </where7-2.675.2>
    })

test:do_test(
    "where7-2.676.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR f='klmnopqrs'
         OR f='lmnopqrst'
  ]])
    end, {
        -- <where7-2.676.1>
        10, 11, 19, 36, 37, 62, 63, 88, 89, 100, "scan", 0, "sort", 0
        -- </where7-2.676.1>
    })

test:do_test(
    "where7-2.676.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR f='klmnopqrs'
         OR f='lmnopqrst'
  ]])
    end, {
        -- <where7-2.676.2>
        10, 11, 19, 36, 37, 62, 63, 88, 89, 100, "scan", 0, "sort", 0
        -- </where7-2.676.2>
    })

test:do_test(
    "where7-2.677.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 90 AND 92) AND a!=91)
         OR a=46
         OR a=44
  ]])
    end, {
        -- <where7-2.677.1>
        44, 46, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.677.1>
    })

test:do_test(
    "where7-2.677.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 90 AND 92) AND a!=91)
         OR a=46
         OR a=44
  ]])
    end, {
        -- <where7-2.677.2>
        44, 46, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.677.2>
    })

test:do_test(
    "where7-2.678.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=36
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR b=682
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR b=91
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR c=12012
         OR b=267
         OR (g='jihgfed' AND f GLOB 'yzabc*')
  ]])
    end, {
        -- <where7-2.678.1>
        18, 20, 34, 35, 36, 39, 43, 53, 55, 62, 76, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.678.1>
    })

test:do_test(
    "where7-2.678.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=36
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR b=682
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR b=91
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR c=12012
         OR b=267
         OR (g='jihgfed' AND f GLOB 'yzabc*')
  ]])
    end, {
        -- <where7-2.678.2>
        18, 20, 34, 35, 36, 39, 43, 53, 55, 62, 76, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.678.2>
    })

test:do_test(
    "where7-2.679.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=594
         OR f='hijklmnop'
         OR ((a BETWEEN 65 AND 67) AND a!=66)
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR b=707
         OR b=363
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=157
         OR (g='tsrqpon' AND f GLOB 'yzabc*')
  ]])
    end, {
        -- <where7-2.679.1>
        7, 12, 24, 33, 54, 58, 59, 65, 67, 85, "scan", 0, "sort", 0
        -- </where7-2.679.1>
    })

test:do_test(
    "where7-2.679.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=594
         OR f='hijklmnop'
         OR ((a BETWEEN 65 AND 67) AND a!=66)
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR b=707
         OR b=363
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=157
         OR (g='tsrqpon' AND f GLOB 'yzabc*')
  ]])
    end, {
        -- <where7-2.679.2>
        7, 12, 24, 33, 54, 58, 59, 65, 67, 85, "scan", 0, "sort", 0
        -- </where7-2.679.2>
    })

test:do_test(
    "where7-2.680.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR a=2
         OR a=84
         OR b=399
         OR b=828
         OR a=21
         OR b=748
         OR c=13013
         OR a=57
         OR f='mnopqrstu'
  ]])
    end, {
        -- <where7-2.680.1>
        2, 12, 21, 27, 37, 38, 39, 57, 64, 68, 84, 90, "scan", 0, "sort", 0
        -- </where7-2.680.1>
    })

test:do_test(
    "where7-2.680.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR a=2
         OR a=84
         OR b=399
         OR b=828
         OR a=21
         OR b=748
         OR c=13013
         OR a=57
         OR f='mnopqrstu'
  ]])
    end, {
        -- <where7-2.680.2>
        2, 12, 21, 27, 37, 38, 39, 57, 64, 68, 84, 90, "scan", 0, "sort", 0
        -- </where7-2.680.2>
    })

test:do_test(
    "where7-2.681.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='nmlkjih' AND f GLOB 'defgh*')
         OR b=674
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR c=3003
         OR a=19
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR ((a BETWEEN 20 AND 22) AND a!=21)
  ]])
    end, {
        -- <where7-2.681.1>
        7, 8, 9, 19, 20, 22, 38, 40, 46, 55, "scan", 0, "sort", 0
        -- </where7-2.681.1>
    })

test:do_test(
    "where7-2.681.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='nmlkjih' AND f GLOB 'defgh*')
         OR b=674
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR c=3003
         OR a=19
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR ((a BETWEEN 20 AND 22) AND a!=21)
  ]])
    end, {
        -- <where7-2.681.2>
        7, 8, 9, 19, 20, 22, 38, 40, 46, 55, "scan", 0, "sort", 0
        -- </where7-2.681.2>
    })

test:do_test(
    "where7-2.682.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=652
         OR a=83
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR b=102
         OR b=300
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.682.1>
        49, 83, 97, "scan", 0, "sort", 0
        -- </where7-2.682.1>
    })

test:do_test(
    "where7-2.682.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=652
         OR a=83
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR b=102
         OR b=300
         OR (d>=49.0 AND d<50.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.682.2>
        49, 83, 97, "scan", 0, "sort", 0
        -- </where7-2.682.2>
    })

test:do_test(
    "where7-2.683.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 53 AND 55) AND a!=54)
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR c=4004
         OR a=95
         OR b=707
         OR f='vwxyzabcd'
         OR b=286
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR b=693
         OR ((a BETWEEN 6 AND 8) AND a!=7)
  ]])
    end, {
        -- <where7-2.683.1>
        6, 8, 10, 11, 12, 21, 26, 43, 45, 47, 53, 55, 63, 73, 95, 99, "scan", 0, "sort", 0
        -- </where7-2.683.1>
    })

test:do_test(
    "where7-2.683.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 53 AND 55) AND a!=54)
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR c=4004
         OR a=95
         OR b=707
         OR f='vwxyzabcd'
         OR b=286
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR b=693
         OR ((a BETWEEN 6 AND 8) AND a!=7)
  ]])
    end, {
        -- <where7-2.683.2>
        6, 8, 10, 11, 12, 21, 26, 43, 45, 47, 53, 55, 63, 73, 95, 99, "scan", 0, "sort", 0
        -- </where7-2.683.2>
    })

test:do_test(
    "where7-2.684.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=209
         OR b=198
         OR a=52
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR d<0.0
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR b=168
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR f='uvwxyzabc'
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.684.1>
        7, 18, 19, 20, 24, 33, 35, 42, 46, 52, 59, 64, 72, 85, 98, "scan", 0, "sort", 0
        -- </where7-2.684.1>
    })

test:do_test(
    "where7-2.684.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=209
         OR b=198
         OR a=52
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR d<0.0
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR b=168
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR f='uvwxyzabc'
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.684.2>
        7, 18, 19, 20, 24, 33, 35, 42, 46, 52, 59, 64, 72, 85, 98, "scan", 0, "sort", 0
        -- </where7-2.684.2>
    })

test:do_test(
    "where7-2.685.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 4 AND 6) AND a!=5)
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR f='rstuvwxyz'
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR a=14
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.685.1>
        4, 6, 8, 12, 14, 17, 21, 26, 43, 47, 69, 73, 84, 89, 91, 95, 99, "scan", 0, "sort", 0
        -- </where7-2.685.1>
    })

test:do_test(
    "where7-2.685.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 4 AND 6) AND a!=5)
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR f='rstuvwxyz'
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR a=14
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.685.2>
        4, 6, 8, 12, 14, 17, 21, 26, 43, 47, 69, 73, 84, 89, 91, 95, 99, "scan", 0, "sort", 0
        -- </where7-2.685.2>
    })

test:do_test(
    "where7-2.686.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 13 AND 15) AND a!=14)
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR f='mnopqrstu'
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR a=38
         OR c=26026
  ]])
    end, {
        -- <where7-2.686.1>
        2, 12, 13, 15, 33, 35, 38, 64, 76, 77, 78, 90, 93, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.686.1>
    })

test:do_test(
    "where7-2.686.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 13 AND 15) AND a!=14)
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR f='mnopqrstu'
         OR (g='fedcbaz' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR a=38
         OR c=26026
  ]])
    end, {
        -- <where7-2.686.2>
        2, 12, 13, 15, 33, 35, 38, 64, 76, 77, 78, 90, 93, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.686.2>
    })

test:do_test(
    "where7-2.687.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ponmlkj' AND f GLOB 'stuvw*')
         OR ((a BETWEEN 71 AND 73) AND a!=72)
         OR a=7
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR ((a BETWEEN 33 AND 35) AND a!=34)
  ]])
    end, {
        -- <where7-2.687.1>
        7, 32, 33, 35, 39, 44, 71, 73, "scan", 0, "sort", 0
        -- </where7-2.687.1>
    })

test:do_test(
    "where7-2.687.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ponmlkj' AND f GLOB 'stuvw*')
         OR ((a BETWEEN 71 AND 73) AND a!=72)
         OR a=7
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR ((a BETWEEN 33 AND 35) AND a!=34)
  ]])
    end, {
        -- <where7-2.687.2>
        7, 32, 33, 35, 39, 44, 71, 73, "scan", 0, "sort", 0
        -- </where7-2.687.2>
    })

test:do_test(
    "where7-2.688.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=6006
         OR b=938
         OR b=484
         OR b=652
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR f='opqrstuvw'
  ]])
    end, {
        -- <where7-2.688.1>
        14, 15, 16, 17, 18, 40, 41, 44, 58, 66, 67, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.688.1>
    })

test:do_test(
    "where7-2.688.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=6006
         OR b=938
         OR b=484
         OR b=652
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR f='opqrstuvw'
  ]])
    end, {
        -- <where7-2.688.2>
        14, 15, 16, 17, 18, 40, 41, 44, 58, 66, 67, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.688.2>
    })

test:do_test(
    "where7-2.689.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=27027
         OR b=968
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR b=487
         OR b=924
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR c=14014
         OR b=1001
  ]])
    end, {
        -- <where7-2.689.1>
        40, 41, 42, 51, 70, 79, 80, 81, 84, 88, 91, "scan", 0, "sort", 0
        -- </where7-2.689.1>
    })

test:do_test(
    "where7-2.689.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=27027
         OR b=968
         OR (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR b=487
         OR b=924
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
         OR c=14014
         OR b=1001
  ]])
    end, {
        -- <where7-2.689.2>
        40, 41, 42, 51, 70, 79, 80, 81, 84, 88, 91, "scan", 0, "sort", 0
        -- </where7-2.689.2>
    })

test:do_test(
    "where7-2.690.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=25
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR b=443
         OR b=564
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=531
         OR b=1081
         OR a=96
  ]])
    end, {
        -- <where7-2.690.1>
        10, 19, 25, 43, 45, 69, 71, 90, 96, 97, "scan", 0, "sort", 0
        -- </where7-2.690.1>
    })

test:do_test(
    "where7-2.690.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=25
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR b=443
         OR b=564
         OR (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=531
         OR b=1081
         OR a=96
  ]])
    end, {
        -- <where7-2.690.2>
        10, 19, 25, 43, 45, 69, 71, 90, 96, 97, "scan", 0, "sort", 0
        -- </where7-2.690.2>
    })

test:do_test(
    "where7-2.691.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=36
         OR (g='srqponm' AND f GLOB 'defgh*')
  ]])
    end, {
        -- <where7-2.691.1>
        29, "scan", 0, "sort", 0
        -- </where7-2.691.1>
    })

test:do_test(
    "where7-2.691.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=36
         OR (g='srqponm' AND f GLOB 'defgh*')
  ]])
    end, {
        -- <where7-2.691.2>
        29, "scan", 0, "sort", 0
        -- </where7-2.691.2>
    })

test:do_test(
    "where7-2.692.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'stuvw*')
         OR b=531
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR (d>=3.0 AND d<4.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.692.1>
        3, 70, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.692.1>
    })

test:do_test(
    "where7-2.692.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'stuvw*')
         OR b=531
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR (d>=3.0 AND d<4.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.692.2>
        3, 70, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.692.2>
    })

test:do_test(
    "where7-2.693.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=256
         OR b=1034
  ]])
    end, {
        -- <where7-2.693.1>
        94, "scan", 0, "sort", 0
        -- </where7-2.693.1>
    })

test:do_test(
    "where7-2.693.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=256
         OR b=1034
  ]])
    end, {
        -- <where7-2.693.2>
        94, "scan", 0, "sort", 0
        -- </where7-2.693.2>
    })

test:do_test(
    "where7-2.694.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR (d>=83.0 AND d<84.0 AND d IS NOT NULL)
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR b=784
         OR b=718
         OR a=18
         OR a=3
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR c=28028
  ]])
    end, {
        -- <where7-2.694.1>
        3, 18, 19, 21, 24, 26, 47, 58, 60, 73, 82, 83, 84, 99, "scan", 0, "sort", 0
        -- </where7-2.694.1>
    })

test:do_test(
    "where7-2.694.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR (d>=83.0 AND d<84.0 AND d IS NOT NULL)
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR b=784
         OR b=718
         OR a=18
         OR a=3
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR c=28028
  ]])
    end, {
        -- <where7-2.694.2>
        3, 18, 19, 21, 24, 26, 47, 58, 60, 73, 82, 83, 84, 99, "scan", 0, "sort", 0
        -- </where7-2.694.2>
    })

test:do_test(
    "where7-2.695.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=473
         OR b=649
         OR ((a BETWEEN 46 AND 48) AND a!=47)
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR b=1100
         OR b=1012
         OR a=72
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR b=176
         OR b=355
  ]])
    end, {
        -- <where7-2.695.1>
        16, 18, 43, 46, 48, 59, 72, 91, 92, 100, "scan", 0, "sort", 0
        -- </where7-2.695.1>
    })

test:do_test(
    "where7-2.695.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=473
         OR b=649
         OR ((a BETWEEN 46 AND 48) AND a!=47)
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR b=1100
         OR b=1012
         OR a=72
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR b=176
         OR b=355
  ]])
    end, {
        -- <where7-2.695.2>
        16, 18, 43, 46, 48, 59, 72, 91, 92, 100, "scan", 0, "sort", 0
        -- </where7-2.695.2>
    })

test:do_test(
    "where7-2.696.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR f='cdefghijk'
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.696.1>
        2, 15, 19, 28, 29, 30, 32, 54, 80, 91, "scan", 0, "sort", 0
        -- </where7-2.696.1>
    })

test:do_test(
    "where7-2.696.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR f='cdefghijk'
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.696.2>
        2, 15, 19, 28, 29, 30, 32, 54, 80, 91, "scan", 0, "sort", 0
        -- </where7-2.696.2>
    })

test:do_test(
    "where7-2.697.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='mlkjihg' AND f GLOB 'ijklm*')
         OR b=883
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR b=938
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR f='defghijkl'
         OR c=2002
         OR b=990
  ]])
    end, {
        -- <where7-2.697.1>
        3, 4, 5, 6, 17, 19, 22, 29, 55, 60, 81, 90, "scan", 0, "sort", 0
        -- </where7-2.697.1>
    })

test:do_test(
    "where7-2.697.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='mlkjihg' AND f GLOB 'ijklm*')
         OR b=883
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR b=938
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR f='defghijkl'
         OR c=2002
         OR b=990
  ]])
    end, {
        -- <where7-2.697.2>
        3, 4, 5, 6, 17, 19, 22, 29, 55, 60, 81, 90, "scan", 0, "sort", 0
        -- </where7-2.697.2>
    })

test:do_test(
    "where7-2.698.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 7 AND 9) AND a!=8)
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR b=902
         OR b=25
  ]])
    end, {
        -- <where7-2.698.1>
        7, 9, 76, 82, "scan", 0, "sort", 0
        -- </where7-2.698.1>
    })

test:do_test(
    "where7-2.698.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 7 AND 9) AND a!=8)
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR b=902
         OR b=25
  ]])
    end, {
        -- <where7-2.698.2>
        7, 9, 76, 82, "scan", 0, "sort", 0
        -- </where7-2.698.2>
    })

test:do_test(
    "where7-2.699.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='tsrqpon' AND f GLOB 'abcde*')
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR b=1092
         OR b=495
  ]])
    end, {
        -- <where7-2.699.1>
        26, 45, 55, 68, 70, 74, 76, "scan", 0, "sort", 0
        -- </where7-2.699.1>
    })

test:do_test(
    "where7-2.699.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='tsrqpon' AND f GLOB 'abcde*')
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR ((a BETWEEN 74 AND 76) AND a!=75)
         OR b=1092
         OR b=495
  ]])
    end, {
        -- <where7-2.699.2>
        26, 45, 55, 68, 70, 74, 76, "scan", 0, "sort", 0
        -- </where7-2.699.2>
    })

test:do_test(
    "where7-2.700.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 90 AND 92) AND a!=91)
         OR a=46
         OR a=74
  ]])
    end, {
        -- <where7-2.700.1>
        46, 74, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.700.1>
    })

test:do_test(
    "where7-2.700.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 90 AND 92) AND a!=91)
         OR a=46
         OR a=74
  ]])
    end, {
        -- <where7-2.700.2>
        46, 74, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.700.2>
    })

test:do_test(
    "where7-2.701.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=18
         OR b=66
         OR b=498
         OR b=143
         OR b=1034
         OR b=289
         OR b=319
  ]])
    end, {
        -- <where7-2.701.1>
        6, 13, 18, 29, 94, "scan", 0, "sort", 0
        -- </where7-2.701.1>
    })

test:do_test(
    "where7-2.701.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=18
         OR b=66
         OR b=498
         OR b=143
         OR b=1034
         OR b=289
         OR b=319
  ]])
    end, {
        -- <where7-2.701.2>
        6, 13, 18, 29, 94, "scan", 0, "sort", 0
        -- </where7-2.701.2>
    })

test:do_test(
    "where7-2.702.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?abcd*' AND f GLOB 'zabc*')
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR (g='srqponm' AND f GLOB 'cdefg*')
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR f='lmnopqrst'
         OR ((a BETWEEN 11 AND 13) AND a!=12)
         OR b=872
         OR a=44
         OR ((a BETWEEN 38 AND 40) AND a!=39)
  ]])
    end, {
        -- <where7-2.702.1>
        11, 13, 25, 28, 30, 37, 38, 40, 44, 45, 51, 54, 63, 77, 79, 89, "scan", 0, "sort", 0
        -- </where7-2.702.1>
    })

test:do_test(
    "where7-2.702.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?abcd*' AND f GLOB 'zabc*')
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR (g='srqponm' AND f GLOB 'cdefg*')
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR f='lmnopqrst'
         OR ((a BETWEEN 11 AND 13) AND a!=12)
         OR b=872
         OR a=44
         OR ((a BETWEEN 38 AND 40) AND a!=39)
  ]])
    end, {
        -- <where7-2.702.2>
        11, 13, 25, 28, 30, 37, 38, 40, 44, 45, 51, 54, 63, 77, 79, 89, "scan", 0, "sort", 0
        -- </where7-2.702.2>
    })

test:do_test(
    "where7-2.703.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR a=20
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR (g='jihgfed' AND f GLOB 'xyzab*')
         OR b=1004
         OR b=77
         OR b=927
         OR a=99
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
  ]])
    end, {
        -- <where7-2.703.1>
        7, 17, 20, 46, 66, 71, 72, 73, 75, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.703.1>
    })

test:do_test(
    "where7-2.703.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR a=20
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR (g='jihgfed' AND f GLOB 'xyzab*')
         OR b=1004
         OR b=77
         OR b=927
         OR a=99
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
  ]])
    end, {
        -- <where7-2.703.2>
        7, 17, 20, 46, 66, 71, 72, 73, 75, 98, 99, "scan", 0, "sort", 0
        -- </where7-2.703.2>
    })

test:do_test(
    "where7-2.704.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=11
         OR ((a BETWEEN 21 AND 23) AND a!=22)
  ]])
    end, {
        -- <where7-2.704.1>
        1, 21, 23, 45, 76, "scan", 0, "sort", 0
        -- </where7-2.704.1>
    })

test:do_test(
    "where7-2.704.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=11
         OR ((a BETWEEN 21 AND 23) AND a!=22)
  ]])
    end, {
        -- <where7-2.704.2>
        1, 21, 23, 45, 76, "scan", 0, "sort", 0
        -- </where7-2.704.2>
    })

test:do_test(
    "where7-2.705.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=572
         OR (g='nmlkjih' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.705.1>
        52, 57, "scan", 0, "sort", 0
        -- </where7-2.705.1>
    })

test:do_test(
    "where7-2.705.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=572
         OR (g='nmlkjih' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.705.2>
        52, 57, "scan", 0, "sort", 0
        -- </where7-2.705.2>
    })

test:do_test(
    "where7-2.706.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR ((a BETWEEN 54 AND 56) AND a!=55)
         OR f='lmnopqrst'
         OR (f GLOB '?lmno*' AND f GLOB 'klmn*')
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR a=23
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.706.1>
        10, 11, 23, 36, 37, 44, 54, 56, 62, 63, 69, 81, 88, 89, "scan", 0, "sort", 0
        -- </where7-2.706.1>
    })

test:do_test(
    "where7-2.706.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR ((a BETWEEN 54 AND 56) AND a!=55)
         OR f='lmnopqrst'
         OR (f GLOB '?lmno*' AND f GLOB 'klmn*')
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR a=23
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.706.2>
        10, 11, 23, 36, 37, 44, 54, 56, 62, 63, 69, 81, 88, 89, "scan", 0, "sort", 0
        -- </where7-2.706.2>
    })

test:do_test(
    "where7-2.707.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=836
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR b=605
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR b=759
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR a=40
         OR f='ghijklmno'
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.707.1>
        6, 24, 32, 38, 40, 46, 50, 55, 58, 69, 70, 76, 84, 85, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.707.1>
    })

test:do_test(
    "where7-2.707.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=836
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR b=605
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR b=759
         OR (f GLOB '?zabc*' AND f GLOB 'yzab*')
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR a=40
         OR f='ghijklmno'
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.707.2>
        6, 24, 32, 38, 40, 46, 50, 55, 58, 69, 70, 76, 84, 85, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.707.2>
    })

test:do_test(
    "where7-2.708.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.708.1>
        42, 51, "scan", 0, "sort", 0
        -- </where7-2.708.1>
    })

test:do_test(
    "where7-2.708.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.708.2>
        42, 51, "scan", 0, "sort", 0
        -- </where7-2.708.2>
    })

test:do_test(
    "where7-2.709.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=27027
         OR b=872
         OR a=56
  ]])
    end, {
        -- <where7-2.709.1>
        56, 79, 80, 81, "scan", 0, "sort", 0
        -- </where7-2.709.1>
    })

test:do_test(
    "where7-2.709.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=27027
         OR b=872
         OR a=56
  ]])
    end, {
        -- <where7-2.709.2>
        56, 79, 80, 81, "scan", 0, "sort", 0
        -- </where7-2.709.2>
    })

test:do_test(
    "where7-2.710.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=685
         OR b=256
         OR ((a BETWEEN 78 AND 80) AND a!=79)
         OR a=44
         OR a=63
         OR a=15
         OR ((a BETWEEN 22 AND 24) AND a!=23)
  ]])
    end, {
        -- <where7-2.710.1>
        15, 22, 24, 44, 63, 78, 80, "scan", 0, "sort", 0
        -- </where7-2.710.1>
    })

test:do_test(
    "where7-2.710.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=685
         OR b=256
         OR ((a BETWEEN 78 AND 80) AND a!=79)
         OR a=44
         OR a=63
         OR a=15
         OR ((a BETWEEN 22 AND 24) AND a!=23)
  ]])
    end, {
        -- <where7-2.710.2>
        15, 22, 24, 44, 63, 78, 80, "scan", 0, "sort", 0
        -- </where7-2.710.2>
    })

test:do_test(
    "where7-2.711.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='nmlkjih' AND f GLOB 'efghi*')
         OR a=34
         OR ((a BETWEEN 6 AND 8) AND a!=7)
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR a=67
         OR a=28
  ]])
    end, {
        -- <where7-2.711.1>
        6, 8, 28, 34, 56, 67, 75, "scan", 0, "sort", 0
        -- </where7-2.711.1>
    })

test:do_test(
    "where7-2.711.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='nmlkjih' AND f GLOB 'efghi*')
         OR a=34
         OR ((a BETWEEN 6 AND 8) AND a!=7)
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR a=67
         OR a=28
  ]])
    end, {
        -- <where7-2.711.2>
        6, 8, 28, 34, 56, 67, 75, "scan", 0, "sort", 0
        -- </where7-2.711.2>
    })

test:do_test(
    "where7-2.712.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='qponmlk' AND f GLOB 'pqrst*')
         OR a=52
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR f='ghijklmno'
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR b=319
         OR a=34
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR f='hijklmnop'
  ]])
    end, {
        -- <where7-2.712.1>
        6, 7, 12, 18, 24, 29, 32, 33, 34, 41, 52, 58, 59, 68, 70, 84, 85, "scan", 0, "sort", 0
        -- </where7-2.712.1>
    })

test:do_test(
    "where7-2.712.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='qponmlk' AND f GLOB 'pqrst*')
         OR a=52
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR f='ghijklmno'
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR b=319
         OR a=34
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR f='hijklmnop'
  ]])
    end, {
        -- <where7-2.712.2>
        6, 7, 12, 18, 24, 29, 32, 33, 34, 41, 52, 58, 59, 68, 70, 84, 85, "scan", 0, "sort", 0
        -- </where7-2.712.2>
    })

test:do_test(
    "where7-2.713.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='qponmlk' AND f GLOB 'pqrst*')
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR a=47
  ]])
    end, {
        -- <where7-2.713.1>
        41, 47, 69, 71, "scan", 0, "sort", 0
        -- </where7-2.713.1>
    })

test:do_test(
    "where7-2.713.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='qponmlk' AND f GLOB 'pqrst*')
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR a=47
  ]])
    end, {
        -- <where7-2.713.2>
        41, 47, 69, 71, "scan", 0, "sort", 0
        -- </where7-2.713.2>
    })

test:do_test(
    "where7-2.714.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 33 AND 35) AND a!=34)
         OR c=7007
  ]])
    end, {
        -- <where7-2.714.1>
        19, 20, 21, 33, 35, "scan", 0, "sort", 0
        -- </where7-2.714.1>
    })

test:do_test(
    "where7-2.714.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 33 AND 35) AND a!=34)
         OR c=7007
  ]])
    end, {
        -- <where7-2.714.2>
        19, 20, 21, 33, 35, "scan", 0, "sort", 0
        -- </where7-2.714.2>
    })

test:do_test(
    "where7-2.715.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=531
         OR a=12
         OR b=583
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR a=61
         OR b=187
  ]])
    end, {
        -- <where7-2.715.1>
        12, 17, 53, 61, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.715.1>
    })

test:do_test(
    "where7-2.715.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=531
         OR a=12
         OR b=583
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR a=61
         OR b=187
  ]])
    end, {
        -- <where7-2.715.2>
        12, 17, 53, 61, 93, 95, "scan", 0, "sort", 0
        -- </where7-2.715.2>
    })

test:do_test(
    "where7-2.716.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=31031
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=256
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR b=715
         OR b=212
         OR b=99
         OR c=29029
  ]])
    end, {
        -- <where7-2.716.1>
        9, 12, 38, 45, 65, 66, 68, 77, 79, 85, 86, 87, 91, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.716.1>
    })

test:do_test(
    "where7-2.716.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=31031
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=256
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR b=715
         OR b=212
         OR b=99
         OR c=29029
  ]])
    end, {
        -- <where7-2.716.2>
        9, 12, 38, 45, 65, 66, 68, 77, 79, 85, 86, 87, 91, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.716.2>
    })

test:do_test(
    "where7-2.717.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 40 AND 42) AND a!=41)
         OR b=33
         OR a=62
         OR b=916
         OR b=1012
         OR a=2
         OR a=51
         OR b=286
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR b=80
  ]])
    end, {
        -- <where7-2.717.1>
        2, 3, 26, 40, 42, 51, 62, 92, 96, "scan", 0, "sort", 0
        -- </where7-2.717.1>
    })

test:do_test(
    "where7-2.717.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 40 AND 42) AND a!=41)
         OR b=33
         OR a=62
         OR b=916
         OR b=1012
         OR a=2
         OR a=51
         OR b=286
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR b=80
  ]])
    end, {
        -- <where7-2.717.2>
        2, 3, 26, 40, 42, 51, 62, 92, 96, "scan", 0, "sort", 0
        -- </where7-2.717.2>
    })

test:do_test(
    "where7-2.718.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=847
         OR f='efghijklm'
         OR (d>=6.0 AND d<7.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.718.1>
        4, 6, 30, 56, 77, 82, "scan", 0, "sort", 0
        -- </where7-2.718.1>
    })

test:do_test(
    "where7-2.718.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=847
         OR f='efghijklm'
         OR (d>=6.0 AND d<7.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.718.2>
        4, 6, 30, 56, 77, 82, "scan", 0, "sort", 0
        -- </where7-2.718.2>
    })

test:do_test(
    "where7-2.719.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='tsrqpon' AND f GLOB 'zabcd*')
         OR ((a BETWEEN 62 AND 64) AND a!=63)
  ]])
    end, {
        -- <where7-2.719.1>
        25, 62, 64, "scan", 0, "sort", 0
        -- </where7-2.719.1>
    })

test:do_test(
    "where7-2.719.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='tsrqpon' AND f GLOB 'zabcd*')
         OR ((a BETWEEN 62 AND 64) AND a!=63)
  ]])
    end, {
        -- <where7-2.719.2>
        25, 62, 64, "scan", 0, "sort", 0
        -- </where7-2.719.2>
    })

test:do_test(
    "where7-2.720.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 43 AND 45) AND a!=44)
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR a=43
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
         OR b=729
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.720.1>
        14, 31, 33, 43, 45, 53, "scan", 0, "sort", 0
        -- </where7-2.720.1>
    })

test:do_test(
    "where7-2.720.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 43 AND 45) AND a!=44)
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR a=43
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
         OR b=729
         OR (g='vutsrqp' AND f GLOB 'opqrs*')
  ]])
    end, {
        -- <where7-2.720.2>
        14, 31, 33, 43, 45, 53, "scan", 0, "sort", 0
        -- </where7-2.720.2>
    })

test:do_test(
    "where7-2.721.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='efghijklm'
         OR a=70
         OR b=278
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR c=8008
         OR f='opqrstuvw'
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR (g='xwvutsr' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.721.1>
        4, 5, 14, 22, 23, 24, 25, 30, 33, 35, 40, 56, 66, 70, 82, 92, "scan", 0, "sort", 0
        -- </where7-2.721.1>
    })

test:do_test(
    "where7-2.721.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='efghijklm'
         OR a=70
         OR b=278
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR c=8008
         OR f='opqrstuvw'
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR (g='xwvutsr' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.721.2>
        4, 5, 14, 22, 23, 24, 25, 30, 33, 35, 40, 56, 66, 70, 82, 92, "scan", 0, "sort", 0
        -- </where7-2.721.2>
    })

test:do_test(
    "where7-2.722.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 40 AND 42) AND a!=41)
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR c<=10
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR a=35
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR b=1089
         OR a=73
         OR b=737
         OR c=18018
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.722.1>
        6, 23, 31, 32, 35, 40, 42, 52, 53, 54, 58, 62, 67, 73, 84, 99, "scan", 0, "sort", 0
        -- </where7-2.722.1>
    })

test:do_test(
    "where7-2.722.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 40 AND 42) AND a!=41)
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR c<=10
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR a=35
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR b=1089
         OR a=73
         OR b=737
         OR c=18018
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.722.2>
        6, 23, 31, 32, 35, 40, 42, 52, 53, 54, 58, 62, 67, 73, 84, 99, "scan", 0, "sort", 0
        -- </where7-2.722.2>
    })

test:do_test(
    "where7-2.723.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 0 AND 2) AND a!=1)
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=762
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR a=80
  ]])
    end, {
        -- <where7-2.723.1>
        2, 39, 41, 79, 80, 95, "scan", 0, "sort", 0
        -- </where7-2.723.1>
    })

test:do_test(
    "where7-2.723.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 0 AND 2) AND a!=1)
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=762
         OR ((a BETWEEN 39 AND 41) AND a!=40)
         OR a=80
  ]])
    end, {
        -- <where7-2.723.2>
        2, 39, 41, 79, 80, 95, "scan", 0, "sort", 0
        -- </where7-2.723.2>
    })

test:do_test(
    "where7-2.724.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 28 AND 30) AND a!=29)
         OR b=737
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR b=979
         OR a=36
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR a=55
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.724.1>
        20, 28, 30, 36, 46, 50, 55, 67, 72, 80, 82, 89, 95, 98, "scan", 0, "sort", 0
        -- </where7-2.724.1>
    })

test:do_test(
    "where7-2.724.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 28 AND 30) AND a!=29)
         OR b=737
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR b=979
         OR a=36
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
         OR a=55
         OR (g='fedcbaz' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.724.2>
        20, 28, 30, 36, 46, 50, 55, 67, 72, 80, 82, 89, 95, 98, "scan", 0, "sort", 0
        -- </where7-2.724.2>
    })

test:do_test(
    "where7-2.725.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=75
         OR a=61
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR (g='gfedcba' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.725.1>
        52, 61, 75, 91, "scan", 0, "sort", 0
        -- </where7-2.725.1>
    })

test:do_test(
    "where7-2.725.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=75
         OR a=61
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR (g='gfedcba' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.725.2>
        52, 61, 75, 91, "scan", 0, "sort", 0
        -- </where7-2.725.2>
    })

test:do_test(
    "where7-2.726.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1004
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
         OR a=56
  ]])
    end, {
        -- <where7-2.726.1>
        7, 56, 61, "scan", 0, "sort", 0
        -- </where7-2.726.1>
    })

test:do_test(
    "where7-2.726.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1004
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
         OR a=56
  ]])
    end, {
        -- <where7-2.726.2>
        7, 56, 61, "scan", 0, "sort", 0
        -- </where7-2.726.2>
    })

test:do_test(
    "where7-2.727.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=93
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR a=83
         OR b=828
         OR b=454
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR b=924
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
         OR a=50
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.727.1>
        38, 50, 58, 66, 83, 84, 89, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.727.1>
    })

test:do_test(
    "where7-2.727.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=93
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR a=83
         OR b=828
         OR b=454
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR b=924
         OR (g='lkjihgf' AND f GLOB 'opqrs*')
         OR a=50
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.727.2>
        38, 50, 58, 66, 83, 84, 89, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.727.2>
    })

test:do_test(
    "where7-2.728.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='stuvwxyza'
         OR a=44
         OR c=2002
  ]])
    end, {
        -- <where7-2.728.1>
        4, 5, 6, 18, 44, 70, 96, "scan", 0, "sort", 0
        -- </where7-2.728.1>
    })

test:do_test(
    "where7-2.728.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='stuvwxyza'
         OR a=44
         OR c=2002
  ]])
    end, {
        -- <where7-2.728.2>
        4, 5, 6, 18, 44, 70, 96, "scan", 0, "sort", 0
        -- </where7-2.728.2>
    })

test:do_test(
    "where7-2.729.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=55
         OR a=65
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
  ]])
    end, {
        -- <where7-2.729.1>
        14, 40, 55, 65, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.729.1>
    })

test:do_test(
    "where7-2.729.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=55
         OR a=65
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
  ]])
    end, {
        -- <where7-2.729.2>
        14, 40, 55, 65, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.729.2>
    })

test:do_test(
    "where7-2.730.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 72 AND 74) AND a!=73)
         OR b=605
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR f='ijklmnopq'
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR c=9009
         OR b=374
  ]])
    end, {
        -- <where7-2.730.1>
        8, 12, 13, 25, 26, 27, 34, 43, 55, 60, 72, 74, 86, 88, "scan", 0, "sort", 0
        -- </where7-2.730.1>
    })

test:do_test(
    "where7-2.730.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 72 AND 74) AND a!=73)
         OR b=605
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR f='ijklmnopq'
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR c=9009
         OR b=374
  ]])
    end, {
        -- <where7-2.730.2>
        8, 12, 13, 25, 26, 27, 34, 43, 55, 60, 72, 74, 86, 88, "scan", 0, "sort", 0
        -- </where7-2.730.2>
    })

test:do_test(
    "where7-2.731.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=476
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR b=982
         OR a=43
         OR b=355
  ]])
    end, {
        -- <where7-2.731.1>
        8, 43, "scan", 0, "sort", 0
        -- </where7-2.731.1>
    })

test:do_test(
    "where7-2.731.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=476
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR b=982
         OR a=43
         OR b=355
  ]])
    end, {
        -- <where7-2.731.2>
        8, 43, "scan", 0, "sort", 0
        -- </where7-2.731.2>
    })

test:do_test(
    "where7-2.732.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=85
         OR b=718
         OR (g='fedcbaz' AND f GLOB 'pqrst*')
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.732.1>
        1, 25, 27, 53, 79, 85, 93, "scan", 0, "sort", 0
        -- </where7-2.732.1>
    })

test:do_test(
    "where7-2.732.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=85
         OR b=718
         OR (g='fedcbaz' AND f GLOB 'pqrst*')
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.732.2>
        1, 25, 27, 53, 79, 85, 93, "scan", 0, "sort", 0
        -- </where7-2.732.2>
    })

test:do_test(
    "where7-2.733.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR ((a BETWEEN 96 AND 98) AND a!=97)
  ]])
    end, {
        -- <where7-2.733.1>
        73, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.733.1>
    })

test:do_test(
    "where7-2.733.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR ((a BETWEEN 96 AND 98) AND a!=97)
  ]])
    end, {
        -- <where7-2.733.2>
        73, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.733.2>
    })

test:do_test(
    "where7-2.734.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=176
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR ((a BETWEEN 27 AND 29) AND a!=28)
         OR b=619
         OR b=597
         OR b=198
         OR a=27
         OR b=91
         OR a=77
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.734.1>
        16, 18, 25, 27, 29, 77, 80, "scan", 0, "sort", 0
        -- </where7-2.734.1>
    })

test:do_test(
    "where7-2.734.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=176
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR ((a BETWEEN 27 AND 29) AND a!=28)
         OR b=619
         OR b=597
         OR b=198
         OR a=27
         OR b=91
         OR a=77
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.734.2>
        16, 18, 25, 27, 29, 77, 80, "scan", 0, "sort", 0
        -- </where7-2.734.2>
    })

test:do_test(
    "where7-2.735.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=41
         OR b=528
         OR c=3003
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=22
  ]])
    end, {
        -- <where7-2.735.1>
        2, 7, 8, 9, 20, 22, 41, 48, "scan", 0, "sort", 0
        -- </where7-2.735.1>
    })

test:do_test(
    "where7-2.735.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=41
         OR b=528
         OR c=3003
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR b=22
  ]])
    end, {
        -- <where7-2.735.2>
        2, 7, 8, 9, 20, 22, 41, 48, "scan", 0, "sort", 0
        -- </where7-2.735.2>
    })

test:do_test(
    "where7-2.736.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR b=465
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=37
         OR b=1056
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR b=1023
  ]])
    end, {
        -- <where7-2.736.1>
        4, 16, 29, 37, 42, 63, 65, 68, 93, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.736.1>
    })

test:do_test(
    "where7-2.736.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR b=465
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=37
         OR b=1056
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR b=1023
  ]])
    end, {
        -- <where7-2.736.2>
        4, 16, 29, 37, 42, 63, 65, 68, 93, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.736.2>
    })

test:do_test(
    "where7-2.737.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=76
         OR a=8
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR b=495
         OR b=663
         OR a=98
         OR b=748
  ]])
    end, {
        -- <where7-2.737.1>
        8, 27, 45, 68, 76, 98, "scan", 0, "sort", 0
        -- </where7-2.737.1>
    })

test:do_test(
    "where7-2.737.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=76
         OR a=8
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR b=495
         OR b=663
         OR a=98
         OR b=748
  ]])
    end, {
        -- <where7-2.737.2>
        8, 27, 45, 68, 76, 98, "scan", 0, "sort", 0
        -- </where7-2.737.2>
    })

test:do_test(
    "where7-2.738.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1081
         OR b=542
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR b=828
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR a=18
  ]])
    end, {
        -- <where7-2.738.1>
        18, 47, 61, 64, 67, "scan", 0, "sort", 0
        -- </where7-2.738.1>
    })

test:do_test(
    "where7-2.738.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1081
         OR b=542
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR b=828
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR a=18
  ]])
    end, {
        -- <where7-2.738.2>
        18, 47, 61, 64, 67, "scan", 0, "sort", 0
        -- </where7-2.738.2>
    })

test:do_test(
    "where7-2.739.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='abcdefghi'
         OR a=14
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR c=27027
         OR a=47
  ]])
    end, {
        -- <where7-2.739.1>
        13, 14, 26, 47, 52, 78, 79, 80, 81, "scan", 0, "sort", 0
        -- </where7-2.739.1>
    })

test:do_test(
    "where7-2.739.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='abcdefghi'
         OR a=14
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR c=27027
         OR a=47
  ]])
    end, {
        -- <where7-2.739.2>
        13, 14, 26, 47, 52, 78, 79, 80, 81, "scan", 0, "sort", 0
        -- </where7-2.739.2>
    })

test:do_test(
    "where7-2.740.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=31031
         OR b=737
         OR a=37
         OR ((a BETWEEN 98 AND 100) AND a!=99)
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR ((a BETWEEN 65 AND 67) AND a!=66)
         OR a=91
         OR b=77
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.740.1>
        7, 37, 65, 67, 91, 92, 93, 94, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.740.1>
    })

test:do_test(
    "where7-2.740.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=31031
         OR b=737
         OR a=37
         OR ((a BETWEEN 98 AND 100) AND a!=99)
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR ((a BETWEEN 65 AND 67) AND a!=66)
         OR a=91
         OR b=77
         OR (d>=94.0 AND d<95.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.740.2>
        7, 37, 65, 67, 91, 92, 93, 94, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.740.2>
    })

test:do_test(
    "where7-2.741.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=17
         OR b=484
         OR c=3003
         OR b=121
         OR a=53
  ]])
    end, {
        -- <where7-2.741.1>
        7, 8, 9, 11, 17, 44, 53, "scan", 0, "sort", 0
        -- </where7-2.741.1>
    })

test:do_test(
    "where7-2.741.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=17
         OR b=484
         OR c=3003
         OR b=121
         OR a=53
  ]])
    end, {
        -- <where7-2.741.2>
        7, 8, 9, 11, 17, 44, 53, "scan", 0, "sort", 0
        -- </where7-2.741.2>
    })

test:do_test(
    "where7-2.742.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=880
         OR b=696
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR b=308
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 96 AND 98) AND a!=97)
  ]])
    end, {
        -- <where7-2.742.1>
        5, 28, 65, 80, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.742.1>
    })

test:do_test(
    "where7-2.742.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=880
         OR b=696
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR b=308
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 96 AND 98) AND a!=97)
  ]])
    end, {
        -- <where7-2.742.2>
        5, 28, 65, 80, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.742.2>
    })

test:do_test(
    "where7-2.743.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='tsrqpon' AND f GLOB 'zabcd*')
         OR a=24
         OR f IS NULL
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR c=12012
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.743.1>
        24, 25, 34, 35, 36, 57, 77, 88, "scan", 0, "sort", 0
        -- </where7-2.743.1>
    })

test:do_test(
    "where7-2.743.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='tsrqpon' AND f GLOB 'zabcd*')
         OR a=24
         OR f IS NULL
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR c=12012
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.743.2>
        24, 25, 34, 35, 36, 57, 77, 88, "scan", 0, "sort", 0
        -- </where7-2.743.2>
    })

test:do_test(
    "where7-2.744.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=94
         OR (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR b=792
         OR a=77
         OR a=26
         OR b=641
         OR a=38
  ]])
    end, {
        -- <where7-2.744.1>
        26, 38, 72, 74, 77, 85, 94, "scan", 0, "sort", 0
        -- </where7-2.744.1>
    })

test:do_test(
    "where7-2.744.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=94
         OR (d>=74.0 AND d<75.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR b=792
         OR a=77
         OR a=26
         OR b=641
         OR a=38
  ]])
    end, {
        -- <where7-2.744.2>
        26, 38, 72, 74, 77, 85, 94, "scan", 0, "sort", 0
        -- </where7-2.744.2>
    })

test:do_test(
    "where7-2.745.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 51 AND 53) AND a!=52)
         OR (d>=30.0 AND d<31.0 AND d IS NOT NULL)
         OR b=14
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR ((a BETWEEN 15 AND 17) AND a!=16)
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR b=121
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.745.1>
        11, 15, 17, 27, 30, 51, 53, 63, 86, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.745.1>
    })

test:do_test(
    "where7-2.745.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 51 AND 53) AND a!=52)
         OR (d>=30.0 AND d<31.0 AND d IS NOT NULL)
         OR b=14
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR ((a BETWEEN 15 AND 17) AND a!=16)
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR b=121
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.745.2>
        11, 15, 17, 27, 30, 51, 53, 63, 86, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.745.2>
    })

test:do_test(
    "where7-2.746.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=517
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR f='opqrstuvw'
  ]])
    end, {
        -- <where7-2.746.1>
        14, 40, 47, 66, 69, 71, 92, "scan", 0, "sort", 0
        -- </where7-2.746.1>
    })

test:do_test(
    "where7-2.746.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=517
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR f='opqrstuvw'
  ]])
    end, {
        -- <where7-2.746.2>
        14, 40, 47, 66, 69, 71, 92, "scan", 0, "sort", 0
        -- </where7-2.746.2>
    })

test:do_test(
    "where7-2.747.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=84.0 AND d<85.0 AND d IS NOT NULL)
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=267
         OR c=19019
         OR a=42
         OR b=938
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
  ]])
    end, {
        -- <where7-2.747.1>
        1, 9, 17, 21, 22, 24, 32, 34, 35, 42, 43, 55, 56, 57, 61, 69, 84, 87, 95, "scan", 0, "sort", 0
        -- </where7-2.747.1>
    })

test:do_test(
    "where7-2.747.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=84.0 AND d<85.0 AND d IS NOT NULL)
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=267
         OR c=19019
         OR a=42
         OR b=938
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
  ]])
    end, {
        -- <where7-2.747.2>
        1, 9, 17, 21, 22, 24, 32, 34, 35, 42, 43, 55, 56, 57, 61, 69, 84, 87, 95, "scan", 0, "sort", 0
        -- </where7-2.747.2>
    })

test:do_test(
    "where7-2.748.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=179
         OR a=50
         OR (g='srqponm' AND f GLOB 'defgh*')
  ]])
    end, {
        -- <where7-2.748.1>
        29, 50, "scan", 0, "sort", 0
        -- </where7-2.748.1>
    })

test:do_test(
    "where7-2.748.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=179
         OR a=50
         OR (g='srqponm' AND f GLOB 'defgh*')
  ]])
    end, {
        -- <where7-2.748.2>
        29, 50, "scan", 0, "sort", 0
        -- </where7-2.748.2>
    })

test:do_test(
    "where7-2.749.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='vutsrqp' AND f GLOB 'rstuv*')
         OR f='xyzabcdef'
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR b=575
         OR b=385
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=46
         OR b=220
         OR a=63
  ]])
    end, {
        -- <where7-2.749.1>
        17, 18, 20, 23, 35, 46, 49, 51, 63, 65, 75, "scan", 0, "sort", 0
        -- </where7-2.749.1>
    })

test:do_test(
    "where7-2.749.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='vutsrqp' AND f GLOB 'rstuv*')
         OR f='xyzabcdef'
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR b=575
         OR b=385
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=46
         OR b=220
         OR a=63
  ]])
    end, {
        -- <where7-2.749.2>
        17, 18, 20, 23, 35, 46, 49, 51, 63, 65, 75, "scan", 0, "sort", 0
        -- </where7-2.749.2>
    })

test:do_test(
    "where7-2.750.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1056
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=1078
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
         OR c=31031
         OR b=869
         OR (g='jihgfed' AND f GLOB 'zabcd*')
         OR b=245
         OR a=92
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR b=880
  ]])
    end, {
        -- <where7-2.750.1>
        66, 77, 79, 80, 91, 92, 93, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.750.1>
    })

test:do_test(
    "where7-2.750.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1056
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=1078
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
         OR c=31031
         OR b=869
         OR (g='jihgfed' AND f GLOB 'zabcd*')
         OR b=245
         OR a=92
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR b=880
  ]])
    end, {
        -- <where7-2.750.2>
        66, 77, 79, 80, 91, 92, 93, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.750.2>
    })

test:do_test(
    "where7-2.751.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1078
         OR c=28028
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR c=9009
         OR a=17
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.751.1>
        16, 17, 19, 25, 26, 27, 38, 39, 40, 42, 61, 68, 82, 83, 84, 94, 98, "scan", 0, "sort", 0
        -- </where7-2.751.1>
    })

test:do_test(
    "where7-2.751.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1078
         OR c=28028
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR (g='mlkjihg' AND f GLOB 'jklmn*')
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR c=9009
         OR a=17
         OR (d>=39.0 AND d<40.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.751.2>
        16, 17, 19, 25, 26, 27, 38, 39, 40, 42, 61, 68, 82, 83, 84, 94, 98, "scan", 0, "sort", 0
        -- </where7-2.751.2>
    })

test:do_test(
    "where7-2.752.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR b=762
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR f='tuvwxyzab'
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR b=1034
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.752.1>
        14, 19, 31, 33, 44, 45, 57, 58, 71, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.752.1>
    })

test:do_test(
    "where7-2.752.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR b=762
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR f='tuvwxyzab'
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR b=1034
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.752.2>
        14, 19, 31, 33, 44, 45, 57, 58, 71, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.752.2>
    })

test:do_test(
    "where7-2.753.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=47
         OR b=187
         OR a=56
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR ((a BETWEEN 89 AND 91) AND a!=90)
  ]])
    end, {
        -- <where7-2.753.1>
        17, 30, 32, 56, 68, 70, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.753.1>
    })

test:do_test(
    "where7-2.753.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=47
         OR b=187
         OR a=56
         OR ((a BETWEEN 30 AND 32) AND a!=31)
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR ((a BETWEEN 89 AND 91) AND a!=90)
  ]])
    end, {
        -- <where7-2.753.2>
        17, 30, 32, 56, 68, 70, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.753.2>
    })

test:do_test(
    "where7-2.754.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=11011
         OR a=14
         OR c=16016
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR f='jklmnopqr'
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR b=916
  ]])
    end, {
        -- <where7-2.754.1>
        9, 14, 21, 25, 30, 31, 32, 33, 35, 46, 47, 48, 61, 87, 96, "scan", 0, "sort", 0
        -- </where7-2.754.1>
    })

test:do_test(
    "where7-2.754.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=11011
         OR a=14
         OR c=16016
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR f='jklmnopqr'
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR b=916
  ]])
    end, {
        -- <where7-2.754.2>
        9, 14, 21, 25, 30, 31, 32, 33, 35, 46, 47, 48, 61, 87, 96, "scan", 0, "sort", 0
        -- </where7-2.754.2>
    })

test:do_test(
    "where7-2.755.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=949
         OR (g='srqponm' AND f GLOB 'cdefg*')
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR c<=10
         OR a=14
         OR b=608
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR b=121
         OR b=333
         OR ((a BETWEEN 93 AND 95) AND a!=94)
  ]])
    end, {
        -- <where7-2.755.1>
        11, 14, 17, 28, 66, 93, 95, 98, "scan", 0, "sort", 0
        -- </where7-2.755.1>
    })

test:do_test(
    "where7-2.755.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=949
         OR (g='srqponm' AND f GLOB 'cdefg*')
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
         OR c<=10
         OR a=14
         OR b=608
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR b=121
         OR b=333
         OR ((a BETWEEN 93 AND 95) AND a!=94)
  ]])
    end, {
        -- <where7-2.755.2>
        11, 14, 17, 28, 66, 93, 95, 98, "scan", 0, "sort", 0
        -- </where7-2.755.2>
    })

test:do_test(
    "where7-2.756.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=355
         OR b=627
         OR b=1001
         OR b=1026
         OR ((a BETWEEN 58 AND 60) AND a!=59)
  ]])
    end, {
        -- <where7-2.756.1>
        57, 58, 60, 69, 91, "scan", 0, "sort", 0
        -- </where7-2.756.1>
    })

test:do_test(
    "where7-2.756.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'rstuv*')
         OR b=355
         OR b=627
         OR b=1001
         OR b=1026
         OR ((a BETWEEN 58 AND 60) AND a!=59)
  ]])
    end, {
        -- <where7-2.756.2>
        57, 58, 60, 69, 91, "scan", 0, "sort", 0
        -- </where7-2.756.2>
    })

test:do_test(
    "where7-2.757.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'efghi*')
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.757.1>
        4, 79, "scan", 0, "sort", 0
        -- </where7-2.757.1>
    })

test:do_test(
    "where7-2.757.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'efghi*')
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.757.2>
        4, 79, "scan", 0, "sort", 0
        -- </where7-2.757.2>
    })

test:do_test(
    "where7-2.758.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=685
         OR a=14
         OR b=990
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR f='efghijklm'
         OR c=1001
         OR b=784
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.758.1>
        1, 2, 3, 4, 14, 26, 30, 32, 56, 69, 82, 90, "scan", 0, "sort", 0
        -- </where7-2.758.1>
    })

test:do_test(
    "where7-2.758.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=685
         OR a=14
         OR b=990
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR f='efghijklm'
         OR c=1001
         OR b=784
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.758.2>
        1, 2, 3, 4, 14, 26, 30, 32, 56, 69, 82, 90, "scan", 0, "sort", 0
        -- </where7-2.758.2>
    })

test:do_test(
    "where7-2.759.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=54
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR c=26026
         OR ((a BETWEEN 97 AND 99) AND a!=98)
  ]])
    end, {
        -- <where7-2.759.1>
        39, 54, 76, 77, 78, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.759.1>
    })

test:do_test(
    "where7-2.759.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=54
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR c=26026
         OR ((a BETWEEN 97 AND 99) AND a!=98)
  ]])
    end, {
        -- <where7-2.759.2>
        39, 54, 76, 77, 78, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.759.2>
    })

test:do_test(
    "where7-2.760.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'ghijk*')
         OR c=24024
         OR a=98
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR a=5
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR f='pqrstuvwx'
         OR f='bcdefghij'
         OR b=1001
         OR ((a BETWEEN 77 AND 79) AND a!=78)
  ]])
    end, {
        -- <where7-2.760.1>
        1, 5, 15, 21, 27, 31, 33, 36, 41, 53, 67, 70, 71, 72, 77, 79, 84, 91, 93, 98, "scan", 0, "sort", 0
        -- </where7-2.760.1>
    })

test:do_test(
    "where7-2.760.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'ghijk*')
         OR c=24024
         OR a=98
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR a=5
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR f='pqrstuvwx'
         OR f='bcdefghij'
         OR b=1001
         OR ((a BETWEEN 77 AND 79) AND a!=78)
  ]])
    end, {
        -- <where7-2.760.2>
        1, 5, 15, 21, 27, 31, 33, 36, 41, 53, 67, 70, 71, 72, 77, 79, 84, 91, 93, 98, "scan", 0, "sort", 0
        -- </where7-2.760.2>
    })

test:do_test(
    "where7-2.761.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=781
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR f='lmnopqrst'
         OR a=39
         OR a=100
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.761.1>
        1, 11, 14, 37, 39, 40, 54, 56, 58, 63, 66, 71, 89, 92, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.761.1>
    })

test:do_test(
    "where7-2.761.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=781
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR f='lmnopqrst'
         OR a=39
         OR a=100
         OR ((a BETWEEN 56 AND 58) AND a!=57)
  ]])
    end, {
        -- <where7-2.761.2>
        1, 11, 14, 37, 39, 40, 54, 56, 58, 63, 66, 71, 89, 92, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.761.2>
    })

test:do_test(
    "where7-2.762.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=4004
         OR b=718
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR a=50
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR b=363
         OR (g='rqponml' AND f GLOB 'ijklm*')
         OR b=1023
  ]])
    end, {
        -- <where7-2.762.1>
        10, 11, 12, 33, 34, 40, 50, 93, "scan", 0, "sort", 0
        -- </where7-2.762.1>
    })

test:do_test(
    "where7-2.762.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=4004
         OR b=718
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR a=50
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR b=363
         OR (g='rqponml' AND f GLOB 'ijklm*')
         OR b=1023
  ]])
    end, {
        -- <where7-2.762.2>
        10, 11, 12, 33, 34, 40, 50, 93, "scan", 0, "sort", 0
        -- </where7-2.762.2>
    })

test:do_test(
    "where7-2.763.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1081
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR b=473
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR b=586
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
  ]])
    end, {
        -- <where7-2.763.1>
        20, 26, 43, 45, 46, 55, 72, 98, "scan", 0, "sort", 0
        -- </where7-2.763.1>
    })

test:do_test(
    "where7-2.763.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1081
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR b=473
         OR ((a BETWEEN 43 AND 45) AND a!=44)
         OR b=586
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR (f GLOB '?vwxy*' AND f GLOB 'uvwx*')
  ]])
    end, {
        -- <where7-2.763.2>
        20, 26, 43, 45, 46, 55, 72, 98, "scan", 0, "sort", 0
        -- </where7-2.763.2>
    })

test:do_test(
    "where7-2.764.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.764.1>
        7, 13, 33, 58, 59, 85, "scan", 0, "sort", 0
        -- </where7-2.764.1>
    })

test:do_test(
    "where7-2.764.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.764.2>
        7, 13, 33, 58, 59, 85, "scan", 0, "sort", 0
        -- </where7-2.764.2>
    })

test:do_test(
    "where7-2.765.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'hijkl*')
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR a=47
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR (g='lkjihgf' AND f GLOB 'lmnop*')
         OR (d>=84.0 AND d<85.0 AND d IS NOT NULL)
         OR f='lmnopqrst'
  ]])
    end, {
        -- <where7-2.765.1>
        11, 37, 47, 63, 68, 76, 78, 84, 85, 89, "scan", 0, "sort", 0
        -- </where7-2.765.1>
    })

test:do_test(
    "where7-2.765.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'hijkl*')
         OR ((a BETWEEN 76 AND 78) AND a!=77)
         OR a=47
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR (g='lkjihgf' AND f GLOB 'lmnop*')
         OR (d>=84.0 AND d<85.0 AND d IS NOT NULL)
         OR f='lmnopqrst'
  ]])
    end, {
        -- <where7-2.765.2>
        11, 37, 47, 63, 68, 76, 78, 84, 85, 89, "scan", 0, "sort", 0
        -- </where7-2.765.2>
    })

test:do_test(
    "where7-2.766.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c>=34035
         OR a=29
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR f='abcdefghi'
         OR b=993
         OR ((a BETWEEN 52 AND 54) AND a!=53)
         OR ((a BETWEEN 45 AND 47) AND a!=46)
  ]])
    end, {
        -- <where7-2.766.1>
        19, 21, 26, 29, 45, 47, 52, 54, 73, 78, 99, "scan", 0, "sort", 0
        -- </where7-2.766.1>
    })

test:do_test(
    "where7-2.766.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c>=34035
         OR a=29
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR (f GLOB '?wxyz*' AND f GLOB 'vwxy*')
         OR f='abcdefghi'
         OR b=993
         OR ((a BETWEEN 52 AND 54) AND a!=53)
         OR ((a BETWEEN 45 AND 47) AND a!=46)
  ]])
    end, {
        -- <where7-2.766.2>
        19, 21, 26, 29, 45, 47, 52, 54, 73, 78, 99, "scan", 0, "sort", 0
        -- </where7-2.766.2>
    })

test:do_test(
    "where7-2.767.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=696
         OR b=154
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR a=22
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR a=52
         OR a=21
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.767.1>
        14, 16, 21, 22, 24, 47, 52, 63, 70, "scan", 0, "sort", 0
        -- </where7-2.767.1>
    })

test:do_test(
    "where7-2.767.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=696
         OR b=154
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR a=22
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR a=52
         OR a=21
         OR (d>=70.0 AND d<71.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.767.2>
        14, 16, 21, 22, 24, 47, 52, 63, 70, "scan", 0, "sort", 0
        -- </where7-2.767.2>
    })

test:do_test(
    "where7-2.768.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=693
         OR b=201
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR b=520
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR b=407
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR c>=34035
         OR b=135
  ]])
    end, {
        -- <where7-2.768.1>
        23, 25, 36, 37, 38, 63, "scan", 0, "sort", 0
        -- </where7-2.768.1>
    })

test:do_test(
    "where7-2.768.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=693
         OR b=201
         OR ((a BETWEEN 36 AND 38) AND a!=37)
         OR b=520
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR b=407
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR c>=34035
         OR b=135
  ]])
    end, {
        -- <where7-2.768.2>
        23, 25, 36, 37, 38, 63, "scan", 0, "sort", 0
        -- </where7-2.768.2>
    })

test:do_test(
    "where7-2.769.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR b=707
         OR b=14
         OR b=1089
         OR b=352
  ]])
    end, {
        -- <where7-2.769.1>
        32, 43, 99, "scan", 0, "sort", 0
        -- </where7-2.769.1>
    })

test:do_test(
    "where7-2.769.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR b=707
         OR b=14
         OR b=1089
         OR b=352
  ]])
    end, {
        -- <where7-2.769.2>
        32, 43, 99, "scan", 0, "sort", 0
        -- </where7-2.769.2>
    })

test:do_test(
    "where7-2.770.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=278
         OR b=278
         OR b=825
         OR f='rstuvwxyz'
         OR b=938
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR ((a BETWEEN 43 AND 45) AND a!=44)
  ]])
    end, {
        -- <where7-2.770.1>
        17, 19, 43, 45, 69, 75, 95, "scan", 0, "sort", 0
        -- </where7-2.770.1>
    })

test:do_test(
    "where7-2.770.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=278
         OR b=278
         OR b=825
         OR f='rstuvwxyz'
         OR b=938
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR ((a BETWEEN 43 AND 45) AND a!=44)
  ]])
    end, {
        -- <where7-2.770.2>
        17, 19, 43, 45, 69, 75, 95, "scan", 0, "sort", 0
        -- </where7-2.770.2>
    })

test:do_test(
    "where7-2.771.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=1045
         OR c=27027
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
  ]])
    end, {
        -- <where7-2.771.1>
        11, 32, 34, 37, 63, 79, 80, 81, 89, 95, "scan", 0, "sort", 0
        -- </where7-2.771.1>
    })

test:do_test(
    "where7-2.771.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=1045
         OR c=27027
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
  ]])
    end, {
        -- <where7-2.771.2>
        11, 32, 34, 37, 63, 79, 80, 81, 89, 95, "scan", 0, "sort", 0
        -- </where7-2.771.2>
    })

test:do_test(
    "where7-2.772.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=87
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR b=487
         OR (g='gfedcba' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.772.1>
        47, 87, 90, "scan", 0, "sort", 0
        -- </where7-2.772.1>
    })

test:do_test(
    "where7-2.772.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=87
         OR (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR b=487
         OR (g='gfedcba' AND f GLOB 'mnopq*')
  ]])
    end, {
        -- <where7-2.772.2>
        47, 87, 90, "scan", 0, "sort", 0
        -- </where7-2.772.2>
    })

test:do_test(
    "where7-2.773.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 30 AND 32) AND a!=31)
         OR b=69
         OR b=608
         OR b=814
         OR a=67
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR b=1059
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
         OR a=18
         OR b=407
         OR ((a BETWEEN 10 AND 12) AND a!=11)
  ]])
    end, {
        -- <where7-2.773.1>
        10, 12, 18, 30, 32, 37, 58, 61, 67, 74, "scan", 0, "sort", 0
        -- </where7-2.773.1>
    })

test:do_test(
    "where7-2.773.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 30 AND 32) AND a!=31)
         OR b=69
         OR b=608
         OR b=814
         OR a=67
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR b=1059
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
         OR a=18
         OR b=407
         OR ((a BETWEEN 10 AND 12) AND a!=11)
  ]])
    end, {
        -- <where7-2.773.2>
        10, 12, 18, 30, 32, 37, 58, 61, 67, 74, "scan", 0, "sort", 0
        -- </where7-2.773.2>
    })

test:do_test(
    "where7-2.774.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=223
         OR b=80
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR ((a BETWEEN 74 AND 76) AND a!=75)
  ]])
    end, {
        -- <where7-2.774.1>
        74, 76, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.774.1>
    })

test:do_test(
    "where7-2.774.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=223
         OR b=80
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR ((a BETWEEN 74 AND 76) AND a!=75)
  ]])
    end, {
        -- <where7-2.774.2>
        74, 76, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.774.2>
    })

test:do_test(
    "where7-2.775.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=220
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR b=363
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR ((a BETWEEN 10 AND 12) AND a!=11)
  ]])
    end, {
        -- <where7-2.775.1>
        10, 12, 20, 33, 52, 54, 55, 66, "scan", 0, "sort", 0
        -- </where7-2.775.1>
    })

test:do_test(
    "where7-2.775.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=220
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR b=363
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'defgh*')
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR ((a BETWEEN 10 AND 12) AND a!=11)
  ]])
    end, {
        -- <where7-2.775.2>
        10, 12, 20, 33, 52, 54, 55, 66, "scan", 0, "sort", 0
        -- </where7-2.775.2>
    })

test:do_test(
    "where7-2.776.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=498
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR b=880
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR b=828
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR b=113
  ]])
    end, {
        -- <where7-2.776.1>
        5, 15, 60, 62, 80, "scan", 0, "sort", 0
        -- </where7-2.776.1>
    })

test:do_test(
    "where7-2.776.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=498
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR b=880
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR b=828
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR b=113
  ]])
    end, {
        -- <where7-2.776.2>
        5, 15, 60, 62, 80, "scan", 0, "sort", 0
        -- </where7-2.776.2>
    })

test:do_test(
    "where7-2.777.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1059
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR b=960
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR b=894
         OR c=2002
  ]])
    end, {
        -- <where7-2.777.1>
        4, 5, 6, 12, 16, 20, 42, 68, 94, "scan", 0, "sort", 0
        -- </where7-2.777.1>
    })

test:do_test(
    "where7-2.777.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1059
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR b=960
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR b=894
         OR c=2002
  ]])
    end, {
        -- <where7-2.777.2>
        4, 5, 6, 12, 16, 20, 42, 68, 94, "scan", 0, "sort", 0
        -- </where7-2.777.2>
    })

test:do_test(
    "where7-2.778.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=14
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.778.1>
        85, "scan", 0, "sort", 0
        -- </where7-2.778.1>
    })

test:do_test(
    "where7-2.778.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=14
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.778.2>
        85, "scan", 0, "sort", 0
        -- </where7-2.778.2>
    })

test:do_test(
    "where7-2.779.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=806
         OR (g='rqponml' AND f GLOB 'hijkl*')
         OR b=795
         OR ((a BETWEEN 99 AND 101) AND a!=100)
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR c=23023
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.779.1>
        21, 23, 33, 67, 68, 69, 86, 88, 99, "scan", 0, "sort", 0
        -- </where7-2.779.1>
    })

test:do_test(
    "where7-2.779.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=806
         OR (g='rqponml' AND f GLOB 'hijkl*')
         OR b=795
         OR ((a BETWEEN 99 AND 101) AND a!=100)
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR c=23023
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.779.2>
        21, 23, 33, 67, 68, 69, 86, 88, 99, "scan", 0, "sort", 0
        -- </where7-2.779.2>
    })

test:do_test(
    "where7-2.780.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=726
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR f='abcdefghi'
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR b=869
  ]])
    end, {
        -- <where7-2.780.1>
        8, 10, 15, 26, 41, 52, 66, 67, 78, 79, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.780.1>
    })

test:do_test(
    "where7-2.780.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=726
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR f='abcdefghi'
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR b=869
  ]])
    end, {
        -- <where7-2.780.2>
        8, 10, 15, 26, 41, 52, 66, 67, 78, 79, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.780.2>
    })

test:do_test(
    "where7-2.781.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=59
         OR ((a BETWEEN 5 AND 7) AND a!=6)
         OR b=1081
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
  ]])
    end, {
        -- <where7-2.781.1>
        5, 7, 59, 96, "scan", 0, "sort", 0
        -- </where7-2.781.1>
    })

test:do_test(
    "where7-2.781.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=59
         OR ((a BETWEEN 5 AND 7) AND a!=6)
         OR b=1081
         OR (g='fedcbaz' AND f GLOB 'stuvw*')
  ]])
    end, {
        -- <where7-2.781.2>
        5, 7, 59, 96, "scan", 0, "sort", 0
        -- </where7-2.781.2>
    })

test:do_test(
    "where7-2.782.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='qponmlk' AND f GLOB 'nopqr*')
         OR b=1037
         OR b=132
         OR c=1001
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR a=32
  ]])
    end, {
        -- <where7-2.782.1>
        1, 2, 3, 12, 18, 20, 32, 39, 58, 68, 91, "scan", 0, "sort", 0
        -- </where7-2.782.1>
    })

test:do_test(
    "where7-2.782.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='qponmlk' AND f GLOB 'nopqr*')
         OR b=1037
         OR b=132
         OR c=1001
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR a=32
  ]])
    end, {
        -- <where7-2.782.2>
        1, 2, 3, 12, 18, 20, 32, 39, 58, 68, 91, "scan", 0, "sort", 0
        -- </where7-2.782.2>
    })

test:do_test(
    "where7-2.783.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=24
         OR b=927
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR a=7
         OR b=462
         OR b=608
         OR b=781
         OR b=253
         OR c=25025
         OR b=132
  ]])
    end, {
        -- <where7-2.783.1>
        7, 12, 23, 24, 42, 52, 71, 73, 74, 75, "scan", 0, "sort", 0
        -- </where7-2.783.1>
    })

test:do_test(
    "where7-2.783.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=24
         OR b=927
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR a=7
         OR b=462
         OR b=608
         OR b=781
         OR b=253
         OR c=25025
         OR b=132
  ]])
    end, {
        -- <where7-2.783.2>
        7, 12, 23, 24, 42, 52, 71, 73, 74, 75, "scan", 0, "sort", 0
        -- </where7-2.783.2>
    })

test:do_test(
    "where7-2.784.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=1001
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR a=83
  ]])
    end, {
        -- <where7-2.784.1>
        23, 25, 61, 83, 91, "scan", 0, "sort", 0
        -- </where7-2.784.1>
    })

test:do_test(
    "where7-2.784.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=1001
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR a=83
  ]])
    end, {
        -- <where7-2.784.2>
        23, 25, 61, 83, 91, "scan", 0, "sort", 0
        -- </where7-2.784.2>
    })

test:do_test(
    "where7-2.785.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR b=36
         OR (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR ((a BETWEEN 46 AND 48) AND a!=47)
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR ((a BETWEEN 26 AND 28) AND a!=27)
  ]])
    end, {
        -- <where7-2.785.1>
        3, 26, 28, 29, 31, 33, 46, 48, 55, 60, 73, 77, 80, 81, 82, 91, "scan", 0, "sort", 0
        -- </where7-2.785.1>
    })

test:do_test(
    "where7-2.785.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR b=36
         OR (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR ((a BETWEEN 46 AND 48) AND a!=47)
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR (d>=91.0 AND d<92.0 AND d IS NOT NULL)
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR ((a BETWEEN 26 AND 28) AND a!=27)
  ]])
    end, {
        -- <where7-2.785.2>
        3, 26, 28, 29, 31, 33, 46, 48, 55, 60, 73, 77, 80, 81, 82, 91, "scan", 0, "sort", 0
        -- </where7-2.785.2>
    })

test:do_test(
    "where7-2.786.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=69
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR a=98
         OR b=300
         OR a=41
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR a=33
         OR ((a BETWEEN 10 AND 12) AND a!=11)
  ]])
    end, {
        -- <where7-2.786.1>
        1, 2, 10, 12, 28, 33, 37, 39, 41, 54, 58, 60, 69, 80, 98, "scan", 0, "sort", 0
        -- </where7-2.786.1>
    })

test:do_test(
    "where7-2.786.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=69
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR a=98
         OR b=300
         OR a=41
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR a=33
         OR ((a BETWEEN 10 AND 12) AND a!=11)
  ]])
    end, {
        -- <where7-2.786.2>
        1, 2, 10, 12, 28, 33, 37, 39, 41, 54, 58, 60, 69, 80, 98, "scan", 0, "sort", 0
        -- </where7-2.786.2>
    })

test:do_test(
    "where7-2.787.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 68 AND 70) AND a!=69)
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR ((a BETWEEN 94 AND 96) AND a!=95)
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR b=619
         OR c=6006
         OR b=91
         OR b=297
         OR b=165
  ]])
    end, {
        -- <where7-2.787.1>
        1, 15, 16, 17, 18, 22, 24, 27, 53, 68, 70, 71, 79, 90, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.787.1>
    })

test:do_test(
    "where7-2.787.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 68 AND 70) AND a!=69)
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR ((a BETWEEN 94 AND 96) AND a!=95)
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR b=619
         OR c=6006
         OR b=91
         OR b=297
         OR b=165
  ]])
    end, {
        -- <where7-2.787.2>
        1, 15, 16, 17, 18, 22, 24, 27, 53, 68, 70, 71, 79, 90, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.787.2>
    })

test:do_test(
    "where7-2.788.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 9 AND 11) AND a!=10)
         OR a=55
         OR (g='jihgfed' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.788.1>
        9, 11, 55, 75, "scan", 0, "sort", 0
        -- </where7-2.788.1>
    })

test:do_test(
    "where7-2.788.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 9 AND 11) AND a!=10)
         OR a=55
         OR (g='jihgfed' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.788.2>
        9, 11, 55, 75, "scan", 0, "sort", 0
        -- </where7-2.788.2>
    })

test:do_test(
    "where7-2.789.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 84 AND 86) AND a!=85)
         OR b=737
         OR b=201
         OR a=7
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR b=957
  ]])
    end, {
        -- <where7-2.789.1>
        2, 7, 26, 67, 84, 86, 87, "scan", 0, "sort", 0
        -- </where7-2.789.1>
    })

test:do_test(
    "where7-2.789.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 84 AND 86) AND a!=85)
         OR b=737
         OR b=201
         OR a=7
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR (g='yxwvuts' AND f GLOB 'cdefg*')
         OR b=957
  ]])
    end, {
        -- <where7-2.789.2>
        2, 7, 26, 67, 84, 86, 87, "scan", 0, "sort", 0
        -- </where7-2.789.2>
    })

test:do_test(
    "where7-2.790.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 90 AND 92) AND a!=91)
         OR a=74
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR a=89
  ]])
    end, {
        -- <where7-2.790.1>
        18, 44, 67, 70, 74, 79, 89, 90, 92, 95, 96, 97, "scan", 0, "sort", 0
        -- </where7-2.790.1>
    })

test:do_test(
    "where7-2.790.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 90 AND 92) AND a!=91)
         OR a=74
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR (f GLOB '?tuvw*' AND f GLOB 'stuv*')
         OR a=89
  ]])
    end, {
        -- <where7-2.790.2>
        18, 44, 67, 70, 74, 79, 89, 90, 92, 95, 96, 97, "scan", 0, "sort", 0
        -- </where7-2.790.2>
    })

test:do_test(
    "where7-2.791.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=179
         OR b=1081
         OR b=377
         OR b=495
         OR b=564
         OR b=289
         OR (g='qponmlk' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.791.1>
        39, 45, "scan", 0, "sort", 0
        -- </where7-2.791.1>
    })

test:do_test(
    "where7-2.791.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=179
         OR b=1081
         OR b=377
         OR b=495
         OR b=564
         OR b=289
         OR (g='qponmlk' AND f GLOB 'nopqr*')
  ]])
    end, {
        -- <where7-2.791.2>
        39, 45, "scan", 0, "sort", 0
        -- </where7-2.791.2>
    })

test:do_test(
    "where7-2.792.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='utsrqpo' AND f GLOB 'wxyza*')
         OR a=69
         OR a=12
         OR b=718
         OR ((a BETWEEN 20 AND 22) AND a!=21)
  ]])
    end, {
        -- <where7-2.792.1>
        12, 20, 22, 69, "scan", 0, "sort", 0
        -- </where7-2.792.1>
    })

test:do_test(
    "where7-2.792.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='utsrqpo' AND f GLOB 'wxyza*')
         OR a=69
         OR a=12
         OR b=718
         OR ((a BETWEEN 20 AND 22) AND a!=21)
  ]])
    end, {
        -- <where7-2.792.2>
        12, 20, 22, 69, "scan", 0, "sort", 0
        -- </where7-2.792.2>
    })

test:do_test(
    "where7-2.793.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='tsrqpon' AND f GLOB 'zabcd*')
         OR f='klmnopqrs'
         OR b=674
         OR a=96
         OR a=99
         OR b=608
         OR b=707
         OR f='cdefghijk'
         OR a=91
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.793.1>
        2, 10, 23, 25, 28, 36, 54, 62, 80, 88, 91, 96, 99, "scan", 0, "sort", 0
        -- </where7-2.793.1>
    })

test:do_test(
    "where7-2.793.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='tsrqpon' AND f GLOB 'zabcd*')
         OR f='klmnopqrs'
         OR b=674
         OR a=96
         OR a=99
         OR b=608
         OR b=707
         OR f='cdefghijk'
         OR a=91
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.793.2>
        2, 10, 23, 25, 28, 36, 54, 62, 80, 88, 91, 96, 99, "scan", 0, "sort", 0
        -- </where7-2.793.2>
    })

test:do_test(
    "where7-2.794.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR b=564
         OR b=784
         OR b=418
         OR b=275
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR a=58
         OR c=11011
         OR b=660
  ]])
    end, {
        -- <where7-2.794.1>
        9, 25, 31, 32, 33, 35, 38, 58, 60, 61, 87, 88, "scan", 0, "sort", 0
        -- </where7-2.794.1>
    })

test:do_test(
    "where7-2.794.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR b=564
         OR b=784
         OR b=418
         OR b=275
         OR (g='gfedcba' AND f GLOB 'klmno*')
         OR a=58
         OR c=11011
         OR b=660
  ]])
    end, {
        -- <where7-2.794.2>
        9, 25, 31, 32, 33, 35, 38, 58, 60, 61, 87, 88, "scan", 0, "sort", 0
        -- </where7-2.794.2>
    })

test:do_test(
    "where7-2.795.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR b=509
         OR b=1004
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR (g='mlkjihg' AND f GLOB 'hijkl*')
         OR f='pqrstuvwx'
  ]])
    end, {
        -- <where7-2.795.1>
        15, 25, 28, 30, 41, 57, 59, 67, 93, "scan", 0, "sort", 0
        -- </where7-2.795.1>
    })

test:do_test(
    "where7-2.795.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR b=509
         OR b=1004
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR ((a BETWEEN 57 AND 59) AND a!=58)
         OR (g='mlkjihg' AND f GLOB 'hijkl*')
         OR f='pqrstuvwx'
  ]])
    end, {
        -- <where7-2.795.2>
        15, 25, 28, 30, 41, 57, 59, 67, 93, "scan", 0, "sort", 0
        -- </where7-2.795.2>
    })

test:do_test(
    "where7-2.796.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=440
         OR ((a BETWEEN 52 AND 54) AND a!=53)
  ]])
    end, {
        -- <where7-2.796.1>
        40, 52, 54, "scan", 0, "sort", 0
        -- </where7-2.796.1>
    })

test:do_test(
    "where7-2.796.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=440
         OR ((a BETWEEN 52 AND 54) AND a!=53)
  ]])
    end, {
        -- <where7-2.796.2>
        40, 52, 54, "scan", 0, "sort", 0
        -- </where7-2.796.2>
    })

test:do_test(
    "where7-2.797.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=95.0 AND d<96.0 AND d IS NOT NULL)
         OR f='abcdefghi'
  ]])
    end, {
        -- <where7-2.797.1>
        26, 52, 78, 95, "scan", 0, "sort", 0
        -- </where7-2.797.1>
    })

test:do_test(
    "where7-2.797.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=95.0 AND d<96.0 AND d IS NOT NULL)
         OR f='abcdefghi'
  ]])
    end, {
        -- <where7-2.797.2>
        26, 52, 78, 95, "scan", 0, "sort", 0
        -- </where7-2.797.2>
    })

test:do_test(
    "where7-2.798.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=19
         OR a=29
         OR b=476
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR b=91
  ]])
    end, {
        -- <where7-2.798.1>
        19, 29, 41, "scan", 0, "sort", 0
        -- </where7-2.798.1>
    })

test:do_test(
    "where7-2.798.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=19
         OR a=29
         OR b=476
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR b=91
  ]])
    end, {
        -- <where7-2.798.2>
        19, 29, 41, "scan", 0, "sort", 0
        -- </where7-2.798.2>
    })

test:do_test(
    "where7-2.799.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='lmnopqrst'
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR a=47
         OR a=71
  ]])
    end, {
        -- <where7-2.799.1>
        8, 11, 37, 47, 63, 71, 89, "scan", 0, "sort", 0
        -- </where7-2.799.1>
    })

test:do_test(
    "where7-2.799.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='lmnopqrst'
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR a=47
         OR a=71
  ]])
    end, {
        -- <where7-2.799.2>
        8, 11, 37, 47, 63, 71, 89, "scan", 0, "sort", 0
        -- </where7-2.799.2>
    })

test:do_test(
    "where7-2.800.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=531
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=44
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR (g='onmlkji' AND f GLOB 'xyzab*')
         OR b=707
         OR b=322
  ]])
    end, {
        -- <where7-2.800.1>
        4, 12, 32, 34, 49, 84, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.800.1>
    })

test:do_test(
    "where7-2.800.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=531
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=44
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR (g='onmlkji' AND f GLOB 'xyzab*')
         OR b=707
         OR b=322
  ]])
    end, {
        -- <where7-2.800.2>
        4, 12, 32, 34, 49, 84, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.800.2>
    })

test:do_test(
    "where7-2.801.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR f='jklmnopqr'
  ]])
    end, {
        -- <where7-2.801.1>
        3, 9, 29, 35, 55, 61, 81, 82, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.801.1>
    })

test:do_test(
    "where7-2.801.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?efgh*' AND f GLOB 'defg*')
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR f='jklmnopqr'
  ]])
    end, {
        -- <where7-2.801.2>
        3, 9, 29, 35, 55, 61, 81, 82, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.801.2>
    })

test:do_test(
    "where7-2.802.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=946
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR a=47
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR b=80
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.802.1>
        8, 23, 42, 47, 60, 62, 78, 86, 93, "scan", 0, "sort", 0
        -- </where7-2.802.1>
    })

test:do_test(
    "where7-2.802.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=946
         OR (g='ihgfedc' AND f GLOB 'abcde*')
         OR a=47
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR b=80
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
  ]])
    end, {
        -- <where7-2.802.2>
        8, 23, 42, 47, 60, 62, 78, 86, 93, "scan", 0, "sort", 0
        -- </where7-2.802.2>
    })

test:do_test(
    "where7-2.803.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=48
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR b=1015
         OR a=57
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR ((a BETWEEN 47 AND 49) AND a!=48)
         OR ((a BETWEEN 98 AND 100) AND a!=99)
         OR (g='onmlkji' AND f GLOB 'yzabc*')
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR b=165
  ]])
    end, {
        -- <where7-2.803.1>
        4, 9, 15, 35, 47, 48, 49, 50, 55, 57, 61, 87, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.803.1>
    })

test:do_test(
    "where7-2.803.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=48
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR b=1015
         OR a=57
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR ((a BETWEEN 47 AND 49) AND a!=48)
         OR ((a BETWEEN 98 AND 100) AND a!=99)
         OR (g='onmlkji' AND f GLOB 'yzabc*')
         OR (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR b=165
  ]])
    end, {
        -- <where7-2.803.2>
        4, 9, 15, 35, 47, 48, 49, 50, 55, 57, 61, 87, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.803.2>
    })

test:do_test(
    "where7-2.804.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 97 AND 99) AND a!=98)
         OR a=73
         OR b=1048
         OR c>=34035
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR a=72
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=638
  ]])
    end, {
        -- <where7-2.804.1>
        58, 72, 73, 80, 91, 93, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.804.1>
    })

test:do_test(
    "where7-2.804.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 97 AND 99) AND a!=98)
         OR a=73
         OR b=1048
         OR c>=34035
         OR (g='ihgfedc' AND f GLOB 'cdefg*')
         OR a=72
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=638
  ]])
    end, {
        -- <where7-2.804.2>
        58, 72, 73, 80, 91, 93, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.804.2>
    })

test:do_test(
    "where7-2.805.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 28 AND 30) AND a!=29)
         OR a=39
         OR b=165
  ]])
    end, {
        -- <where7-2.805.1>
        15, 28, 30, 39, "scan", 0, "sort", 0
        -- </where7-2.805.1>
    })

test:do_test(
    "where7-2.805.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 28 AND 30) AND a!=29)
         OR a=39
         OR b=165
  ]])
    end, {
        -- <where7-2.805.2>
        15, 28, 30, 39, "scan", 0, "sort", 0
        -- </where7-2.805.2>
    })

test:do_test(
    "where7-2.806.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=50
         OR ((a BETWEEN 61 AND 63) AND a!=62)
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR a=32
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR a=14
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR b=946
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR b=124
  ]])
    end, {
        -- <where7-2.806.1>
        14, 17, 32, 43, 45, 50, 53, 55, 61, 63, 69, 86, 93, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.806.1>
    })

test:do_test(
    "where7-2.806.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=50
         OR ((a BETWEEN 61 AND 63) AND a!=62)
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR a=32
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR a=14
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR b=946
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR b=124
  ]])
    end, {
        -- <where7-2.806.2>
        14, 17, 32, 43, 45, 50, 53, 55, 61, 63, 69, 86, 93, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.806.2>
    })

test:do_test(
    "where7-2.807.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 88 AND 90) AND a!=89)
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.807.1>
        52, 66, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.807.1>
    })

test:do_test(
    "where7-2.807.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 88 AND 90) AND a!=89)
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.807.2>
        52, 66, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.807.2>
    })

test:do_test(
    "where7-2.808.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=6
         OR f='tuvwxyzab'
         OR (g='mlkjihg' AND f GLOB 'hijkl*')
         OR b=286
         OR b=781
  ]])
    end, {
        -- <where7-2.808.1>
        6, 19, 26, 45, 59, 71, 97, "scan", 0, "sort", 0
        -- </where7-2.808.1>
    })

test:do_test(
    "where7-2.808.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=6
         OR f='tuvwxyzab'
         OR (g='mlkjihg' AND f GLOB 'hijkl*')
         OR b=286
         OR b=781
  ]])
    end, {
        -- <where7-2.808.2>
        6, 19, 26, 45, 59, 71, 97, "scan", 0, "sort", 0
        -- </where7-2.808.2>
    })

test:do_test(
    "where7-2.809.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='jihgfed' AND f GLOB 'zabcd*')
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR ((a BETWEEN 79 AND 81) AND a!=80)
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR f='vwxyzabcd'
         OR b=275
  ]])
    end, {
        -- <where7-2.809.1>
        9, 11, 21, 25, 35, 37, 43, 47, 61, 63, 73, 77, 79, 81, 87, 89, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.809.1>
    })

test:do_test(
    "where7-2.809.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='jihgfed' AND f GLOB 'zabcd*')
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR ((a BETWEEN 79 AND 81) AND a!=80)
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR f='vwxyzabcd'
         OR b=275
  ]])
    end, {
        -- <where7-2.809.2>
        9, 11, 21, 25, 35, 37, 43, 47, 61, 63, 73, 77, 79, 81, 87, 89, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.809.2>
    })

test:do_test(
    "where7-2.810.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=30.0 AND d<31.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR a=59
  ]])
    end, {
        -- <where7-2.810.1>
        4, 30, 59, 64, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.810.1>
    })

test:do_test(
    "where7-2.810.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=30.0 AND d<31.0 AND d IS NOT NULL)
         OR (g='xwvutsr' AND f GLOB 'efghi*')
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'nopqr*')
         OR a=59
  ]])
    end, {
        -- <where7-2.810.2>
        4, 30, 59, 64, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.810.2>
    })

test:do_test(
    "where7-2.811.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?xyza*' AND f GLOB 'wxyz*')
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR b=663
         OR f='ghijklmno'
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR f='ghijklmno'
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR b=1081
  ]])
    end, {
        -- <where7-2.811.1>
        6, 8, 10, 14, 16, 19, 22, 32, 48, 58, 64, 71, 74, 84, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.811.1>
    })

test:do_test(
    "where7-2.811.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?xyza*' AND f GLOB 'wxyz*')
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR b=663
         OR f='ghijklmno'
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR f='ghijklmno'
         OR (d>=64.0 AND d<65.0 AND d IS NOT NULL)
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
         OR b=1081
  ]])
    end, {
        -- <where7-2.811.2>
        6, 8, 10, 14, 16, 19, 22, 32, 48, 58, 64, 71, 74, 84, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.811.2>
    })

test:do_test(
    "where7-2.812.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 30 AND 32) AND a!=31)
         OR a=96
         OR b=355
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR b=597
         OR ((a BETWEEN 92 AND 94) AND a!=93)
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR (f GLOB '?lmno*' AND f GLOB 'klmn*')
         OR b=168
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
  ]])
    end, {
        -- <where7-2.812.1>
        10, 15, 30, 32, 36, 62, 81, 88, 92, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.812.1>
    })

test:do_test(
    "where7-2.812.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 30 AND 32) AND a!=31)
         OR a=96
         OR b=355
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR b=597
         OR ((a BETWEEN 92 AND 94) AND a!=93)
         OR (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR (f GLOB '?lmno*' AND f GLOB 'klmn*')
         OR b=168
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
  ]])
    end, {
        -- <where7-2.812.2>
        10, 15, 30, 32, 36, 62, 81, 88, 92, 94, 96, "scan", 0, "sort", 0
        -- </where7-2.812.2>
    })

test:do_test(
    "where7-2.813.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR b=957
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR a=40
  ]])
    end, {
        -- <where7-2.813.1>
        9, 40, 47, 58, 60, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.813.1>
    })

test:do_test(
    "where7-2.813.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=47.0 AND d<48.0 AND d IS NOT NULL)
         OR b=957
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR a=40
  ]])
    end, {
        -- <where7-2.813.2>
        9, 40, 47, 58, 60, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.813.2>
    })

test:do_test(
    "where7-2.814.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 12 AND 14) AND a!=13)
         OR a=36
         OR a=75
         OR b=179
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
         OR b=850
         OR a=62
  ]])
    end, {
        -- <where7-2.814.1>
        12, 14, 18, 36, 43, 62, 65, 75, "scan", 0, "sort", 0
        -- </where7-2.814.1>
    })

test:do_test(
    "where7-2.814.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 12 AND 14) AND a!=13)
         OR a=36
         OR a=75
         OR b=179
         OR (d>=43.0 AND d<44.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'stuvw*')
         OR (d>=65.0 AND d<66.0 AND d IS NOT NULL)
         OR b=850
         OR a=62
  ]])
    end, {
        -- <where7-2.814.2>
        12, 14, 18, 36, 43, 62, 65, 75, "scan", 0, "sort", 0
        -- </where7-2.814.2>
    })

test:do_test(
    "where7-2.815.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 21 AND 23) AND a!=22)
         OR a=79
         OR a=66
         OR b=487
         OR a=1
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR c=5005
         OR a=47
         OR c=5005
         OR b=319
         OR b=1037
  ]])
    end, {
        -- <where7-2.815.1>
        1, 13, 14, 15, 21, 23, 29, 47, 54, 66, 79, "scan", 0, "sort", 0
        -- </where7-2.815.1>
    })

test:do_test(
    "where7-2.815.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 21 AND 23) AND a!=22)
         OR a=79
         OR a=66
         OR b=487
         OR a=1
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR c=5005
         OR a=47
         OR c=5005
         OR b=319
         OR b=1037
  ]])
    end, {
        -- <where7-2.815.2>
        1, 13, 14, 15, 21, 23, 29, 47, 54, 66, 79, "scan", 0, "sort", 0
        -- </where7-2.815.2>
    })

test:do_test(
    "where7-2.816.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=176
         OR b=297
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR f='ijklmnopq'
  ]])
    end, {
        -- <where7-2.816.1>
        8, 16, 25, 27, 34, 60, 86, "scan", 0, "sort", 0
        -- </where7-2.816.1>
    })

test:do_test(
    "where7-2.816.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=176
         OR b=297
         OR (g='tsrqpon' AND f GLOB 'zabcd*')
         OR f='ijklmnopq'
  ]])
    end, {
        -- <where7-2.816.2>
        8, 16, 25, 27, 34, 60, 86, "scan", 0, "sort", 0
        -- </where7-2.816.2>
    })

test:do_test(
    "where7-2.817.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR b=319
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR a=21
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.817.1>
        9, 10, 11, 21, 29, 31, 33, 72, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.817.1>
    })

test:do_test(
    "where7-2.817.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR ((a BETWEEN 90 AND 92) AND a!=91)
         OR b=319
         OR ((a BETWEEN 31 AND 33) AND a!=32)
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR a=21
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.817.2>
        9, 10, 11, 21, 29, 31, 33, 72, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.817.2>
    })

test:do_test(
    "where7-2.818.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR b=396
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR b=1012
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR b=784
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR b=979
         OR c<=10
         OR b=913
         OR b=66
  ]])
    end, {
        -- <where7-2.818.1>
        6, 9, 22, 35, 36, 60, 61, 72, 83, 87, 89, 92, "scan", 0, "sort", 0
        -- </where7-2.818.1>
    })

test:do_test(
    "where7-2.818.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR b=396
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR b=1012
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR b=784
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR b=979
         OR c<=10
         OR b=913
         OR b=66
  ]])
    end, {
        -- <where7-2.818.2>
        6, 9, 22, 35, 36, 60, 61, 72, 83, 87, 89, 92, "scan", 0, "sort", 0
        -- </where7-2.818.2>
    })

test:do_test(
    "where7-2.819.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=3
         OR b=803
  ]])
    end, {
        -- <where7-2.819.1>
        3, 73, "scan", 0, "sort", 0
        -- </where7-2.819.1>
    })

test:do_test(
    "where7-2.819.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=3
         OR b=803
  ]])
    end, {
        -- <where7-2.819.2>
        3, 73, "scan", 0, "sort", 0
        -- </where7-2.819.2>
    })

test:do_test(
    "where7-2.820.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 79 AND 81) AND a!=80)
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
  ]])
    end, {
        -- <where7-2.820.1>
        16, 19, 23, 25, 42, 45, 68, 71, 79, 81, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.820.1>
    })

test:do_test(
    "where7-2.820.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 79 AND 81) AND a!=80)
         OR (f GLOB '?rstu*' AND f GLOB 'qrst*')
         OR ((a BETWEEN 23 AND 25) AND a!=24)
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
  ]])
    end, {
        -- <where7-2.820.2>
        16, 19, 23, 25, 42, 45, 68, 71, 79, 81, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.820.2>
    })

test:do_test(
    "where7-2.821.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=1001
         OR a=16
         OR b=132
         OR b=1012
         OR f='xyzabcdef'
         OR b=682
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.821.1>
        1, 2, 3, 12, 16, 23, 49, 52, 62, 75, 92, "scan", 0, "sort", 0
        -- </where7-2.821.1>
    })

test:do_test(
    "where7-2.821.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=1001
         OR a=16
         OR b=132
         OR b=1012
         OR f='xyzabcdef'
         OR b=682
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.821.2>
        1, 2, 3, 12, 16, 23, 49, 52, 62, 75, 92, "scan", 0, "sort", 0
        -- </where7-2.821.2>
    })

test:do_test(
    "where7-2.822.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=96
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.822.1>
        19, 96, "scan", 0, "sort", 0
        -- </where7-2.822.1>
    })

test:do_test(
    "where7-2.822.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=96
         OR (d>=19.0 AND d<20.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.822.2>
        19, 96, "scan", 0, "sort", 0
        -- </where7-2.822.2>
    })

test:do_test(
    "where7-2.823.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=2
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR a=23
         OR b=1092
         OR c=19019
         OR b=245
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR (f GLOB '?nopq*' AND f GLOB 'mnop*')
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR b=572
         OR ((a BETWEEN 22 AND 24) AND a!=23)
  ]])
    end, {
        -- <where7-2.823.1>
        2, 11, 12, 22, 23, 24, 38, 52, 55, 56, 57, 64, 68, 70, 90, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.823.1>
    })

test:do_test(
    "where7-2.823.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=2
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
         OR a=23
         OR b=1092
         OR c=19019
         OR b=245
         OR ((a BETWEEN 97 AND 99) AND a!=98)
         OR (f GLOB '?nopq*' AND f GLOB 'mnop*')
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR b=572
         OR ((a BETWEEN 22 AND 24) AND a!=23)
  ]])
    end, {
        -- <where7-2.823.2>
        2, 11, 12, 22, 23, 24, 38, 52, 55, 56, 57, 64, 68, 70, 90, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.823.2>
    })

test:do_test(
    "where7-2.824.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=993
         OR c=17017
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR b=333
  ]])
    end, {
        -- <where7-2.824.1>
        16, 29, 37, 49, 50, 51, 53, 85, "scan", 0, "sort", 0
        -- </where7-2.824.1>
    })

test:do_test(
    "where7-2.824.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR b=993
         OR c=17017
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR b=333
  ]])
    end, {
        -- <where7-2.824.2>
        16, 29, 37, 49, 50, 51, 53, 85, "scan", 0, "sort", 0
        -- </where7-2.824.2>
    })

test:do_test(
    "where7-2.825.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=330
         OR a=73
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR b=828
         OR b=363
         OR (g='rqponml' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.825.1>
        30, 33, 37, 40, 61, 73, "scan", 0, "sort", 0
        -- </where7-2.825.1>
    })

test:do_test(
    "where7-2.825.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=330
         OR a=73
         OR (d>=61.0 AND d<62.0 AND d IS NOT NULL)
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR b=828
         OR b=363
         OR (g='rqponml' AND f GLOB 'lmnop*')
  ]])
    end, {
        -- <where7-2.825.2>
        30, 33, 37, 40, 61, 73, "scan", 0, "sort", 0
        -- </where7-2.825.2>
    })

test:do_test(
    "where7-2.826.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='gfedcba' AND f GLOB 'lmnop*')
         OR a=41
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR b=825
  ]])
    end, {
        -- <where7-2.826.1>
        29, 41, 75, 89, "scan", 0, "sort", 0
        -- </where7-2.826.1>
    })

test:do_test(
    "where7-2.826.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='gfedcba' AND f GLOB 'lmnop*')
         OR a=41
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR b=825
  ]])
    end, {
        -- <where7-2.826.2>
        29, 41, 75, 89, "scan", 0, "sort", 0
        -- </where7-2.826.2>
    })

test:do_test(
    "where7-2.827.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 46 AND 48) AND a!=47)
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR b=905
         OR b=176
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR b=561
         OR c=8008
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR b=935
         OR c=1001
  ]])
    end, {
        -- <where7-2.827.1>
        1, 2, 3, 10, 16, 22, 23, 24, 46, 48, 51, 84, 85, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.827.1>
    })

test:do_test(
    "where7-2.827.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 46 AND 48) AND a!=47)
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
         OR b=905
         OR b=176
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR b=561
         OR c=8008
         OR (g='hgfedcb' AND f GLOB 'ghijk*')
         OR b=935
         OR c=1001
  ]])
    end, {
        -- <where7-2.827.2>
        1, 2, 3, 10, 16, 22, 23, 24, 46, 48, 51, 84, 85, 89, 91, "scan", 0, "sort", 0
        -- </where7-2.827.2>
    })

test:do_test(
    "where7-2.828.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 75 AND 77) AND a!=76)
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.828.1>
        72, 75, 77, "scan", 0, "sort", 0
        -- </where7-2.828.1>
    })

test:do_test(
    "where7-2.828.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 75 AND 77) AND a!=76)
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.828.2>
        72, 75, 77, "scan", 0, "sort", 0
        -- </where7-2.828.2>
    })

test:do_test(
    "where7-2.829.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 5 AND 7) AND a!=6)
         OR a=28
  ]])
    end, {
        -- <where7-2.829.1>
        5, 7, 28, "scan", 0, "sort", 0
        -- </where7-2.829.1>
    })

test:do_test(
    "where7-2.829.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 5 AND 7) AND a!=6)
         OR a=28
  ]])
    end, {
        -- <where7-2.829.2>
        5, 7, 28, "scan", 0, "sort", 0
        -- </where7-2.829.2>
    })

test:do_test(
    "where7-2.830.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=781
         OR b=410
  ]])
    end, {
        -- <where7-2.830.1>
        71, "scan", 0, "sort", 0
        -- </where7-2.830.1>
    })

test:do_test(
    "where7-2.830.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=781
         OR b=410
  ]])
    end, {
        -- <where7-2.830.2>
        71, "scan", 0, "sort", 0
        -- </where7-2.830.2>
    })

test:do_test(
    "where7-2.831.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 18 AND 20) AND a!=19)
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR f='zabcdefgh'
         OR b=861
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR a=28
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR b=311
  ]])
    end, {
        -- <where7-2.831.1>
        6, 15, 18, 20, 25, 28, 32, 40, 42, 51, 56, 58, 63, 72, 77, 84, "scan", 0, "sort", 0
        -- </where7-2.831.1>
    })

test:do_test(
    "where7-2.831.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 18 AND 20) AND a!=19)
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR f='zabcdefgh'
         OR b=861
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
         OR a=28
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR b=311
  ]])
    end, {
        -- <where7-2.831.2>
        6, 15, 18, 20, 25, 28, 32, 40, 42, 51, 56, 58, 63, 72, 77, 84, "scan", 0, "sort", 0
        -- </where7-2.831.2>
    })

test:do_test(
    "where7-2.832.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=575
         OR (f GLOB '?nopq*' AND f GLOB 'mnop*')
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR b=418
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR b=792
         OR b=861
         OR b=220
         OR ((a BETWEEN 89 AND 91) AND a!=90)
  ]])
    end, {
        -- <where7-2.832.1>
        12, 15, 20, 38, 41, 64, 67, 72, 73, 89, 90, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.832.1>
    })

test:do_test(
    "where7-2.832.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=575
         OR (f GLOB '?nopq*' AND f GLOB 'mnop*')
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR b=418
         OR (f GLOB '?qrst*' AND f GLOB 'pqrs*')
         OR b=792
         OR b=861
         OR b=220
         OR ((a BETWEEN 89 AND 91) AND a!=90)
  ]])
    end, {
        -- <where7-2.832.2>
        12, 15, 20, 38, 41, 64, 67, 72, 73, 89, 90, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.832.2>
    })

test:do_test(
    "where7-2.833.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=517
         OR b=913
         OR b=253
         OR b=198
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR a=17
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.833.1>
        17, 18, 23, 27, 47, 83, 85, "scan", 0, "sort", 0
        -- </where7-2.833.1>
    })

test:do_test(
    "where7-2.833.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=517
         OR b=913
         OR b=253
         OR b=198
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR a=17
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.833.2>
        17, 18, 23, 27, 47, 83, 85, "scan", 0, "sort", 0
        -- </where7-2.833.2>
    })

test:do_test(
    "where7-2.834.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='qponmlk' AND f GLOB 'qrstu*')
         OR b=693
         OR a=73
         OR b=627
         OR c=5005
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR b=267
         OR b=872
         OR a=27
         OR (g='gfedcba' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.834.1>
        13, 14, 15, 27, 28, 42, 57, 62, 63, 73, 88, "scan", 0, "sort", 0
        -- </where7-2.834.1>
    })

test:do_test(
    "where7-2.834.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='qponmlk' AND f GLOB 'qrstu*')
         OR b=693
         OR a=73
         OR b=627
         OR c=5005
         OR (d>=62.0 AND d<63.0 AND d IS NOT NULL)
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR b=267
         OR b=872
         OR a=27
         OR (g='gfedcba' AND f GLOB 'klmno*')
  ]])
    end, {
        -- <where7-2.834.2>
        13, 14, 15, 27, 28, 42, 57, 62, 63, 73, 88, "scan", 0, "sort", 0
        -- </where7-2.834.2>
    })

test:do_test(
    "where7-2.835.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=10
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR ((a BETWEEN 13 AND 15) AND a!=14)
         OR b=1059
         OR a=70
         OR a=93
  ]])
    end, {
        -- <where7-2.835.1>
        10, 13, 15, 70, 93, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.835.1>
    })

test:do_test(
    "where7-2.835.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=10
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR ((a BETWEEN 13 AND 15) AND a!=14)
         OR b=1059
         OR a=70
         OR a=93
  ]])
    end, {
        -- <where7-2.835.2>
        10, 13, 15, 70, 93, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.835.2>
    })

test:do_test(
    "where7-2.836.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=30
         OR a=32
         OR b=1037
         OR b=198
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR a=25
  ]])
    end, {
        -- <where7-2.836.1>
        13, 18, 25, 30, 32, "scan", 0, "sort", 0
        -- </where7-2.836.1>
    })

test:do_test(
    "where7-2.836.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=30
         OR a=32
         OR b=1037
         OR b=198
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR a=25
  ]])
    end, {
        -- <where7-2.836.2>
        13, 18, 25, 30, 32, "scan", 0, "sort", 0
        -- </where7-2.836.2>
    })

test:do_test(
    "where7-2.837.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR b=66
         OR b=322
         OR b=465
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR (g='lkjihgf' AND f GLOB 'mnopq*')
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR b=454
  ]])
    end, {
        -- <where7-2.837.1>
        6, 7, 38, 46, 64, 77, 79, 89, "scan", 0, "sort", 0
        -- </where7-2.837.1>
    })

test:do_test(
    "where7-2.837.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR b=66
         OR b=322
         OR b=465
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR (d>=7.0 AND d<8.0 AND d IS NOT NULL)
         OR ((a BETWEEN 77 AND 79) AND a!=78)
         OR (g='lkjihgf' AND f GLOB 'mnopq*')
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR b=454
  ]])
    end, {
        -- <where7-2.837.2>
        6, 7, 38, 46, 64, 77, 79, 89, "scan", 0, "sort", 0
        -- </where7-2.837.2>
    })

test:do_test(
    "where7-2.838.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=924
         OR ((a BETWEEN 35 AND 37) AND a!=36)
         OR c=15015
         OR (d>=84.0 AND d<85.0 AND d IS NOT NULL)
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR b=803
  ]])
    end, {
        -- <where7-2.838.1>
        3, 5, 35, 37, 43, 44, 45, 52, 73, 84, "scan", 0, "sort", 0
        -- </where7-2.838.1>
    })

test:do_test(
    "where7-2.838.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=924
         OR ((a BETWEEN 35 AND 37) AND a!=36)
         OR c=15015
         OR (d>=84.0 AND d<85.0 AND d IS NOT NULL)
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR b=803
  ]])
    end, {
        -- <where7-2.838.2>
        3, 5, 35, 37, 43, 44, 45, 52, 73, 84, "scan", 0, "sort", 0
        -- </where7-2.838.2>
    })

test:do_test(
    "where7-2.839.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1100
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR a=75
         OR a=45
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR a=27
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR b=850
         OR ((a BETWEEN 55 AND 57) AND a!=56)
  ]])
    end, {
        -- <where7-2.839.1>
        12, 27, 45, 55, 57, 68, 70, 72, 74, 75, 77, 90, 100, "scan", 0, "sort", 0
        -- </where7-2.839.1>
    })

test:do_test(
    "where7-2.839.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1100
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 72 AND 74) AND a!=73)
         OR ((a BETWEEN 68 AND 70) AND a!=69)
         OR a=75
         OR a=45
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR a=27
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR b=850
         OR ((a BETWEEN 55 AND 57) AND a!=56)
  ]])
    end, {
        -- <where7-2.839.2>
        12, 27, 45, 55, 57, 68, 70, 72, 74, 75, 77, 90, 100, "scan", 0, "sort", 0
        -- </where7-2.839.2>
    })

test:do_test(
    "where7-2.840.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=751
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR a=89
         OR ((a BETWEEN 36 AND 38) AND a!=37)
  ]])
    end, {
        -- <where7-2.840.1>
        36, 38, 56, 71, 89, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.840.1>
    })

test:do_test(
    "where7-2.840.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=751
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR (d>=71.0 AND d<72.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR a=89
         OR ((a BETWEEN 36 AND 38) AND a!=37)
  ]])
    end, {
        -- <where7-2.840.2>
        36, 38, 56, 71, 89, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.840.2>
    })

test:do_test(
    "where7-2.841.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='wvutsrq' AND f GLOB 'jklmn*')
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR a=1
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
  ]])
    end, {
        -- <where7-2.841.1>
        1, 9, 19, "scan", 0, "sort", 0
        -- </where7-2.841.1>
    })

test:do_test(
    "where7-2.841.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='wvutsrq' AND f GLOB 'jklmn*')
         OR (g='yxwvuts' AND f GLOB 'bcdef*')
         OR a=1
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
  ]])
    end, {
        -- <where7-2.841.2>
        1, 9, 19, "scan", 0, "sort", 0
        -- </where7-2.841.2>
    })

test:do_test(
    "where7-2.842.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=927
         OR c=15015
         OR f='klmnopqrs'
         OR c=8008
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR b=960
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR b=443
         OR (g='rqponml' AND f GLOB 'ijklm*')
  ]])
    end, {
        -- <where7-2.842.1>
        10, 22, 23, 24, 34, 36, 41, 43, 44, 45, 62, 76, 88, "scan", 0, "sort", 0
        -- </where7-2.842.1>
    })

test:do_test(
    "where7-2.842.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=927
         OR c=15015
         OR f='klmnopqrs'
         OR c=8008
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR b=960
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR b=443
         OR (g='rqponml' AND f GLOB 'ijklm*')
  ]])
    end, {
        -- <where7-2.842.2>
        10, 22, 23, 24, 34, 36, 41, 43, 44, 45, 62, 76, 88, "scan", 0, "sort", 0
        -- </where7-2.842.2>
    })

test:do_test(
    "where7-2.843.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR b=212
         OR f='cdefghijk'
  ]])
    end, {
        -- <where7-2.843.1>
        2, 28, 37, 54, 80, "scan", 0, "sort", 0
        -- </where7-2.843.1>
    })

test:do_test(
    "where7-2.843.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR b=212
         OR f='cdefghijk'
  ]])
    end, {
        -- <where7-2.843.2>
        2, 28, 37, 54, 80, "scan", 0, "sort", 0
        -- </where7-2.843.2>
    })

test:do_test(
    "where7-2.844.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=685
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR b=520
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR a=53
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=938
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR c=25025
  ]])
    end, {
        -- <where7-2.844.1>
        43, 53, 63, 73, 74, 75, 76, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.844.1>
    })

test:do_test(
    "where7-2.844.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=685
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR b=520
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR a=53
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=938
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR c=25025
  ]])
    end, {
        -- <where7-2.844.2>
        43, 53, 63, 73, 74, 75, 76, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.844.2>
    })

test:do_test(
    "where7-2.845.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=583
         OR b=894
         OR c=26026
         OR (d>=84.0 AND d<85.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.845.1>
        53, 76, 77, 78, 84, "scan", 0, "sort", 0
        -- </where7-2.845.1>
    })

test:do_test(
    "where7-2.845.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=583
         OR b=894
         OR c=26026
         OR (d>=84.0 AND d<85.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.845.2>
        53, 76, 77, 78, 84, "scan", 0, "sort", 0
        -- </where7-2.845.2>
    })

test:do_test(
    "where7-2.846.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='abcdefghi'
         OR (g='edcbazy' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.846.1>
        26, 52, 78, 100, "scan", 0, "sort", 0
        -- </where7-2.846.1>
    })

test:do_test(
    "where7-2.846.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='abcdefghi'
         OR (g='edcbazy' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.846.2>
        26, 52, 78, 100, "scan", 0, "sort", 0
        -- </where7-2.846.2>
    })

test:do_test(
    "where7-2.847.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1103
         OR b=638
         OR b=792
         OR b=1034
         OR b=308
         OR f='nopqrstuv'
         OR b=264
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.847.1>
        13, 24, 28, 33, 35, 39, 58, 65, 72, 91, 94, "scan", 0, "sort", 0
        -- </where7-2.847.1>
    })

test:do_test(
    "where7-2.847.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1103
         OR b=638
         OR b=792
         OR b=1034
         OR b=308
         OR f='nopqrstuv'
         OR b=264
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR (d>=58.0 AND d<59.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.847.2>
        13, 24, 28, 33, 35, 39, 58, 65, 72, 91, 94, "scan", 0, "sort", 0
        -- </where7-2.847.2>
    })

test:do_test(
    "where7-2.848.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='defghijkl'
         OR b=814
         OR f='yzabcdefg'
  ]])
    end, {
        -- <where7-2.848.1>
        3, 24, 29, 50, 55, 74, 76, 81, "scan", 0, "sort", 0
        -- </where7-2.848.1>
    })

test:do_test(
    "where7-2.848.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='defghijkl'
         OR b=814
         OR f='yzabcdefg'
  ]])
    end, {
        -- <where7-2.848.2>
        3, 24, 29, 50, 55, 74, 76, 81, "scan", 0, "sort", 0
        -- </where7-2.848.2>
    })

test:do_test(
    "where7-2.849.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=209
         OR b=806
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.849.1>
        8, 17, 19, "scan", 0, "sort", 0
        -- </where7-2.849.1>
    })

test:do_test(
    "where7-2.849.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=209
         OR b=806
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'rstuv*')
  ]])
    end, {
        -- <where7-2.849.2>
        8, 17, 19, "scan", 0, "sort", 0
        -- </where7-2.849.2>
    })

test:do_test(
    "where7-2.850.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='opqrstuvw'
         OR b=69
         OR b=366
  ]])
    end, {
        -- <where7-2.850.1>
        14, 40, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.850.1>
    })

test:do_test(
    "where7-2.850.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='opqrstuvw'
         OR b=69
         OR b=366
  ]])
    end, {
        -- <where7-2.850.2>
        14, 40, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.850.2>
    })

test:do_test(
    "where7-2.851.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR a=45
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR a=69
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.851.1>
        4, 45, 69, 71, 72, "scan", 0, "sort", 0
        -- </where7-2.851.1>
    })

test:do_test(
    "where7-2.851.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR a=45
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR a=69
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.851.2>
        4, 45, 69, 71, 72, "scan", 0, "sort", 0
        -- </where7-2.851.2>
    })

test:do_test(
    "where7-2.852.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=9009
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
  ]])
    end, {
        -- <where7-2.852.1>
        9, 10, 25, 26, 27, 67, 85, "scan", 0, "sort", 0
        -- </where7-2.852.1>
    })

test:do_test(
    "where7-2.852.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=9009
         OR (d>=85.0 AND d<86.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR (d>=9.0 AND d<10.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'pqrst*')
  ]])
    end, {
        -- <where7-2.852.2>
        9, 10, 25, 26, 27, 67, 85, "scan", 0, "sort", 0
        -- </where7-2.852.2>
    })

test:do_test(
    "where7-2.853.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=98
         OR (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR a=47
         OR c=24024
         OR a=27
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.853.1>
        20, 27, 45, 47, 63, 70, 71, 72, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.853.1>
    })

test:do_test(
    "where7-2.853.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=98
         OR (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR a=47
         OR c=24024
         OR a=27
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.853.2>
        20, 27, 45, 47, 63, 70, 71, 72, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.853.2>
    })

test:do_test(
    "where7-2.854.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR a=19
  ]])
    end, {
        -- <where7-2.854.1>
        19, 22, 44, "scan", 0, "sort", 0
        -- </where7-2.854.1>
    })

test:do_test(
    "where7-2.854.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR a=19
  ]])
    end, {
        -- <where7-2.854.2>
        19, 22, 44, "scan", 0, "sort", 0
        -- </where7-2.854.2>
    })

test:do_test(
    "where7-2.855.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=12012
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
         OR ((a BETWEEN 16 AND 18) AND a!=17)
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.855.1>
        16, 18, 34, 35, 36, 80, 98, "scan", 0, "sort", 0
        -- </where7-2.855.1>
    })

test:do_test(
    "where7-2.855.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=12012
         OR (d>=80.0 AND d<81.0 AND d IS NOT NULL)
         OR ((a BETWEEN 16 AND 18) AND a!=17)
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.855.2>
        16, 18, 34, 35, 36, 80, 98, "scan", 0, "sort", 0
        -- </where7-2.855.2>
    })

test:do_test(
    "where7-2.856.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 38 AND 40) AND a!=39)
         OR (f GLOB '?nopq*' AND f GLOB 'mnop*')
         OR b=429
         OR f='jklmnopqr'
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR ((a BETWEEN 77 AND 79) AND a!=78)
  ]])
    end, {
        -- <where7-2.856.1>
        9, 12, 35, 38, 39, 40, 48, 61, 64, 77, 79, 87, 90, "scan", 0, "sort", 0
        -- </where7-2.856.1>
    })

test:do_test(
    "where7-2.856.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 38 AND 40) AND a!=39)
         OR (f GLOB '?nopq*' AND f GLOB 'mnop*')
         OR b=429
         OR f='jklmnopqr'
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR ((a BETWEEN 77 AND 79) AND a!=78)
  ]])
    end, {
        -- <where7-2.856.2>
        9, 12, 35, 38, 39, 40, 48, 61, 64, 77, 79, 87, 90, "scan", 0, "sort", 0
        -- </where7-2.856.2>
    })

test:do_test(
    "where7-2.857.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='lkjihgf' AND f GLOB 'mnopq*')
         OR b=190
  ]])
    end, {
        -- <where7-2.857.1>
        64, "scan", 0, "sort", 0
        -- </where7-2.857.1>
    })

test:do_test(
    "where7-2.857.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='lkjihgf' AND f GLOB 'mnopq*')
         OR b=190
  ]])
    end, {
        -- <where7-2.857.2>
        64, "scan", 0, "sort", 0
        -- </where7-2.857.2>
    })

test:do_test(
    "where7-2.858.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='jihgfed' AND f GLOB 'yzabc*')
         OR b=674
         OR b=289
  ]])
    end, {
        -- <where7-2.858.1>
        76, "scan", 0, "sort", 0
        -- </where7-2.858.1>
    })

test:do_test(
    "where7-2.858.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='jihgfed' AND f GLOB 'yzabc*')
         OR b=674
         OR b=289
  ]])
    end, {
        -- <where7-2.858.2>
        76, "scan", 0, "sort", 0
        -- </where7-2.858.2>
    })

test:do_test(
    "where7-2.859.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=17
         OR b=539
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.859.1>
        17, 21, 47, 49, "scan", 0, "sort", 0
        -- </where7-2.859.1>
    })

test:do_test(
    "where7-2.859.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=17
         OR b=539
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR (g='utsrqpo' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.859.2>
        17, 21, 47, 49, "scan", 0, "sort", 0
        -- </where7-2.859.2>
    })

test:do_test(
    "where7-2.860.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=924
         OR c=27027
         OR ((a BETWEEN 65 AND 67) AND a!=66)
  ]])
    end, {
        -- <where7-2.860.1>
        65, 67, 79, 80, 81, 84, "scan", 0, "sort", 0
        -- </where7-2.860.1>
    })

test:do_test(
    "where7-2.860.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=924
         OR c=27027
         OR ((a BETWEEN 65 AND 67) AND a!=66)
  ]])
    end, {
        -- <where7-2.860.2>
        65, 67, 79, 80, 81, 84, "scan", 0, "sort", 0
        -- </where7-2.860.2>
    })

test:do_test(
    "where7-2.861.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=564
         OR f='mnopqrstu'
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR b=1103
  ]])
    end, {
        -- <where7-2.861.1>
        12, 28, 30, 38, 64, 90, "scan", 0, "sort", 0
        -- </where7-2.861.1>
    })

test:do_test(
    "where7-2.861.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=564
         OR f='mnopqrstu'
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR b=1103
  ]])
    end, {
        -- <where7-2.861.2>
        12, 28, 30, 38, 64, 90, "scan", 0, "sort", 0
        -- </where7-2.861.2>
    })

test:do_test(
    "where7-2.862.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=231
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR a=38
         OR a=4
         OR b=784
  ]])
    end, {
        -- <where7-2.862.1>
        4, 21, 24, 38, "scan", 0, "sort", 0
        -- </where7-2.862.1>
    })

test:do_test(
    "where7-2.862.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=231
         OR (d>=24.0 AND d<25.0 AND d IS NOT NULL)
         OR a=38
         OR a=4
         OR b=784
  ]])
    end, {
        -- <where7-2.862.2>
        4, 21, 24, 38, "scan", 0, "sort", 0
        -- </where7-2.862.2>
    })

test:do_test(
    "where7-2.863.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='ghijklmno'
         OR a=26
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR a=81
         OR (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR b=275
         OR (g='hgfedcb' AND f GLOB 'jklmn*')
         OR b=311
         OR b=894
         OR b=872
  ]])
    end, {
        -- <where7-2.863.1>
        3, 6, 25, 26, 28, 30, 32, 58, 68, 81, 84, 87, "scan", 0, "sort", 0
        -- </where7-2.863.1>
    })

test:do_test(
    "where7-2.863.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='ghijklmno'
         OR a=26
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR a=81
         OR (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR ((a BETWEEN 28 AND 30) AND a!=29)
         OR b=275
         OR (g='hgfedcb' AND f GLOB 'jklmn*')
         OR b=311
         OR b=894
         OR b=872
  ]])
    end, {
        -- <where7-2.863.2>
        3, 6, 25, 26, 28, 30, 32, 58, 68, 81, 84, 87, "scan", 0, "sort", 0
        -- </where7-2.863.2>
    })

test:do_test(
    "where7-2.864.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=91
         OR b=619
  ]])
    end, {
        -- <where7-2.864.1>
        91, "scan", 0, "sort", 0
        -- </where7-2.864.1>
    })

test:do_test(
    "where7-2.864.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=91
         OR b=619
  ]])
    end, {
        -- <where7-2.864.2>
        91, "scan", 0, "sort", 0
        -- </where7-2.864.2>
    })

test:do_test(
    "where7-2.865.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR a=85
         OR f IS NULL
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=154
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.865.1>
        4, 14, 25, 40, 42, 49, 51, 66, 68, 85, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.865.1>
    })

test:do_test(
    "where7-2.865.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=4.0 AND d<5.0 AND d IS NOT NULL)
         OR a=85
         OR f IS NULL
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=154
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.865.2>
        4, 14, 25, 40, 42, 49, 51, 66, 68, 85, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.865.2>
    })

test:do_test(
    "where7-2.866.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=44
         OR b=55
         OR a=30
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR 1000000<b
         OR a=24
         OR b=1089
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR (g='rqponml' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.866.1>
        5, 19, 24, 30, 33, 44, 45, 71, 75, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.866.1>
    })

test:do_test(
    "where7-2.866.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=44
         OR b=55
         OR a=30
         OR (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR 1000000<b
         OR a=24
         OR b=1089
         OR (d>=75.0 AND d<76.0 AND d IS NOT NULL)
         OR (g='rqponml' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.866.2>
        5, 19, 24, 30, 33, 44, 45, 71, 75, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.866.2>
    })

test:do_test(
    "where7-2.867.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR b=36
         OR ((a BETWEEN 33 AND 35) AND a!=34)
  ]])
    end, {
        -- <where7-2.867.1>
        16, 33, 35, 80, 82, "scan", 0, "sort", 0
        -- </where7-2.867.1>
    })

test:do_test(
    "where7-2.867.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=16.0 AND d<17.0 AND d IS NOT NULL)
         OR ((a BETWEEN 80 AND 82) AND a!=81)
         OR b=36
         OR ((a BETWEEN 33 AND 35) AND a!=34)
  ]])
    end, {
        -- <where7-2.867.2>
        16, 33, 35, 80, 82, "scan", 0, "sort", 0
        -- </where7-2.867.2>
    })

test:do_test(
    "where7-2.868.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR (d>=35.0 AND d<36.0 AND d IS NOT NULL)
         OR c=26026
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR a=56
         OR b=506
         OR b=781
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.868.1>
        28, 35, 37, 39, 46, 56, 71, 76, 77, 78, 97, "scan", 0, "sort", 0
        -- </where7-2.868.1>
    })

test:do_test(
    "where7-2.868.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR (d>=35.0 AND d<36.0 AND d IS NOT NULL)
         OR c=26026
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR a=56
         OR b=506
         OR b=781
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.868.2>
        28, 35, 37, 39, 46, 56, 71, 76, 77, 78, 97, "scan", 0, "sort", 0
        -- </where7-2.868.2>
    })

test:do_test(
    "where7-2.869.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='edcbazy' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR f='xyzabcdef'
         OR b=517
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.869.1>
        23, 25, 27, 39, 47, 49, 68, 75, 76, 89, 91, 98, "scan", 0, "sort", 0
        -- </where7-2.869.1>
    })

test:do_test(
    "where7-2.869.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='edcbazy' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 25 AND 27) AND a!=26)
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR ((a BETWEEN 89 AND 91) AND a!=90)
         OR f='xyzabcdef'
         OR b=517
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.869.2>
        23, 25, 27, 39, 47, 49, 68, 75, 76, 89, 91, 98, "scan", 0, "sort", 0
        -- </where7-2.869.2>
    })

test:do_test(
    "where7-2.870.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=54
         OR a=59
  ]])
    end, {
        -- <where7-2.870.1>
        54, 59, "scan", 0, "sort", 0
        -- </where7-2.870.1>
    })

test:do_test(
    "where7-2.870.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=54
         OR a=59
  ]])
    end, {
        -- <where7-2.870.2>
        54, 59, "scan", 0, "sort", 0
        -- </where7-2.870.2>
    })

test:do_test(
    "where7-2.871.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='tsrqpon' AND f GLOB 'yzabc*')
         OR b=762
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR a=25
         OR ((a BETWEEN 65 AND 67) AND a!=66)
  ]])
    end, {
        -- <where7-2.871.1>
        24, 25, 48, 65, 67, "scan", 0, "sort", 0
        -- </where7-2.871.1>
    })

test:do_test(
    "where7-2.871.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='tsrqpon' AND f GLOB 'yzabc*')
         OR b=762
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR a=25
         OR ((a BETWEEN 65 AND 67) AND a!=66)
  ]])
    end, {
        -- <where7-2.871.2>
        24, 25, 48, 65, 67, "scan", 0, "sort", 0
        -- </where7-2.871.2>
    })

test:do_test(
    "where7-2.872.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=59.0 AND d<60.0 AND d IS NOT NULL)
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR b=839
         OR f='defghijkl'
         OR (d>=95.0 AND d<96.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR b=498
  ]])
    end, {
        -- <where7-2.872.1>
        3, 14, 16, 29, 52, 55, 59, 60, 81, 85, 95, "scan", 0, "sort", 0
        -- </where7-2.872.1>
    })

test:do_test(
    "where7-2.872.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=59.0 AND d<60.0 AND d IS NOT NULL)
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR b=839
         OR f='defghijkl'
         OR (d>=95.0 AND d<96.0 AND d IS NOT NULL)
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR b=498
  ]])
    end, {
        -- <where7-2.872.2>
        3, 14, 16, 29, 52, 55, 59, 60, 81, 85, 95, "scan", 0, "sort", 0
        -- </where7-2.872.2>
    })

test:do_test(
    "where7-2.873.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=256
         OR c=19019
         OR a=54
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR b=498
         OR b=77
  ]])
    end, {
        -- <where7-2.873.1>
        7, 46, 54, 55, 56, 57, "scan", 0, "sort", 0
        -- </where7-2.873.1>
    })

test:do_test(
    "where7-2.873.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=256
         OR c=19019
         OR a=54
         OR (d>=46.0 AND d<47.0 AND d IS NOT NULL)
         OR b=498
         OR b=77
  ]])
    end, {
        -- <where7-2.873.2>
        7, 46, 54, 55, 56, 57, "scan", 0, "sort", 0
        -- </where7-2.873.2>
    })

test:do_test(
    "where7-2.874.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=256
         OR b=586
         OR a=74
         OR b=113
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR b=495
  ]])
    end, {
        -- <where7-2.874.1>
        45, 61, 74, 99, "scan", 0, "sort", 0
        -- </where7-2.874.1>
    })

test:do_test(
    "where7-2.874.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='mlkjihg' AND f GLOB 'jklmn*')
         OR b=256
         OR b=586
         OR a=74
         OR b=113
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR b=495
  ]])
    end, {
        -- <where7-2.874.2>
        45, 61, 74, 99, "scan", 0, "sort", 0
        -- </where7-2.874.2>
    })

test:do_test(
    "where7-2.875.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=12
         OR a=50
         OR (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR ((a BETWEEN 66 AND 68) AND a!=67)
  ]])
    end, {
        -- <where7-2.875.1>
        12, 33, 50, 66, 68, "scan", 0, "sort", 0
        -- </where7-2.875.1>
    })

test:do_test(
    "where7-2.875.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=12
         OR a=50
         OR (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR ((a BETWEEN 66 AND 68) AND a!=67)
  ]])
    end, {
        -- <where7-2.875.2>
        12, 33, 50, 66, 68, "scan", 0, "sort", 0
        -- </where7-2.875.2>
    })

test:do_test(
    "where7-2.876.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=308
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR a=83
         OR c=23023
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR a=58
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR c=4004
  ]])
    end, {
        -- <where7-2.876.1>
        10, 11, 12, 17, 19, 28, 30, 53, 57, 58, 65, 67, 68, 69, 73, 83, "scan", 0, "sort", 0
        -- </where7-2.876.1>
    })

test:do_test(
    "where7-2.876.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=308
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR a=83
         OR c=23023
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR (g='lkjihgf' AND f GLOB 'nopqr*')
         OR a=58
         OR ((a BETWEEN 17 AND 19) AND a!=18)
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR c=4004
  ]])
    end, {
        -- <where7-2.876.2>
        10, 11, 12, 17, 19, 28, 30, 53, 57, 58, 65, 67, 68, 69, 73, 83, "scan", 0, "sort", 0
        -- </where7-2.876.2>
    })

test:do_test(
    "where7-2.877.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=476
         OR a=26
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR b=762
         OR b=157
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
  ]])
    end, {
        -- <where7-2.877.1>
        17, 26, 54, 87, "scan", 0, "sort", 0
        -- </where7-2.877.1>
    })

test:do_test(
    "where7-2.877.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=476
         OR a=26
         OR (d>=87.0 AND d<88.0 AND d IS NOT NULL)
         OR b=762
         OR b=157
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
  ]])
    end, {
        -- <where7-2.877.2>
        17, 26, 54, 87, "scan", 0, "sort", 0
        -- </where7-2.877.2>
    })

test:do_test(
    "where7-2.878.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR a=1
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR b=278
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR f='qrstuvwxy'
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR ((a BETWEEN 82 AND 84) AND a!=83)
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.878.1>
        1, 3, 16, 42, 52, 68, 72, 74, 77, 82, 84, 93, 94, 95, 98, "scan", 0, "sort", 0
        -- </where7-2.878.1>
    })

test:do_test(
    "where7-2.878.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR a=1
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR b=278
         OR (g='xwvutsr' AND f GLOB 'defgh*')
         OR f='qrstuvwxy'
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR ((a BETWEEN 82 AND 84) AND a!=83)
         OR (g='edcbazy' AND f GLOB 'uvwxy*')
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.878.2>
        1, 3, 16, 42, 52, 68, 72, 74, 77, 82, 84, 93, 94, 95, 98, "scan", 0, "sort", 0
        -- </where7-2.878.2>
    })

test:do_test(
    "where7-2.879.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=124
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR b=759
         OR (f GLOB '?nopq*' AND f GLOB 'mnop*')
         OR ((a BETWEEN 45 AND 47) AND a!=46)
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.879.1>
        12, 38, 41, 43, 45, 47, 64, 69, 72, 90, 92, 96, "scan", 0, "sort", 0
        -- </where7-2.879.1>
    })

test:do_test(
    "where7-2.879.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=124
         OR (d>=92.0 AND d<93.0 AND d IS NOT NULL)
         OR ((a BETWEEN 41 AND 43) AND a!=42)
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR b=759
         OR (f GLOB '?nopq*' AND f GLOB 'mnop*')
         OR ((a BETWEEN 45 AND 47) AND a!=46)
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
  ]])
    end, {
        -- <where7-2.879.2>
        12, 38, 41, 43, 45, 47, 64, 69, 72, 90, 92, 96, "scan", 0, "sort", 0
        -- </where7-2.879.2>
    })

test:do_test(
    "where7-2.880.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=872
         OR b=267
         OR b=814
         OR b=99
         OR c<=10
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR b=44
         OR f='zabcdefgh'
         OR b=979
         OR (g='rqponml' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.880.1>
        4, 8, 9, 10, 25, 33, 51, 74, 77, 89, "scan", 0, "sort", 0
        -- </where7-2.880.1>
    })

test:do_test(
    "where7-2.880.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=872
         OR b=267
         OR b=814
         OR b=99
         OR c<=10
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR b=44
         OR f='zabcdefgh'
         OR b=979
         OR (g='rqponml' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.880.2>
        4, 8, 9, 10, 25, 33, 51, 74, 77, 89, "scan", 0, "sort", 0
        -- </where7-2.880.2>
    })

test:do_test(
    "where7-2.881.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR f='xyzabcdef'
  ]])
    end, {
        -- <where7-2.881.1>
        23, 26, 49, 75, "scan", 0, "sort", 0
        -- </where7-2.881.1>
    })

test:do_test(
    "where7-2.881.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR f='xyzabcdef'
  ]])
    end, {
        -- <where7-2.881.2>
        23, 26, 49, 75, "scan", 0, "sort", 0
        -- </where7-2.881.2>
    })

test:do_test(
    "where7-2.882.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=487
         OR b=355
         OR c=9009
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=113
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR a=90
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'jklmn*')
         OR f='nopqrstuv'
  ]])
    end, {
        -- <where7-2.882.1>
        8, 13, 24, 25, 26, 27, 32, 34, 39, 65, 66, 87, 90, 91, "scan", 0, "sort", 0
        -- </where7-2.882.1>
    })

test:do_test(
    "where7-2.882.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=487
         OR b=355
         OR c=9009
         OR (d>=8.0 AND d<9.0 AND d IS NOT NULL)
         OR ((a BETWEEN 32 AND 34) AND a!=33)
         OR b=113
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR a=90
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR (g='hgfedcb' AND f GLOB 'jklmn*')
         OR f='nopqrstuv'
  ]])
    end, {
        -- <where7-2.882.2>
        8, 13, 24, 25, 26, 27, 32, 34, 39, 65, 66, 87, 90, 91, "scan", 0, "sort", 0
        -- </where7-2.882.2>
    })

test:do_test(
    "where7-2.883.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR b=275
  ]])
    end, {
        -- <where7-2.883.1>
        25, 34, "scan", 0, "sort", 0
        -- </where7-2.883.1>
    })

test:do_test(
    "where7-2.883.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR b=275
  ]])
    end, {
        -- <where7-2.883.2>
        25, 34, "scan", 0, "sort", 0
        -- </where7-2.883.2>
    })

test:do_test(
    "where7-2.884.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=355
         OR a=44
         OR b=374
         OR c=25025
         OR b=198
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR d<0.0
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR c=9009
  ]])
    end, {
        -- <where7-2.884.1>
        4, 6, 18, 25, 26, 27, 34, 41, 44, 69, 71, 73, 74, 75, "scan", 0, "sort", 0
        -- </where7-2.884.1>
    })

test:do_test(
    "where7-2.884.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=355
         OR a=44
         OR b=374
         OR c=25025
         OR b=198
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR d<0.0
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR c=9009
  ]])
    end, {
        -- <where7-2.884.2>
        4, 6, 18, 25, 26, 27, 34, 41, 44, 69, 71, 73, 74, 75, "scan", 0, "sort", 0
        -- </where7-2.884.2>
    })

test:do_test(
    "where7-2.885.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR b=814
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.885.1>
        19, 45, 54, 71, 74, 97, "scan", 0, "sort", 0
        -- </where7-2.885.1>
    })

test:do_test(
    "where7-2.885.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?uvwx*' AND f GLOB 'tuvw*')
         OR b=814
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.885.2>
        19, 45, 54, 71, 74, 97, "scan", 0, "sort", 0
        -- </where7-2.885.2>
    })

test:do_test(
    "where7-2.886.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='lkjihgf' AND f GLOB 'mnopq*')
         OR b=333
         OR b=275
  ]])
    end, {
        -- <where7-2.886.1>
        25, 64, "scan", 0, "sort", 0
        -- </where7-2.886.1>
    })

test:do_test(
    "where7-2.886.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='lkjihgf' AND f GLOB 'mnopq*')
         OR b=333
         OR b=275
  ]])
    end, {
        -- <where7-2.886.2>
        25, 64, "scan", 0, "sort", 0
        -- </where7-2.886.2>
    })

test:do_test(
    "where7-2.887.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='ihgfedc' AND f GLOB 'efghi*')
         OR ((a BETWEEN 33 AND 35) AND a!=34)
  ]])
    end, {
        -- <where7-2.887.1>
        33, 35, 82, "scan", 0, "sort", 0
        -- </where7-2.887.1>
    })

test:do_test(
    "where7-2.887.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='ihgfedc' AND f GLOB 'efghi*')
         OR ((a BETWEEN 33 AND 35) AND a!=34)
  ]])
    end, {
        -- <where7-2.887.2>
        33, 35, 82, "scan", 0, "sort", 0
        -- </where7-2.887.2>
    })

test:do_test(
    "where7-2.888.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 11 AND 13) AND a!=12)
         OR b=253
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR b=286
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.888.1>
        10, 11, 13, 14, 23, 26, 40, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.888.1>
    })

test:do_test(
    "where7-2.888.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 11 AND 13) AND a!=12)
         OR b=253
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR b=286
         OR (d>=10.0 AND d<11.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.888.2>
        10, 11, 13, 14, 23, 26, 40, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.888.2>
    })

test:do_test(
    "where7-2.889.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 26 AND 28) AND a!=27)
         OR b=421
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR f='ijklmnopq'
         OR b=891
         OR b=1056
  ]])
    end, {
        -- <where7-2.889.1>
        5, 8, 15, 26, 28, 34, 60, 81, 86, 90, 96, "scan", 0, "sort", 0
        -- </where7-2.889.1>
    })

test:do_test(
    "where7-2.889.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 26 AND 28) AND a!=27)
         OR b=421
         OR (g='xwvutsr' AND f GLOB 'fghij*')
         OR f='ijklmnopq'
         OR b=891
         OR b=1056
  ]])
    end, {
        -- <where7-2.889.2>
        5, 8, 15, 26, 28, 34, 60, 81, 86, 90, 96, "scan", 0, "sort", 0
        -- </where7-2.889.2>
    })

test:do_test(
    "where7-2.890.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='fghijklmn'
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR b=671
         OR (g='xwvutsr' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.890.1>
        5, 7, 31, 39, 57, 61, 83, 99, "scan", 0, "sort", 0
        -- </where7-2.890.1>
    })

test:do_test(
    "where7-2.890.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='fghijklmn'
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR (g='edcbazy' AND f GLOB 'vwxyz*')
         OR b=671
         OR (g='xwvutsr' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.890.2>
        5, 7, 31, 39, 57, 61, 83, 99, "scan", 0, "sort", 0
        -- </where7-2.890.2>
    })

test:do_test(
    "where7-2.891.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='lkjihgf' AND f GLOB 'lmnop*')
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.891.1>
        4, 6, 11, 31, 63, 68, "scan", 0, "sort", 0
        -- </where7-2.891.1>
    })

test:do_test(
    "where7-2.891.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='lkjihgf' AND f GLOB 'lmnop*')
         OR (g='srqponm' AND f GLOB 'fghij*')
         OR ((a BETWEEN 4 AND 6) AND a!=5)
         OR (g='kjihgfe' AND f GLOB 'qrstu*')
         OR (d>=11.0 AND d<12.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.891.2>
        4, 6, 11, 31, 63, 68, "scan", 0, "sort", 0
        -- </where7-2.891.2>
    })

test:do_test(
    "where7-2.892.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=11011
         OR a=20
         OR b=432
         OR b=410
         OR a=86
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR b=638
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR b=190
  ]])
    end, {
        -- <where7-2.892.1>
        20, 31, 32, 33, 58, 60, 86, 89, "scan", 0, "sort", 0
        -- </where7-2.892.1>
    })

test:do_test(
    "where7-2.892.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=11011
         OR a=20
         OR b=432
         OR b=410
         OR a=86
         OR (d>=89.0 AND d<90.0 AND d IS NOT NULL)
         OR b=638
         OR ((a BETWEEN 58 AND 60) AND a!=59)
         OR b=190
  ]])
    end, {
        -- <where7-2.892.2>
        20, 31, 32, 33, 58, 60, 86, 89, "scan", 0, "sort", 0
        -- </where7-2.892.2>
    })

test:do_test(
    "where7-2.893.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=88
         OR ((a BETWEEN 42 AND 44) AND a!=43)
         OR a=76
         OR b=69
         OR b=847
         OR b=275
  ]])
    end, {
        -- <where7-2.893.1>
        8, 25, 42, 44, 76, 77, "scan", 0, "sort", 0
        -- </where7-2.893.1>
    })

test:do_test(
    "where7-2.893.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=88
         OR ((a BETWEEN 42 AND 44) AND a!=43)
         OR a=76
         OR b=69
         OR b=847
         OR b=275
  ]])
    end, {
        -- <where7-2.893.2>
        8, 25, 42, 44, 76, 77, "scan", 0, "sort", 0
        -- </where7-2.893.2>
    })

test:do_test(
    "where7-2.894.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=781
         OR b=77
         OR b=58
         OR ((a BETWEEN 67 AND 69) AND a!=68)
  ]])
    end, {
        -- <where7-2.894.1>
        7, 67, 69, 71, "scan", 0, "sort", 0
        -- </where7-2.894.1>
    })

test:do_test(
    "where7-2.894.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=781
         OR b=77
         OR b=58
         OR ((a BETWEEN 67 AND 69) AND a!=68)
  ]])
    end, {
        -- <where7-2.894.2>
        7, 67, 69, 71, "scan", 0, "sort", 0
        -- </where7-2.894.2>
    })

test:do_test(
    "where7-2.895.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 67 AND 69) AND a!=68)
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR a=46
         OR b=187
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (g='onmlkji' AND f GLOB 'yzabc*')
  ]])
    end, {
        -- <where7-2.895.1>
        17, 20, 46, 50, 67, 69, 71, "scan", 0, "sort", 0
        -- </where7-2.895.1>
    })

test:do_test(
    "where7-2.895.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 67 AND 69) AND a!=68)
         OR (d>=69.0 AND d<70.0 AND d IS NOT NULL)
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR a=46
         OR b=187
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR (g='onmlkji' AND f GLOB 'yzabc*')
  ]])
    end, {
        -- <where7-2.895.2>
        17, 20, 46, 50, 67, 69, 71, "scan", 0, "sort", 0
        -- </where7-2.895.2>
    })

test:do_test(
    "where7-2.896.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR a=99
         OR c=3003
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR b=300
         OR b=718
         OR c>=34035
         OR b=264
  ]])
    end, {
        -- <where7-2.896.1>
        7, 8, 9, 24, 57, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.896.1>
    })

test:do_test(
    "where7-2.896.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR a=99
         OR c=3003
         OR (d>=57.0 AND d<58.0 AND d IS NOT NULL)
         OR b=300
         OR b=718
         OR c>=34035
         OR b=264
  ]])
    end, {
        -- <where7-2.896.2>
        7, 8, 9, 24, 57, 97, 99, "scan", 0, "sort", 0
        -- </where7-2.896.2>
    })

test:do_test(
    "where7-2.897.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=872
         OR b=209
         OR ((a BETWEEN 65 AND 67) AND a!=66)
         OR b=355
         OR b=729
         OR ((a BETWEEN 81 AND 83) AND a!=82)
         OR a=58
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR b=608
  ]])
    end, {
        -- <where7-2.897.1>
        14, 19, 40, 58, 65, 66, 67, 81, 83, 92, "scan", 0, "sort", 0
        -- </where7-2.897.1>
    })

test:do_test(
    "where7-2.897.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=872
         OR b=209
         OR ((a BETWEEN 65 AND 67) AND a!=66)
         OR b=355
         OR b=729
         OR ((a BETWEEN 81 AND 83) AND a!=82)
         OR a=58
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR b=608
  ]])
    end, {
        -- <where7-2.897.2>
        14, 19, 40, 58, 65, 66, 67, 81, 83, 92, "scan", 0, "sort", 0
        -- </where7-2.897.2>
    })

test:do_test(
    "where7-2.898.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=29029
         OR f='efghijklm'
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR a=26
         OR (f GLOB '?efgh*' AND f GLOB 'defg*')
  ]])
    end, {
        -- <where7-2.898.1>
        3, 4, 26, 29, 30, 48, 55, 56, 81, 82, 85, 86, 87, "scan", 0, "sort", 0
        -- </where7-2.898.1>
    })

test:do_test(
    "where7-2.898.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=29029
         OR f='efghijklm'
         OR (d>=48.0 AND d<49.0 AND d IS NOT NULL)
         OR a=26
         OR (f GLOB '?efgh*' AND f GLOB 'defg*')
  ]])
    end, {
        -- <where7-2.898.2>
        3, 4, 26, 29, 30, 48, 55, 56, 81, 82, 85, 86, 87, "scan", 0, "sort", 0
        -- </where7-2.898.2>
    })

test:do_test(
    "where7-2.899.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=59
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR a=7
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR b=762
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
  ]])
    end, {
        -- <where7-2.899.1>
        7, 12, 14, 26, 40, 59, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.899.1>
    })

test:do_test(
    "where7-2.899.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=59
         OR (g='wvutsrq' AND f GLOB 'mnopq*')
         OR a=7
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR b=762
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
  ]])
    end, {
        -- <where7-2.899.2>
        7, 12, 14, 26, 40, 59, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.899.2>
    })

test:do_test(
    "where7-2.900.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='gfedcba' AND f GLOB 'nopqr*')
         OR b=539
         OR b=399
  ]])
    end, {
        -- <where7-2.900.1>
        49, 91, "scan", 0, "sort", 0
        -- </where7-2.900.1>
    })

test:do_test(
    "where7-2.900.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='gfedcba' AND f GLOB 'nopqr*')
         OR b=539
         OR b=399
  ]])
    end, {
        -- <where7-2.900.2>
        49, 91, "scan", 0, "sort", 0
        -- </where7-2.900.2>
    })

test:do_test(
    "where7-2.901.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR a=92
  ]])
    end, {
        -- <where7-2.901.1>
        71, 73, 92, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.901.1>
    })

test:do_test(
    "where7-2.901.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 71 AND 73) AND a!=72)
         OR ((a BETWEEN 96 AND 98) AND a!=97)
         OR a=92
  ]])
    end, {
        -- <where7-2.901.2>
        71, 73, 92, 96, 98, "scan", 0, "sort", 0
        -- </where7-2.901.2>
    })

test:do_test(
    "where7-2.902.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR f='lmnopqrst'
         OR (g='nmlkjih' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.902.1>
        9, 11, 35, 36, 37, 57, 61, 63, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.902.1>
    })

test:do_test(
    "where7-2.902.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR (g='rqponml' AND f GLOB 'klmno*')
         OR f='lmnopqrst'
         OR (g='nmlkjih' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.902.2>
        9, 11, 35, 36, 37, 57, 61, 63, 87, 89, "scan", 0, "sort", 0
        -- </where7-2.902.2>
    })

test:do_test(
    "where7-2.903.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 69 AND 71) AND a!=70)
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=652
  ]])
    end, {
        -- <where7-2.903.1>
        69, 71, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.903.1>
    })

test:do_test(
    "where7-2.903.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 69 AND 71) AND a!=70)
         OR ((a BETWEEN 91 AND 93) AND a!=92)
         OR b=652
  ]])
    end, {
        -- <where7-2.903.2>
        69, 71, 91, 93, "scan", 0, "sort", 0
        -- </where7-2.903.2>
    })

test:do_test(
    "where7-2.904.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1067
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR b=520
         OR b=399
         OR b=209
         OR a=68
         OR (g='fedcbaz' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.904.1>
        18, 19, 53, 54, 55, 68, 73, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.904.1>
    })

test:do_test(
    "where7-2.904.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1067
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR b=520
         OR b=399
         OR b=209
         OR a=68
         OR (g='fedcbaz' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.904.2>
        18, 19, 53, 54, 55, 68, 73, 94, 97, "scan", 0, "sort", 0
        -- </where7-2.904.2>
    })

test:do_test(
    "where7-2.905.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR a=57
         OR b=55
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR ((a BETWEEN 0 AND 2) AND a!=1)
         OR ((a BETWEEN 21 AND 23) AND a!=22)
  ]])
    end, {
        -- <where7-2.905.1>
        2, 5, 20, 21, 22, 23, 34, 37, 57, 79, "scan", 0, "sort", 0
        -- </where7-2.905.1>
    })

test:do_test(
    "where7-2.905.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR a=57
         OR b=55
         OR (d>=34.0 AND d<35.0 AND d IS NOT NULL)
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR ((a BETWEEN 0 AND 2) AND a!=1)
         OR ((a BETWEEN 21 AND 23) AND a!=22)
  ]])
    end, {
        -- <where7-2.905.2>
        2, 5, 20, 21, 22, 23, 34, 37, 57, 79, "scan", 0, "sort", 0
        -- </where7-2.905.2>
    })

test:do_test(
    "where7-2.906.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 21 AND 23) AND a!=22)
         OR a=2
         OR b=784
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR b=850
  ]])
    end, {
        -- <where7-2.906.1>
        2, 21, 23, 81, "scan", 0, "sort", 0
        -- </where7-2.906.1>
    })

test:do_test(
    "where7-2.906.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 21 AND 23) AND a!=22)
         OR a=2
         OR b=784
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR (g='ihgfedc' AND f GLOB 'defgh*')
         OR b=850
  ]])
    end, {
        -- <where7-2.906.2>
        2, 21, 23, 81, "scan", 0, "sort", 0
        -- </where7-2.906.2>
    })

test:do_test(
    "where7-2.907.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=748
         OR b=209
         OR a=100
  ]])
    end, {
        -- <where7-2.907.1>
        19, 45, 51, 68, 100, "scan", 0, "sort", 0
        -- </where7-2.907.1>
    })

test:do_test(
    "where7-2.907.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=51.0 AND d<52.0 AND d IS NOT NULL)
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=748
         OR b=209
         OR a=100
  ]])
    end, {
        -- <where7-2.907.2>
        19, 45, 51, 68, 100, "scan", 0, "sort", 0
        -- </where7-2.907.2>
    })

test:do_test(
    "where7-2.908.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='jihgfed' AND f GLOB 'zabcd*')
         OR a=18
         OR a=30
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR ((a BETWEEN 84 AND 86) AND a!=85)
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR b=792
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR c=26026
         OR (g='rqponml' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.908.1>
        8, 9, 10, 11, 18, 19, 21, 30, 33, 37, 63, 72, 76, 77, 78, 84, 86, 89, "scan", 0, "sort", 0
        -- </where7-2.908.1>
    })

test:do_test(
    "where7-2.908.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='jihgfed' AND f GLOB 'zabcd*')
         OR a=18
         OR a=30
         OR ((a BETWEEN 9 AND 11) AND a!=10)
         OR ((a BETWEEN 84 AND 86) AND a!=85)
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR b=792
         OR (f GLOB '?mnop*' AND f GLOB 'lmno*')
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR c=26026
         OR (g='rqponml' AND f GLOB 'hijkl*')
  ]])
    end, {
        -- <where7-2.908.2>
        8, 9, 10, 11, 18, 19, 21, 30, 33, 37, 63, 72, 76, 77, 78, 84, 86, 89, "scan", 0, "sort", 0
        -- </where7-2.908.2>
    })

test:do_test(
    "where7-2.909.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='vutsrqp' AND f GLOB 'qrstu*')
         OR b=968
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR a=78
         OR ((a BETWEEN 90 AND 92) AND a!=91)
  ]])
    end, {
        -- <where7-2.909.1>
        16, 22, 48, 63, 65, 72, 74, 78, 88, 90, 92, 100, "scan", 0, "sort", 0
        -- </where7-2.909.1>
    })

test:do_test(
    "where7-2.909.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='vutsrqp' AND f GLOB 'qrstu*')
         OR b=968
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR (d>=72.0 AND d<73.0 AND d IS NOT NULL)
         OR a=78
         OR ((a BETWEEN 90 AND 92) AND a!=91)
  ]])
    end, {
        -- <where7-2.909.2>
        16, 22, 48, 63, 65, 72, 74, 78, 88, 90, 92, 100, "scan", 0, "sort", 0
        -- </where7-2.909.2>
    })

test:do_test(
    "where7-2.910.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=10010
         OR f='pqrstuvwx'
  ]])
    end, {
        -- <where7-2.910.1>
        15, 28, 29, 30, 41, 67, 93, "scan", 0, "sort", 0
        -- </where7-2.910.1>
    })

test:do_test(
    "where7-2.910.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=10010
         OR f='pqrstuvwx'
  ]])
    end, {
        -- <where7-2.910.2>
        15, 28, 29, 30, 41, 67, 93, "scan", 0, "sort", 0
        -- </where7-2.910.2>
    })

test:do_test(
    "where7-2.911.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=86
         OR a=10
         OR b=528
         OR b=253
         OR a=80
         OR a=87
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.911.1>
        10, 23, 37, 48, 80, 86, 87, "scan", 0, "sort", 0
        -- </where7-2.911.1>
    })

test:do_test(
    "where7-2.911.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=86
         OR a=10
         OR b=528
         OR b=253
         OR a=80
         OR a=87
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.911.2>
        10, 23, 37, 48, 80, 86, 87, "scan", 0, "sort", 0
        -- </where7-2.911.2>
    })

test:do_test(
    "where7-2.912.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR b=825
         OR a=100
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR a=60
  ]])
    end, {
        -- <where7-2.912.1>
        42, 60, 75, 77, 100, "scan", 0, "sort", 0
        -- </where7-2.912.1>
    })

test:do_test(
    "where7-2.912.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR b=825
         OR a=100
         OR (d>=77.0 AND d<78.0 AND d IS NOT NULL)
         OR a=60
  ]])
    end, {
        -- <where7-2.912.2>
        42, 60, 75, 77, 100, "scan", 0, "sort", 0
        -- </where7-2.912.2>
    })

test:do_test(
    "where7-2.913.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR b=883
         OR (d>=35.0 AND d<36.0 AND d IS NOT NULL)
         OR (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR a=81
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR f='mnopqrstu'
  ]])
    end, {
        -- <where7-2.913.1>
        3, 4, 12, 30, 35, 38, 45, 56, 64, 78, 81, 82, 90, 94, "scan", 0, "sort", 0
        -- </where7-2.913.1>
    })

test:do_test(
    "where7-2.913.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=94.0 AND d<95.0 AND d IS NOT NULL)
         OR b=883
         OR (d>=35.0 AND d<36.0 AND d IS NOT NULL)
         OR (d>=3.0 AND d<4.0 AND d IS NOT NULL)
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR a=81
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR f='mnopqrstu'
  ]])
    end, {
        -- <where7-2.913.2>
        3, 4, 12, 30, 35, 38, 45, 56, 64, 78, 81, 82, 90, 94, "scan", 0, "sort", 0
        -- </where7-2.913.2>
    })

test:do_test(
    "where7-2.914.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=443
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR b=663
         OR b=905
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR b=883
         OR c=22022
         OR b=638
  ]])
    end, {
        -- <where7-2.914.1>
        14, 16, 58, 64, 65, 66, 96, "scan", 0, "sort", 0
        -- </where7-2.914.1>
    })

test:do_test(
    "where7-2.914.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=443
         OR ((a BETWEEN 14 AND 16) AND a!=15)
         OR b=663
         OR b=905
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR b=883
         OR c=22022
         OR b=638
  ]])
    end, {
        -- <where7-2.914.2>
        14, 16, 58, 64, 65, 66, 96, "scan", 0, "sort", 0
        -- </where7-2.914.2>
    })

test:do_test(
    "where7-2.915.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 84 AND 86) AND a!=85)
         OR b=234
         OR a=53
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR ((a BETWEEN 27 AND 29) AND a!=28)
         OR b=319
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.915.1>
        14, 16, 20, 22, 27, 29, 40, 53, 84, 86, "scan", 0, "sort", 0
        -- </where7-2.915.1>
    })

test:do_test(
    "where7-2.915.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 84 AND 86) AND a!=85)
         OR b=234
         OR a=53
         OR ((a BETWEEN 20 AND 22) AND a!=21)
         OR ((a BETWEEN 27 AND 29) AND a!=28)
         OR b=319
         OR (g='qponmlk' AND f GLOB 'opqrs*')
         OR ((a BETWEEN 14 AND 16) AND a!=15)
  ]])
    end, {
        -- <where7-2.915.2>
        14, 16, 20, 22, 27, 29, 40, 53, 84, 86, "scan", 0, "sort", 0
        -- </where7-2.915.2>
    })

test:do_test(
    "where7-2.916.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=179
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR a=46
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR a=25
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR f='opqrstuvw'
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR b=938
  ]])
    end, {
        -- <where7-2.916.1>
        5, 13, 14, 25, 40, 46, 53, 55, 66, 72, 92, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.916.1>
    })

test:do_test(
    "where7-2.916.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=179
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR a=46
         OR (g='kjihgfe' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 53 AND 55) AND a!=54)
         OR a=25
         OR (d>=5.0 AND d<6.0 AND d IS NOT NULL)
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR f='opqrstuvw'
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR b=938
  ]])
    end, {
        -- <where7-2.916.2>
        5, 13, 14, 25, 40, 46, 53, 55, 66, 72, 92, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.916.2>
    })

test:do_test(
    "where7-2.917.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='nmlkjih' AND f GLOB 'fghij*')
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.917.1>
        15, 57, "scan", 0, "sort", 0
        -- </where7-2.917.1>
    })

test:do_test(
    "where7-2.917.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='nmlkjih' AND f GLOB 'fghij*')
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.917.2>
        15, 57, "scan", 0, "sort", 0
        -- </where7-2.917.2>
    })

test:do_test(
    "where7-2.918.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=748
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR a=32
         OR b=110
         OR b=297
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR f='ghijklmno'
         OR b=473
         OR b=135
  ]])
    end, {
        -- <where7-2.918.1>
        6, 10, 13, 22, 27, 32, 43, 58, 60, 62, 68, 84, "scan", 0, "sort", 0
        -- </where7-2.918.1>
    })

test:do_test(
    "where7-2.918.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=748
         OR (g='utsrqpo' AND f GLOB 'wxyza*')
         OR a=32
         OR b=110
         OR b=297
         OR (d>=13.0 AND d<14.0 AND d IS NOT NULL)
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR f='ghijklmno'
         OR b=473
         OR b=135
  ]])
    end, {
        -- <where7-2.918.2>
        6, 10, 13, 22, 27, 32, 43, 58, 60, 62, 68, 84, "scan", 0, "sort", 0
        -- </where7-2.918.2>
    })

test:do_test(
    "where7-2.919.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR b=905
         OR a=97
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR c=27027
         OR f='bcdefghij'
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
  ]])
    end, {
        -- <where7-2.919.1>
        1, 4, 25, 27, 30, 33, 38, 40, 53, 54, 56, 79, 80, 81, 82, 85, 97, "scan", 0, "sort", 0
        -- </where7-2.919.1>
    })

test:do_test(
    "where7-2.919.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=33.0 AND d<34.0 AND d IS NOT NULL)
         OR b=905
         OR a=97
         OR (g='hgfedcb' AND f GLOB 'hijkl*')
         OR c=27027
         OR f='bcdefghij'
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR (d>=25.0 AND d<26.0 AND d IS NOT NULL)
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
  ]])
    end, {
        -- <where7-2.919.2>
        1, 4, 25, 27, 30, 33, 38, 40, 53, 54, 56, 79, 80, 81, 82, 85, 97, "scan", 0, "sort", 0
        -- </where7-2.919.2>
    })

test:do_test(
    "where7-2.920.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 64 AND 66) AND a!=65)
         OR ((a BETWEEN 90 AND 92) AND a!=91)
  ]])
    end, {
        -- <where7-2.920.1>
        64, 66, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.920.1>
    })

test:do_test(
    "where7-2.920.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 64 AND 66) AND a!=65)
         OR ((a BETWEEN 90 AND 92) AND a!=91)
  ]])
    end, {
        -- <where7-2.920.2>
        64, 66, 90, 92, "scan", 0, "sort", 0
        -- </where7-2.920.2>
    })

test:do_test(
    "where7-2.921.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=594
         OR b=80
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR b=421
         OR b=418
         OR b=828
         OR a=88
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.921.1>
        23, 27, 38, 54, 60, 88, "scan", 0, "sort", 0
        -- </where7-2.921.1>
    })

test:do_test(
    "where7-2.921.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=594
         OR b=80
         OR (g='tsrqpon' AND f GLOB 'bcdef*')
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR b=421
         OR b=418
         OR b=828
         OR a=88
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.921.2>
        23, 27, 38, 54, 60, 88, "scan", 0, "sort", 0
        -- </where7-2.921.2>
    })

test:do_test(
    "where7-2.922.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'xyzab*')
         OR b=366
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR c=16016
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR c=9009
  ]])
    end, {
        -- <where7-2.922.1>
        17, 25, 26, 27, 28, 46, 47, 48, 75, 100, "scan", 0, "sort", 0
        -- </where7-2.922.1>
    })

test:do_test(
    "where7-2.922.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'xyzab*')
         OR b=366
         OR (d>=28.0 AND d<29.0 AND d IS NOT NULL)
         OR c=16016
         OR (g='edcbazy' AND f GLOB 'wxyza*')
         OR c=9009
  ]])
    end, {
        -- <where7-2.922.2>
        17, 25, 26, 27, 28, 46, 47, 48, 75, 100, "scan", 0, "sort", 0
        -- </where7-2.922.2>
    })

test:do_test(
    "where7-2.923.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=33
         OR f='qrstuvwxy'
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR b=858
  ]])
    end, {
        -- <where7-2.923.1>
        3, 16, 20, 42, 68, 78, 94, "scan", 0, "sort", 0
        -- </where7-2.923.1>
    })

test:do_test(
    "where7-2.923.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=33
         OR f='qrstuvwxy'
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR b=858
  ]])
    end, {
        -- <where7-2.923.2>
        3, 16, 20, 42, 68, 78, 94, "scan", 0, "sort", 0
        -- </where7-2.923.2>
    })

test:do_test(
    "where7-2.924.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=861
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR b=682
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR b=286
  ]])
    end, {
        -- <where7-2.924.1>
        22, 26, 29, 48, 62, 74, 93, 95, 100, "scan", 0, "sort", 0
        -- </where7-2.924.1>
    })

test:do_test(
    "where7-2.924.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=861
         OR (f GLOB '?xyza*' AND f GLOB 'wxyz*')
         OR (d>=29.0 AND d<30.0 AND d IS NOT NULL)
         OR b=682
         OR ((a BETWEEN 93 AND 95) AND a!=94)
         OR b=286
  ]])
    end, {
        -- <where7-2.924.2>
        22, 26, 29, 48, 62, 74, 93, 95, 100, "scan", 0, "sort", 0
        -- </where7-2.924.2>
    })

test:do_test(
    "where7-2.925.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=740
         OR ((a BETWEEN 27 AND 29) AND a!=28)
         OR a=88
  ]])
    end, {
        -- <where7-2.925.1>
        27, 29, 88, "scan", 0, "sort", 0
        -- </where7-2.925.1>
    })

test:do_test(
    "where7-2.925.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=740
         OR ((a BETWEEN 27 AND 29) AND a!=28)
         OR a=88
  ]])
    end, {
        -- <where7-2.925.2>
        27, 29, 88, "scan", 0, "sort", 0
        -- </where7-2.925.2>
    })

test:do_test(
    "where7-2.926.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='abcdefghi'
         OR c=9009
         OR b=663
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR b=91
  ]])
    end, {
        -- <where7-2.926.1>
        10, 25, 26, 27, 52, 78, "scan", 0, "sort", 0
        -- </where7-2.926.1>
    })

test:do_test(
    "where7-2.926.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='abcdefghi'
         OR c=9009
         OR b=663
         OR (g='wvutsrq' AND f GLOB 'klmno*')
         OR b=91
  ]])
    end, {
        -- <where7-2.926.2>
        10, 25, 26, 27, 52, 78, "scan", 0, "sort", 0
        -- </where7-2.926.2>
    })

test:do_test(
    "where7-2.927.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='kjihgfe' AND f GLOB 'qrstu*')
         OR ((a BETWEEN 29 AND 31) AND a!=30)
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR b=1015
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR b=916
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR b=69
         OR (g='hgfedcb' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.927.1>
        13, 29, 31, 39, 42, 65, 68, 83, 91, "scan", 0, "sort", 0
        -- </where7-2.927.1>
    })

test:do_test(
    "where7-2.927.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='kjihgfe' AND f GLOB 'qrstu*')
         OR ((a BETWEEN 29 AND 31) AND a!=30)
         OR (f GLOB '?opqr*' AND f GLOB 'nopq*')
         OR b=1015
         OR (g='qponmlk' AND f GLOB 'qrstu*')
         OR b=916
         OR (d>=31.0 AND d<32.0 AND d IS NOT NULL)
         OR b=69
         OR (g='hgfedcb' AND f GLOB 'fghij*')
  ]])
    end, {
        -- <where7-2.927.2>
        13, 29, 31, 39, 42, 65, 68, 83, 91, "scan", 0, "sort", 0
        -- </where7-2.927.2>
    })

test:do_test(
    "where7-2.928.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=88
         OR a=1
         OR f='uvwxyzabc'
         OR b=498
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR a=63
         OR f='mnopqrstu'
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR b=495
         OR a=35
         OR a=22
  ]])
    end, {
        -- <where7-2.928.1>
        1, 12, 20, 22, 35, 38, 45, 46, 60, 63, 64, 72, 88, 90, 98, "scan", 0, "sort", 0
        -- </where7-2.928.1>
    })

test:do_test(
    "where7-2.928.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=88
         OR a=1
         OR f='uvwxyzabc'
         OR b=498
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR a=63
         OR f='mnopqrstu'
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR b=495
         OR a=35
         OR a=22
  ]])
    end, {
        -- <where7-2.928.2>
        1, 12, 20, 22, 35, 38, 45, 46, 60, 63, 64, 72, 88, 90, 98, "scan", 0, "sort", 0
        -- </where7-2.928.2>
    })

test:do_test(
    "where7-2.929.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=869
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR b=289
         OR a=62
         OR ((a BETWEEN 9 AND 11) AND a!=10)
  ]])
    end, {
        -- <where7-2.929.1>
        9, 11, 35, 62, 79, "scan", 0, "sort", 0
        -- </where7-2.929.1>
    })

test:do_test(
    "where7-2.929.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=869
         OR (g='rqponml' AND f GLOB 'jklmn*')
         OR b=289
         OR a=62
         OR ((a BETWEEN 9 AND 11) AND a!=10)
  ]])
    end, {
        -- <where7-2.929.2>
        9, 11, 35, 62, 79, "scan", 0, "sort", 0
        -- </where7-2.929.2>
    })

test:do_test(
    "where7-2.930.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 22 AND 24) AND a!=23)
         OR b=542
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR a=7
         OR f='klmnopqrs'
  ]])
    end, {
        -- <where7-2.930.1>
        7, 10, 19, 21, 22, 24, 36, 62, 88, "scan", 0, "sort", 0
        -- </where7-2.930.1>
    })

test:do_test(
    "where7-2.930.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 22 AND 24) AND a!=23)
         OR b=542
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR a=7
         OR f='klmnopqrs'
  ]])
    end, {
        -- <where7-2.930.2>
        7, 10, 19, 21, 22, 24, 36, 62, 88, "scan", 0, "sort", 0
        -- </where7-2.930.2>
    })

test:do_test(
    "where7-2.931.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 57 AND 59) AND a!=58)
         OR b=1078
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR b=429
  ]])
    end, {
        -- <where7-2.931.1>
        20, 21, 23, 39, 57, 59, 60, 98, "scan", 0, "sort", 0
        -- </where7-2.931.1>
    })

test:do_test(
    "where7-2.931.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 57 AND 59) AND a!=58)
         OR b=1078
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR (g='mlkjihg' AND f GLOB 'ijklm*')
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR b=429
  ]])
    end, {
        -- <where7-2.931.2>
        20, 21, 23, 39, 57, 59, 60, 98, "scan", 0, "sort", 0
        -- </where7-2.931.2>
    })

test:do_test(
    "where7-2.932.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR b=264
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR b=1048
         OR a=15
  ]])
    end, {
        -- <where7-2.932.1>
        15, 24, 82, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.932.1>
    })

test:do_test(
    "where7-2.932.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR b=264
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR b=1048
         OR a=15
  ]])
    end, {
        -- <where7-2.932.2>
        15, 24, 82, 85, 87, "scan", 0, "sort", 0
        -- </where7-2.932.2>
    })

test:do_test(
    "where7-2.933.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=93
         OR f='ijklmnopq'
         OR f='mnopqrstu'
         OR ((a BETWEEN 67 AND 69) AND a!=68)
  ]])
    end, {
        -- <where7-2.933.1>
        8, 12, 34, 38, 60, 64, 67, 69, 86, 90, 93, "scan", 0, "sort", 0
        -- </where7-2.933.1>
    })

test:do_test(
    "where7-2.933.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=93
         OR f='ijklmnopq'
         OR f='mnopqrstu'
         OR ((a BETWEEN 67 AND 69) AND a!=68)
  ]])
    end, {
        -- <where7-2.933.2>
        8, 12, 34, 38, 60, 64, 67, 69, 86, 90, 93, "scan", 0, "sort", 0
        -- </where7-2.933.2>
    })

test:do_test(
    "where7-2.934.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR b=858
         OR a=58
         OR (g='onmlkji' AND f GLOB 'xyzab*')
         OR c=21021
         OR ((a BETWEEN 45 AND 47) AND a!=46)
         OR b=616
         OR b=784
         OR b=55
  ]])
    end, {
        -- <where7-2.934.1>
        5, 45, 47, 49, 56, 58, 61, 62, 63, 78, "scan", 0, "sort", 0
        -- </where7-2.934.1>
    })

test:do_test(
    "where7-2.934.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR b=858
         OR a=58
         OR (g='onmlkji' AND f GLOB 'xyzab*')
         OR c=21021
         OR ((a BETWEEN 45 AND 47) AND a!=46)
         OR b=616
         OR b=784
         OR b=55
  ]])
    end, {
        -- <where7-2.934.2>
        5, 45, 47, 49, 56, 58, 61, 62, 63, 78, "scan", 0, "sort", 0
        -- </where7-2.934.2>
    })

test:do_test(
    "where7-2.935.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=682
         OR b=99
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR b=531
  ]])
    end, {
        -- <where7-2.935.1>
        2, 9, 28, 54, 62, 80, "scan", 0, "sort", 0
        -- </where7-2.935.1>
    })

test:do_test(
    "where7-2.935.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=682
         OR b=99
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR b=531
  ]])
    end, {
        -- <where7-2.935.2>
        2, 9, 28, 54, 62, 80, "scan", 0, "sort", 0
        -- </where7-2.935.2>
    })

test:do_test(
    "where7-2.936.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 56 AND 58) AND a!=57)
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR b=726
         OR a=79
         OR a=47
         OR b=212
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR c=8008
  ]])
    end, {
        -- <where7-2.936.1>
        8, 22, 23, 24, 26, 34, 47, 52, 56, 58, 60, 66, 70, 78, 79, 86, "scan", 0, "sort", 0
        -- </where7-2.936.1>
    })

test:do_test(
    "where7-2.936.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 56 AND 58) AND a!=57)
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR (f GLOB '?jklm*' AND f GLOB 'ijkl*')
         OR b=726
         OR a=79
         OR a=47
         OR b=212
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
         OR c=8008
  ]])
    end, {
        -- <where7-2.936.2>
        8, 22, 23, 24, 26, 34, 47, 52, 56, 58, 60, 66, 70, 78, 79, 86, "scan", 0, "sort", 0
        -- </where7-2.936.2>
    })

test:do_test(
    "where7-2.937.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='uvwxyzabc'
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR a=5
         OR b=33
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
         OR a=59
         OR b=44
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
         OR (d>=59.0 AND d<60.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.937.1>
        3, 4, 5, 14, 20, 23, 46, 49, 59, 60, 62, 72, 75, 98, "scan", 0, "sort", 0
        -- </where7-2.937.1>
    })

test:do_test(
    "where7-2.937.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='uvwxyzabc'
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR a=5
         OR b=33
         OR (f GLOB '?yzab*' AND f GLOB 'xyza*')
         OR a=59
         OR b=44
         OR (d>=14.0 AND d<15.0 AND d IS NOT NULL)
         OR (d>=59.0 AND d<60.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.937.2>
        3, 4, 5, 14, 20, 23, 46, 49, 59, 60, 62, 72, 75, 98, "scan", 0, "sort", 0
        -- </where7-2.937.2>
    })

test:do_test(
    "where7-2.938.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=564
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR b=451
         OR b=330
         OR a=47
         OR ((a BETWEEN 17 AND 19) AND a!=18)
  ]])
    end, {
        -- <where7-2.938.1>
        17, 19, 30, 41, 47, 93, "scan", 0, "sort", 0
        -- </where7-2.938.1>
    })

test:do_test(
    "where7-2.938.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=564
         OR (d>=93.0 AND d<94.0 AND d IS NOT NULL)
         OR b=451
         OR b=330
         OR a=47
         OR ((a BETWEEN 17 AND 19) AND a!=18)
  ]])
    end, {
        -- <where7-2.938.2>
        17, 19, 30, 41, 47, 93, "scan", 0, "sort", 0
        -- </where7-2.938.2>
    })

test:do_test(
    "where7-2.939.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=40
         OR b=333
  ]])
    end, {
        -- <where7-2.939.1>
        40, "scan", 0, "sort", 0
        -- </where7-2.939.1>
    })

test:do_test(
    "where7-2.939.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=40
         OR b=333
  ]])
    end, {
        -- <where7-2.939.2>
        40, "scan", 0, "sort", 0
        -- </where7-2.939.2>
    })

test:do_test(
    "where7-2.940.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=924
         OR ((a BETWEEN 6 AND 8) AND a!=7)
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR a=100
         OR c=15015
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.940.1>
        2, 6, 8, 36, 40, 43, 44, 45, 82, 84, 100, "scan", 0, "sort", 0
        -- </where7-2.940.1>
    })

test:do_test(
    "where7-2.940.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=924
         OR ((a BETWEEN 6 AND 8) AND a!=7)
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR a=100
         OR c=15015
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
         OR (d>=2.0 AND d<3.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.940.2>
        2, 6, 8, 36, 40, 43, 44, 45, 82, 84, 100, "scan", 0, "sort", 0
        -- </where7-2.940.2>
    })

test:do_test(
    "where7-2.941.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.941.1>
        36, 44, 63, "scan", 0, "sort", 0
        -- </where7-2.941.1>
    })

test:do_test(
    "where7-2.941.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=63.0 AND d<64.0 AND d IS NOT NULL)
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.941.2>
        36, 44, 63, "scan", 0, "sort", 0
        -- </where7-2.941.2>
    })

test:do_test(
    "where7-2.942.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=58
         OR ((a BETWEEN 7 AND 9) AND a!=8)
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=31
         OR f='tuvwxyzab'
         OR b=341
         OR b=47
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR a=49
         OR b=223
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.942.1>
        7, 9, 16, 19, 31, 42, 45, 49, 63, 65, 68, 71, 94, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.942.1>
    })

test:do_test(
    "where7-2.942.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=58
         OR ((a BETWEEN 7 AND 9) AND a!=8)
         OR ((a BETWEEN 63 AND 65) AND a!=64)
         OR a=31
         OR f='tuvwxyzab'
         OR b=341
         OR b=47
         OR ((a BETWEEN 95 AND 97) AND a!=96)
         OR a=49
         OR b=223
         OR f='qrstuvwxy'
  ]])
    end, {
        -- <where7-2.942.2>
        7, 9, 16, 19, 31, 42, 45, 49, 63, 65, 68, 71, 94, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.942.2>
    })

test:do_test(
    "where7-2.943.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=96
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR a=85
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR c=11011
         OR b=641
         OR f='vwxyzabcd'
         OR b=286
  ]])
    end, {
        -- <where7-2.943.1>
        4, 10, 12, 21, 23, 26, 30, 31, 32, 33, 37, 39, 47, 56, 73, 82, 85, 96, 99, "scan", 0, "sort", 0
        -- </where7-2.943.1>
    })

test:do_test(
    "where7-2.943.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=96
         OR (d>=23.0 AND d<24.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'vwxyz*')
         OR (f GLOB '?fghi*' AND f GLOB 'efgh*')
         OR ((a BETWEEN 37 AND 39) AND a!=38)
         OR a=85
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR c=11011
         OR b=641
         OR f='vwxyzabcd'
         OR b=286
  ]])
    end, {
        -- <where7-2.943.2>
        4, 10, 12, 21, 23, 26, 30, 31, 32, 33, 37, 39, 47, 56, 73, 82, 85, 96, 99, "scan", 0, "sort", 0
        -- </where7-2.943.2>
    })

test:do_test(
    "where7-2.944.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 3 AND 5) AND a!=4)
         OR b=1012
         OR a=7
         OR b=773
         OR a=1
         OR b=726
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR b=110
  ]])
    end, {
        -- <where7-2.944.1>
        1, 3, 5, 7, 10, 66, 87, 89, 92, 99, "scan", 0, "sort", 0
        -- </where7-2.944.1>
    })

test:do_test(
    "where7-2.944.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 3 AND 5) AND a!=4)
         OR b=1012
         OR a=7
         OR b=773
         OR a=1
         OR b=726
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR ((a BETWEEN 87 AND 89) AND a!=88)
         OR b=110
  ]])
    end, {
        -- <where7-2.944.2>
        1, 3, 5, 7, 10, 66, 87, 89, 92, 99, "scan", 0, "sort", 0
        -- </where7-2.944.2>
    })

test:do_test(
    "where7-2.945.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'hijkl*')
         OR a=60
         OR a=4
         OR b=520
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR a=44
         OR a=36
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
         OR b=715
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.945.1>
        4, 7, 16, 36, 44, 60, 65, 76, 79, "scan", 0, "sort", 0
        -- </where7-2.945.1>
    })

test:do_test(
    "where7-2.945.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'hijkl*')
         OR a=60
         OR a=4
         OR b=520
         OR (g='ihgfedc' AND f GLOB 'bcdef*')
         OR a=44
         OR a=36
         OR (d>=76.0 AND d<77.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
         OR b=715
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
  ]])
    end, {
        -- <where7-2.945.2>
        4, 7, 16, 36, 44, 60, 65, 76, 79, "scan", 0, "sort", 0
        -- </where7-2.945.2>
    })

test:do_test(
    "where7-2.946.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 3 AND 5) AND a!=4)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR a=24
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
  ]])
    end, {
        -- <where7-2.946.1>
        3, 5, 15, 24, 26, 52, 55, 56, 58, 76, 78, 99, "scan", 0, "sort", 0
        -- </where7-2.946.1>
    })

test:do_test(
    "where7-2.946.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 3 AND 5) AND a!=4)
         OR ((a BETWEEN 56 AND 58) AND a!=57)
         OR (d>=15.0 AND d<16.0 AND d IS NOT NULL)
         OR (d>=55.0 AND d<56.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'yzabc*')
         OR a=24
         OR (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR (f GLOB '?bcde*' AND f GLOB 'abcd*')
  ]])
    end, {
        -- <where7-2.946.2>
        3, 5, 15, 24, 26, 52, 55, 56, 58, 76, 78, 99, "scan", 0, "sort", 0
        -- </where7-2.946.2>
    })

test:do_test(
    "where7-2.947.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR b=132
         OR f='ghijklmno'
         OR b=740
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR b=1059
  ]])
    end, {
        -- <where7-2.947.1>
        6, 12, 21, 26, 32, 38, 58, 84, "scan", 0, "sort", 0
        -- </where7-2.947.1>
    })

test:do_test(
    "where7-2.947.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='utsrqpo' AND f GLOB 'vwxyz*')
         OR b=132
         OR f='ghijklmno'
         OR b=740
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR b=1059
  ]])
    end, {
        -- <where7-2.947.2>
        6, 12, 21, 26, 32, 38, 58, 84, "scan", 0, "sort", 0
        -- </where7-2.947.2>
    })

test:do_test(
    "where7-2.948.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=28
         OR b=927
         OR b=520
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR b=638
         OR f='vwxyzabcd'
  ]])
    end, {
        -- <where7-2.948.1>
        21, 28, 47, 53, 58, 73, 99, "scan", 0, "sort", 0
        -- </where7-2.948.1>
    })

test:do_test(
    "where7-2.948.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=28
         OR b=927
         OR b=520
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
         OR b=638
         OR f='vwxyzabcd'
  ]])
    end, {
        -- <where7-2.948.2>
        21, 28, 47, 53, 58, 73, 99, "scan", 0, "sort", 0
        -- </where7-2.948.2>
    })

test:do_test(
    "where7-2.949.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='nmlkjih' AND f GLOB 'cdefg*')
         OR b=1026
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'lmnop*')
         OR b=355
         OR b=641
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.949.1>
        1, 11, 53, 54, "scan", 0, "sort", 0
        -- </where7-2.949.1>
    })

test:do_test(
    "where7-2.949.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='nmlkjih' AND f GLOB 'cdefg*')
         OR b=1026
         OR (d>=1.0 AND d<2.0 AND d IS NOT NULL)
         OR (g='wvutsrq' AND f GLOB 'lmnop*')
         OR b=355
         OR b=641
         OR (d>=53.0 AND d<54.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.949.2>
        1, 11, 53, 54, "scan", 0, "sort", 0
        -- </where7-2.949.2>
    })

test:do_test(
    "where7-2.950.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 46 AND 48) AND a!=47)
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR b=641
         OR a=3
         OR a=35
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR f='opqrstuvw'
         OR a=41
         OR a=83
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR b=751
  ]])
    end, {
        -- <where7-2.950.1>
        3, 14, 35, 40, 41, 46, 48, 54, 60, 62, 66, 81, 83, 92, "scan", 0, "sort", 0
        -- </where7-2.950.1>
    })

test:do_test(
    "where7-2.950.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 46 AND 48) AND a!=47)
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR b=641
         OR a=3
         OR a=35
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR f='opqrstuvw'
         OR a=41
         OR a=83
         OR (g='nmlkjih' AND f GLOB 'cdefg*')
         OR b=751
  ]])
    end, {
        -- <where7-2.950.2>
        3, 14, 35, 40, 41, 46, 48, 54, 60, 62, 66, 81, 83, 92, "scan", 0, "sort", 0
        -- </where7-2.950.2>
    })

test:do_test(
    "where7-2.951.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 3 AND 5) AND a!=4)
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR c=15015
         OR b=146
         OR b=1092
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.951.1>
        3, 5, 43, 44, 45, 60, 62, "scan", 0, "sort", 0
        -- </where7-2.951.1>
    })

test:do_test(
    "where7-2.951.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 3 AND 5) AND a!=4)
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR c=15015
         OR b=146
         OR b=1092
         OR (d>=60.0 AND d<61.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.951.2>
        3, 5, 43, 44, 45, 60, 62, "scan", 0, "sort", 0
        -- </where7-2.951.2>
    })

test:do_test(
    "where7-2.952.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='qponmlk' AND f GLOB 'qrstu*')
         OR f='bcdefghij'
         OR f='hijklmnop'
         OR a=65
         OR f='ijklmnopq'
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR g IS NULL
         OR a=26
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR a=9
         OR (d>=32.0 AND d<33.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.952.1>
        1, 7, 8, 9, 26, 27, 32, 33, 34, 38, 40, 42, 53, 59, 60, 65, 79, 85, 86, "scan", 0, "sort", 0
        -- </where7-2.952.1>
    })

test:do_test(
    "where7-2.952.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='qponmlk' AND f GLOB 'qrstu*')
         OR f='bcdefghij'
         OR f='hijklmnop'
         OR a=65
         OR f='ijklmnopq'
         OR (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR g IS NULL
         OR a=26
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR a=9
         OR (d>=32.0 AND d<33.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.952.2>
        1, 7, 8, 9, 26, 27, 32, 33, 34, 38, 40, 42, 53, 59, 60, 65, 79, 85, 86, "scan", 0, "sort", 0
        -- </where7-2.952.2>
    })

test:do_test(
    "where7-2.953.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='jihgfed' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR ((a BETWEEN 79 AND 81) AND a!=80)
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR b=1100
         OR c=6006
         OR c=4004
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR c=33033
  ]])
    end, {
        -- <where7-2.953.1>
        10, 11, 12, 16, 17, 18, 24, 26, 41, 70, 73, 79, 81, 97, 98, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.953.1>
    })

test:do_test(
    "where7-2.953.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='jihgfed' AND f GLOB 'vwxyz*')
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR ((a BETWEEN 79 AND 81) AND a!=80)
         OR (g='kjihgfe' AND f GLOB 'stuvw*')
         OR (g='qponmlk' AND f GLOB 'pqrst*')
         OR b=1100
         OR c=6006
         OR c=4004
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR c=33033
  ]])
    end, {
        -- <where7-2.953.2>
        10, 11, 12, 16, 17, 18, 24, 26, 41, 70, 73, 79, 81, 97, 98, 99, 100, "scan", 0, "sort", 0
        -- </where7-2.953.2>
    })

test:do_test(
    "where7-2.954.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=17
         OR ((a BETWEEN 95 AND 97) AND a!=96)
  ]])
    end, {
        -- <where7-2.954.1>
        17, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.954.1>
    })

test:do_test(
    "where7-2.954.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=17
         OR ((a BETWEEN 95 AND 97) AND a!=96)
  ]])
    end, {
        -- <where7-2.954.2>
        17, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.954.2>
    })

test:do_test(
    "where7-2.955.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=3003
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR a=93
  ]])
    end, {
        -- <where7-2.955.1>
        7, 8, 9, 67, 93, "scan", 0, "sort", 0
        -- </where7-2.955.1>
    })

test:do_test(
    "where7-2.955.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=3003
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR a=93
  ]])
    end, {
        -- <where7-2.955.2>
        7, 8, 9, 67, 93, "scan", 0, "sort", 0
        -- </where7-2.955.2>
    })

test:do_test(
    "where7-2.956.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=737
  ]])
    end, {
        -- <where7-2.956.1>
        12, 21, 23, 42, 44, 67, "scan", 0, "sort", 0
        -- </where7-2.956.1>
    })

test:do_test(
    "where7-2.956.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR ((a BETWEEN 21 AND 23) AND a!=22)
         OR (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR b=737
  ]])
    end, {
        -- <where7-2.956.2>
        12, 21, 23, 42, 44, 67, "scan", 0, "sort", 0
        -- </where7-2.956.2>
    })

test:do_test(
    "where7-2.957.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='rqponml' AND f GLOB 'klmno*')
         OR ((a BETWEEN 5 AND 7) AND a!=6)
  ]])
    end, {
        -- <where7-2.957.1>
        5, 7, 36, "scan", 0, "sort", 0
        -- </where7-2.957.1>
    })

test:do_test(
    "where7-2.957.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='rqponml' AND f GLOB 'klmno*')
         OR ((a BETWEEN 5 AND 7) AND a!=6)
  ]])
    end, {
        -- <where7-2.957.2>
        5, 7, 36, "scan", 0, "sort", 0
        -- </where7-2.957.2>
    })

test:do_test(
    "where7-2.958.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='hgfedcb' AND f GLOB 'hijkl*')
         OR c=32032
         OR f='opqrstuvw'
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR b=993
  ]])
    end, {
        -- <where7-2.958.1>
        14, 40, 49, 51, 66, 68, 85, 92, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.958.1>
    })

test:do_test(
    "where7-2.958.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='hgfedcb' AND f GLOB 'hijkl*')
         OR c=32032
         OR f='opqrstuvw'
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR ((a BETWEEN 49 AND 51) AND a!=50)
         OR b=993
  ]])
    end, {
        -- <where7-2.958.2>
        14, 40, 49, 51, 66, 68, 85, 92, 94, 95, 96, "scan", 0, "sort", 0
        -- </where7-2.958.2>
    })

test:do_test(
    "where7-2.959.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR b=245
         OR b=528
         OR b=366
         OR a=73
         OR a=49
         OR b=421
         OR a=58
  ]])
    end, {
        -- <where7-2.959.1>
        12, 38, 48, 49, 58, 59, 61, 73, 86, 88, "scan", 0, "sort", 0
        -- </where7-2.959.1>
    })

test:do_test(
    "where7-2.959.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=12.0 AND d<13.0 AND d IS NOT NULL)
         OR ((a BETWEEN 59 AND 61) AND a!=60)
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR b=245
         OR b=528
         OR b=366
         OR a=73
         OR a=49
         OR b=421
         OR a=58
  ]])
    end, {
        -- <where7-2.959.2>
        12, 38, 48, 49, 58, 59, 61, 73, 86, 88, "scan", 0, "sort", 0
        -- </where7-2.959.2>
    })

test:do_test(
    "where7-2.960.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR b=146
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR ((a BETWEEN 73 AND 75) AND a!=74)
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR b=704
  ]])
    end, {
        -- <where7-2.960.1>
        8, 10, 20, 43, 60, 62, 64, 73, 75, 82, 86, 88, 100, "scan", 0, "sort", 0
        -- </where7-2.960.1>
    })

test:do_test(
    "where7-2.960.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=100.0 AND d<101.0 AND d IS NOT NULL)
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR ((a BETWEEN 86 AND 88) AND a!=87)
         OR b=146
         OR (g='ponmlkj' AND f GLOB 'rstuv*')
         OR ((a BETWEEN 73 AND 75) AND a!=74)
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR ((a BETWEEN 60 AND 62) AND a!=61)
         OR (g='ihgfedc' AND f GLOB 'efghi*')
         OR b=704
  ]])
    end, {
        -- <where7-2.960.2>
        8, 10, 20, 43, 60, 62, 64, 73, 75, 82, 86, 88, 100, "scan", 0, "sort", 0
        -- </where7-2.960.2>
    })

test:do_test(
    "where7-2.961.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 65 AND 67) AND a!=66)
         OR b=14
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR a=49
         OR b=333
  ]])
    end, {
        -- <where7-2.961.1>
        3, 5, 49, 65, 67, "scan", 0, "sort", 0
        -- </where7-2.961.1>
    })

test:do_test(
    "where7-2.961.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 65 AND 67) AND a!=66)
         OR b=14
         OR ((a BETWEEN 3 AND 5) AND a!=4)
         OR a=49
         OR b=333
  ]])
    end, {
        -- <where7-2.961.2>
        3, 5, 49, 65, 67, "scan", 0, "sort", 0
        -- </where7-2.961.2>
    })

test:do_test(
    "where7-2.962.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=17017
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR b=971
         OR a=37
         OR a=7
         OR b=641
         OR a=13
         OR b=597
  ]])
    end, {
        -- <where7-2.962.1>
        7, 13, 37, 38, 49, 50, 51, "scan", 0, "sort", 0
        -- </where7-2.962.1>
    })

test:do_test(
    "where7-2.962.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=17017
         OR (g='qponmlk' AND f GLOB 'mnopq*')
         OR b=971
         OR a=37
         OR a=7
         OR b=641
         OR a=13
         OR b=597
  ]])
    end, {
        -- <where7-2.962.2>
        7, 13, 37, 38, 49, 50, 51, "scan", 0, "sort", 0
        -- </where7-2.962.2>
    })

test:do_test(
    "where7-2.963.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='tuvwxyzab'
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
  ]])
    end, {
        -- <where7-2.963.1>
        17, 19, 43, 45, 69, 71, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.963.1>
    })

test:do_test(
    "where7-2.963.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='tuvwxyzab'
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
  ]])
    end, {
        -- <where7-2.963.2>
        17, 19, 43, 45, 69, 71, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.963.2>
    })

test:do_test(
    "where7-2.964.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=638
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR b=165
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR f='stuvwxyza'
         OR b=652
         OR b=66
         OR b=770
         OR b=91
  ]])
    end, {
        -- <where7-2.964.1>
        6, 10, 12, 15, 18, 44, 58, 70, 89, 96, "scan", 0, "sort", 0
        -- </where7-2.964.1>
    })

test:do_test(
    "where7-2.964.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=638
         OR (d>=44.0 AND d<45.0 AND d IS NOT NULL)
         OR (g='gfedcba' AND f GLOB 'lmnop*')
         OR b=165
         OR ((a BETWEEN 10 AND 12) AND a!=11)
         OR f='stuvwxyza'
         OR b=652
         OR b=66
         OR b=770
         OR b=91
  ]])
    end, {
        -- <where7-2.964.2>
        6, 10, 12, 15, 18, 44, 58, 70, 89, 96, "scan", 0, "sort", 0
        -- </where7-2.964.2>
    })

test:do_test(
    "where7-2.965.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR f='opqrstuvw'
         OR a=83
         OR a=93
         OR b=858
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.965.1>
        14, 18, 40, 52, 66, 73, 78, 83, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.965.1>
    })

test:do_test(
    "where7-2.965.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=52.0 AND d<53.0 AND d IS NOT NULL)
         OR f='opqrstuvw'
         OR a=83
         OR a=93
         OR b=858
         OR (d>=18.0 AND d<19.0 AND d IS NOT NULL)
         OR (g='jihgfed' AND f GLOB 'vwxyz*')
  ]])
    end, {
        -- <where7-2.965.2>
        14, 18, 40, 52, 66, 73, 78, 83, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.965.2>
    })

test:do_test(
    "where7-2.966.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE c=3003
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR a=38
  ]])
    end, {
        -- <where7-2.966.1>
        7, 8, 9, 38, 40, 42, "scan", 0, "sort", 0
        -- </where7-2.966.1>
    })

test:do_test(
    "where7-2.966.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE c=3003
         OR ((a BETWEEN 40 AND 42) AND a!=41)
         OR a=38
  ]])
    end, {
        -- <where7-2.966.2>
        7, 8, 9, 38, 40, 42, "scan", 0, "sort", 0
        -- </where7-2.966.2>
    })

test:do_test(
    "where7-2.967.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR ((a BETWEEN 50 AND 52) AND a!=51)
  ]])
    end, {
        -- <where7-2.967.1>
        50, 52, 60, "scan", 0, "sort", 0
        -- </where7-2.967.1>
    })

test:do_test(
    "where7-2.967.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=60.0 AND d<61.0 AND d IS NOT NULL)
         OR ((a BETWEEN 50 AND 52) AND a!=51)
  ]])
    end, {
        -- <where7-2.967.2>
        50, 52, 60, "scan", 0, "sort", 0
        -- </where7-2.967.2>
    })

test:do_test(
    "where7-2.968.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='qponmlk' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR a=5
         OR b=396
         OR a=13
  ]])
    end, {
        -- <where7-2.968.1>
        5, 13, 24, 26, 36, 38, "scan", 0, "sort", 0
        -- </where7-2.968.1>
    })

test:do_test(
    "where7-2.968.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='qponmlk' AND f GLOB 'mnopq*')
         OR ((a BETWEEN 24 AND 26) AND a!=25)
         OR a=5
         OR b=396
         OR a=13
  ]])
    end, {
        -- <where7-2.968.2>
        5, 13, 24, 26, 36, 38, "scan", 0, "sort", 0
        -- </where7-2.968.2>
    })

test:do_test(
    "where7-2.969.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=748
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR b=531
         OR b=1092
         OR b=418
  ]])
    end, {
        -- <where7-2.969.1>
        38, 68, 69, 71, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.969.1>
    })

test:do_test(
    "where7-2.969.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='fedcbaz' AND f GLOB 'rstuv*')
         OR b=748
         OR (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR ((a BETWEEN 69 AND 71) AND a!=70)
         OR b=531
         OR b=1092
         OR b=418
  ]])
    end, {
        -- <where7-2.969.2>
        38, 68, 69, 71, 95, 97, "scan", 0, "sort", 0
        -- </where7-2.969.2>
    })

test:do_test(
    "where7-2.970.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=30.0 AND d<31.0 AND d IS NOT NULL)
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR a=50
         OR a=46
         OR ((a BETWEEN 38 AND 40) AND a!=39)
  ]])
    end, {
        -- <where7-2.970.1>
        8, 10, 14, 30, 38, 40, 46, 50, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.970.1>
    })

test:do_test(
    "where7-2.970.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=30.0 AND d<31.0 AND d IS NOT NULL)
         OR ((a BETWEEN 8 AND 10) AND a!=9)
         OR (f GLOB '?pqrs*' AND f GLOB 'opqr*')
         OR a=50
         OR a=46
         OR ((a BETWEEN 38 AND 40) AND a!=39)
  ]])
    end, {
        -- <where7-2.970.2>
        8, 10, 14, 30, 38, 40, 46, 50, 66, 92, "scan", 0, "sort", 0
        -- </where7-2.970.2>
    })

test:do_test(
    "where7-2.971.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=24
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR b=487
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR ((a BETWEEN 13 AND 15) AND a!=14)
         OR b=132
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR b=795
         OR b=737
  ]])
    end, {
        -- <where7-2.971.1>
        12, 13, 15, 22, 24, 54, 67, 96, "scan", 0, "sort", 0
        -- </where7-2.971.1>
    })

test:do_test(
    "where7-2.971.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=24
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR b=487
         OR (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR ((a BETWEEN 13 AND 15) AND a!=14)
         OR b=132
         OR (d>=54.0 AND d<55.0 AND d IS NOT NULL)
         OR b=795
         OR b=737
  ]])
    end, {
        -- <where7-2.971.2>
        12, 13, 15, 22, 24, 54, 67, 96, "scan", 0, "sort", 0
        -- </where7-2.971.2>
    })

test:do_test(
    "where7-2.972.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR a=34
         OR c=16016
         OR b=1078
         OR b=960
         OR (g='hgfedcb' AND f GLOB 'jklmn*')
  ]])
    end, {
        -- <where7-2.972.1>
        34, 46, 47, 48, 87, 88, 98, "scan", 0, "sort", 0
        -- </where7-2.972.1>
    })

test:do_test(
    "where7-2.972.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=88.0 AND d<89.0 AND d IS NOT NULL)
         OR a=34
         OR c=16016
         OR b=1078
         OR b=960
         OR (g='hgfedcb' AND f GLOB 'jklmn*')
  ]])
    end, {
        -- <where7-2.972.2>
        34, 46, 47, 48, 87, 88, 98, "scan", 0, "sort", 0
        -- </where7-2.972.2>
    })

test:do_test(
    "where7-2.973.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1081
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 73 AND 75) AND a!=74)
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR a=6
  ]])
    end, {
        -- <where7-2.973.1>
        6, 19, 21, 38, 45, 73, 75, "scan", 0, "sort", 0
        -- </where7-2.973.1>
    })

test:do_test(
    "where7-2.973.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1081
         OR ((a BETWEEN 19 AND 21) AND a!=20)
         OR (g='ponmlkj' AND f GLOB 'tuvwx*')
         OR ((a BETWEEN 73 AND 75) AND a!=74)
         OR (d>=38.0 AND d<39.0 AND d IS NOT NULL)
         OR a=6
  ]])
    end, {
        -- <where7-2.973.2>
        6, 19, 21, 38, 45, 73, 75, "scan", 0, "sort", 0
        -- </where7-2.973.2>
    })

test:do_test(
    "where7-2.974.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='fedcbaz' AND f GLOB 'rstuv*')
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR a=92
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR f='fghijklmn'
         OR a=100
         OR b=209
         OR c=9009
         OR ((a BETWEEN 52 AND 54) AND a!=53)
         OR a=73
         OR b=902
  ]])
    end, {
        -- <where7-2.974.1>
        5, 9, 19, 25, 26, 27, 31, 35, 37, 52, 54, 57, 61, 73, 82, 83, 87, 92, 95, 100, "scan", 0, "sort", 0
        -- </where7-2.974.1>
    })

test:do_test(
    "where7-2.974.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='fedcbaz' AND f GLOB 'rstuv*')
         OR (g='rqponml' AND f GLOB 'lmnop*')
         OR a=92
         OR (f GLOB '?klmn*' AND f GLOB 'jklm*')
         OR f='fghijklmn'
         OR a=100
         OR b=209
         OR c=9009
         OR ((a BETWEEN 52 AND 54) AND a!=53)
         OR a=73
         OR b=902
  ]])
    end, {
        -- <where7-2.974.2>
        5, 9, 19, 25, 26, 27, 31, 35, 37, 52, 54, 57, 61, 73, 82, 83, 87, 92, 95, 100, "scan", 0, "sort", 0
        -- </where7-2.974.2>
    })

test:do_test(
    "where7-2.975.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR b=110
         OR f='ghijklmno'
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.975.1>
        6, 10, 21, 32, 40, 58, 84, "scan", 0, "sort", 0
        -- </where7-2.975.1>
    })

test:do_test(
    "where7-2.975.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=40.0 AND d<41.0 AND d IS NOT NULL)
         OR b=110
         OR f='ghijklmno'
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.975.2>
        6, 10, 21, 32, 40, 58, 84, "scan", 0, "sort", 0
        -- </where7-2.975.2>
    })

test:do_test(
    "where7-2.976.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 51 AND 53) AND a!=52)
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR b=91
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=77
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
  ]])
    end, {
        -- <where7-2.976.1>
        1, 7, 15, 20, 27, 45, 46, 51, 53, 79, "scan", 0, "sort", 0
        -- </where7-2.976.1>
    })

test:do_test(
    "where7-2.976.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 51 AND 53) AND a!=52)
         OR (g='utsrqpo' AND f GLOB 'uvwxy*')
         OR (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR b=91
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR b=77
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR (g='vutsrqp' AND f GLOB 'pqrst*')
  ]])
    end, {
        -- <where7-2.976.2>
        1, 7, 15, 20, 27, 45, 46, 51, 53, 79, "scan", 0, "sort", 0
        -- </where7-2.976.2>
    })

test:do_test(
    "where7-2.977.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR b=693
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR a=52
         OR b=377
  ]])
    end, {
        -- <where7-2.977.1>
        21, 26, 42, 52, 56, 63, 78, "scan", 0, "sort", 0
        -- </where7-2.977.1>
    })

test:do_test(
    "where7-2.977.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=56.0 AND d<57.0 AND d IS NOT NULL)
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR b=693
         OR (d>=21.0 AND d<22.0 AND d IS NOT NULL)
         OR (d>=26.0 AND d<27.0 AND d IS NOT NULL)
         OR (d>=42.0 AND d<43.0 AND d IS NOT NULL)
         OR a=52
         OR b=377
  ]])
    end, {
        -- <where7-2.977.2>
        21, 26, 42, 52, 56, 63, 78, "scan", 0, "sort", 0
        -- </where7-2.977.2>
    })

test:do_test(
    "where7-2.978.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=99
         OR a=36
         OR b=297
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=1004
         OR b=872
         OR a=95
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR b=176
         OR b=300
  ]])
    end, {
        -- <where7-2.978.1>
        16, 27, 36, 66, 68, 95, 99, "scan", 0, "sort", 0
        -- </where7-2.978.1>
    })

test:do_test(
    "where7-2.978.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=99
         OR a=36
         OR b=297
         OR ((a BETWEEN 66 AND 68) AND a!=67)
         OR b=1004
         OR b=872
         OR a=95
         OR (d>=27.0 AND d<28.0 AND d IS NOT NULL)
         OR b=176
         OR b=300
  ]])
    end, {
        -- <where7-2.978.2>
        16, 27, 36, 66, 68, 95, 99, "scan", 0, "sort", 0
        -- </where7-2.978.2>
    })

test:do_test(
    "where7-2.979.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=737
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
         OR a=40
         OR f='uvwxyzabc'
         OR b=311
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR b=927
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.979.1>
        5, 6, 8, 20, 31, 32, 40, 46, 50, 53, 57, 58, 67, 72, 83, 84, 98, "scan", 0, "sort", 0
        -- </where7-2.979.1>
    })

test:do_test(
    "where7-2.979.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=737
         OR (g='wvutsrq' AND f GLOB 'ijklm*')
         OR (f GLOB '?ghij*' AND f GLOB 'fghi*')
         OR a=40
         OR f='uvwxyzabc'
         OR b=311
         OR (g='nmlkjih' AND f GLOB 'bcdef*')
         OR (f GLOB '?hijk*' AND f GLOB 'ghij*')
         OR b=927
         OR (d>=50.0 AND d<51.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.979.2>
        5, 6, 8, 20, 31, 32, 40, 46, 50, 53, 57, 58, 67, 72, 83, 84, 98, "scan", 0, "sort", 0
        -- </where7-2.979.2>
    })

test:do_test(
    "where7-2.980.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE f='fghijklmn'
         OR b=1078
         OR (d>=35.0 AND d<36.0 AND d IS NOT NULL)
         OR f='fghijklmn'
  ]])
    end, {
        -- <where7-2.980.1>
        5, 31, 35, 57, 83, 98, "scan", 0, "sort", 0
        -- </where7-2.980.1>
    })

test:do_test(
    "where7-2.980.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE f='fghijklmn'
         OR b=1078
         OR (d>=35.0 AND d<36.0 AND d IS NOT NULL)
         OR f='fghijklmn'
  ]])
    end, {
        -- <where7-2.980.2>
        5, 31, 35, 57, 83, 98, "scan", 0, "sort", 0
        -- </where7-2.980.2>
    })

test:do_test(
    "where7-2.981.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='xwvutsr' AND f GLOB 'ghijk*')
         OR b=487
         OR f='tuvwxyzab'
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR b=971
         OR c=19019
         OR a=39
         OR (f GLOB '?nopq*' AND f GLOB 'mnop*')
         OR b=550
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR b=660
  ]])
    end, {
        -- <where7-2.981.1>
        6, 12, 19, 38, 39, 45, 48, 50, 55, 56, 57, 60, 64, 71, 90, 97, "scan", 0, "sort", 0
        -- </where7-2.981.1>
    })

test:do_test(
    "where7-2.981.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='xwvutsr' AND f GLOB 'ghijk*')
         OR b=487
         OR f='tuvwxyzab'
         OR (g='onmlkji' AND f GLOB 'wxyza*')
         OR b=971
         OR c=19019
         OR a=39
         OR (f GLOB '?nopq*' AND f GLOB 'mnop*')
         OR b=550
         OR (g='kjihgfe' AND f GLOB 'tuvwx*')
         OR b=660
  ]])
    end, {
        -- <where7-2.981.2>
        6, 12, 19, 38, 39, 45, 48, 50, 55, 56, 57, 60, 64, 71, 90, 97, "scan", 0, "sort", 0
        -- </where7-2.981.2>
    })

test:do_test(
    "where7-2.982.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=80
         OR b=839
  ]])
    end, {
        -- <where7-2.982.1>
        "scan", 0, "sort", 0
        -- </where7-2.982.1>
    })

test:do_test(
    "where7-2.982.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=80
         OR b=839
  ]])
    end, {
        -- <where7-2.982.2>
        "scan", 0, "sort", 0
        -- </where7-2.982.2>
    })

test:do_test(
    "where7-2.983.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=363
         OR b=630
         OR b=935
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR f='yzabcdefg'
         OR ((a BETWEEN 37 AND 39) AND a!=38)
  ]])
    end, {
        -- <where7-2.983.1>
        20, 24, 29, 33, 37, 39, 50, 76, 85, "scan", 0, "sort", 0
        -- </where7-2.983.1>
    })

test:do_test(
    "where7-2.983.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=363
         OR b=630
         OR b=935
         OR (d>=20.0 AND d<21.0 AND d IS NOT NULL)
         OR (g='srqponm' AND f GLOB 'defgh*')
         OR f='yzabcdefg'
         OR ((a BETWEEN 37 AND 39) AND a!=38)
  ]])
    end, {
        -- <where7-2.983.2>
        20, 24, 29, 33, 37, 39, 50, 76, 85, "scan", 0, "sort", 0
        -- </where7-2.983.2>
    })

test:do_test(
    "where7-2.984.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR a=40
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR f='abcdefghi'
         OR b=696
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
         OR b=682
         OR a=32
         OR ((a BETWEEN 34 AND 36) AND a!=35)
         OR b=671
         OR a=15
  ]])
    end, {
        -- <where7-2.984.1>
        15, 16, 26, 32, 34, 36, 40, 52, 61, 62, 78, 86, 97, "scan", 0, "sort", 0
        -- </where7-2.984.1>
    })

test:do_test(
    "where7-2.984.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=97.0 AND d<98.0 AND d IS NOT NULL)
         OR a=40
         OR (d>=86.0 AND d<87.0 AND d IS NOT NULL)
         OR f='abcdefghi'
         OR b=696
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
         OR b=682
         OR a=32
         OR ((a BETWEEN 34 AND 36) AND a!=35)
         OR b=671
         OR a=15
  ]])
    end, {
        -- <where7-2.984.2>
        15, 16, 26, 32, 34, 36, 40, 52, 61, 62, 78, 86, 97, "scan", 0, "sort", 0
        -- </where7-2.984.2>
    })

test:do_test(
    "where7-2.985.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (g='gfedcba' AND f GLOB 'lmnop*')
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR b=311
  ]])
    end, {
        -- <where7-2.985.1>
        7, 33, 59, 85, 89, "scan", 0, "sort", 0
        -- </where7-2.985.1>
    })

test:do_test(
    "where7-2.985.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (g='gfedcba' AND f GLOB 'lmnop*')
         OR (f GLOB '?ijkl*' AND f GLOB 'hijk*')
         OR b=311
  ]])
    end, {
        -- <where7-2.985.2>
        7, 33, 59, 85, 89, "scan", 0, "sort", 0
        -- </where7-2.985.2>
    })

test:do_test(
    "where7-2.986.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR a=73
         OR b=729
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR a=32
  ]])
    end, {
        -- <where7-2.986.1>
        32, 67, 73, 81, 96, "scan", 0, "sort", 0
        -- </where7-2.986.1>
    })

test:do_test(
    "where7-2.986.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=96.0 AND d<97.0 AND d IS NOT NULL)
         OR a=73
         OR b=729
         OR (d>=81.0 AND d<82.0 AND d IS NOT NULL)
         OR (d>=67.0 AND d<68.0 AND d IS NOT NULL)
         OR a=32
  ]])
    end, {
        -- <where7-2.986.2>
        32, 67, 73, 81, 96, "scan", 0, "sort", 0
        -- </where7-2.986.2>
    })

test:do_test(
    "where7-2.987.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 98 AND 100) AND a!=99)
         OR b=110
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR b=484
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.987.1>
        10, 23, 38, 40, 44, 82, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.987.1>
    })

test:do_test(
    "where7-2.987.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 98 AND 100) AND a!=99)
         OR b=110
         OR ((a BETWEEN 38 AND 40) AND a!=39)
         OR (g='tsrqpon' AND f GLOB 'xyzab*')
         OR b=484
         OR (d>=82.0 AND d<83.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.987.2>
        10, 23, 38, 40, 44, 82, 98, 100, "scan", 0, "sort", 0
        -- </where7-2.987.2>
    })

test:do_test(
    "where7-2.988.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR b=135
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR b=209
         OR b=363
         OR c=27027
         OR b=1026
         OR c=6006
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.988.1>
        16, 17, 18, 19, 33, 46, 66, 73, 79, 80, 81, 99, "scan", 0, "sort", 0
        -- </where7-2.988.1>
    })

test:do_test(
    "where7-2.988.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=99.0 AND d<100.0 AND d IS NOT NULL)
         OR b=135
         OR (d>=66.0 AND d<67.0 AND d IS NOT NULL)
         OR b=209
         OR b=363
         OR c=27027
         OR b=1026
         OR c=6006
         OR (g='ponmlkj' AND f GLOB 'uvwxy*')
         OR (d>=73.0 AND d<74.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.988.2>
        16, 17, 18, 19, 33, 46, 66, 73, 79, 80, 81, 99, "scan", 0, "sort", 0
        -- </where7-2.988.2>
    })

test:do_test(
    "where7-2.989.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR a=97
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR b=674
         OR c=14014
         OR b=69
  ]])
    end, {
        -- <where7-2.989.1>
        18, 20, 22, 24, 39, 40, 41, 42, 45, 58, 79, 97, "scan", 0, "sort", 0
        -- </where7-2.989.1>
    })

test:do_test(
    "where7-2.989.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=79.0 AND d<80.0 AND d IS NOT NULL)
         OR ((a BETWEEN 18 AND 20) AND a!=19)
         OR (g='qponmlk' AND f GLOB 'nopqr*')
         OR a=97
         OR (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR ((a BETWEEN 22 AND 24) AND a!=23)
         OR (g='mlkjihg' AND f GLOB 'ghijk*')
         OR b=674
         OR c=14014
         OR b=69
  ]])
    end, {
        -- <where7-2.989.2>
        18, 20, 22, 24, 39, 40, 41, 42, 45, 58, 79, 97, "scan", 0, "sort", 0
        -- </where7-2.989.2>
    })

test:do_test(
    "where7-2.990.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=297
         OR a=83
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR ((a BETWEEN 16 AND 18) AND a!=17)
  ]])
    end, {
        -- <where7-2.990.1>
        16, 18, 27, 78, 83, "scan", 0, "sort", 0
        -- </where7-2.990.1>
    })

test:do_test(
    "where7-2.990.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=297
         OR a=83
         OR (d>=78.0 AND d<79.0 AND d IS NOT NULL)
         OR ((a BETWEEN 16 AND 18) AND a!=17)
  ]])
    end, {
        -- <where7-2.990.2>
        16, 18, 27, 78, 83, "scan", 0, "sort", 0
        -- </where7-2.990.2>
    })

test:do_test(
    "where7-2.991.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=451
         OR ((a BETWEEN 11 AND 13) AND a!=12)
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR b=539
         OR a=26
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR b=465
         OR (g='jihgfed' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.991.1>
        11, 13, 26, 30, 41, 49, 74, "scan", 0, "sort", 0
        -- </where7-2.991.1>
    })

test:do_test(
    "where7-2.991.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=451
         OR ((a BETWEEN 11 AND 13) AND a!=12)
         OR (g='tsrqpon' AND f GLOB 'abcde*')
         OR b=539
         OR a=26
         OR (g='srqponm' AND f GLOB 'efghi*')
         OR b=465
         OR (g='jihgfed' AND f GLOB 'wxyza*')
  ]])
    end, {
        -- <where7-2.991.2>
        11, 13, 26, 30, 41, 49, 74, "scan", 0, "sort", 0
        -- </where7-2.991.2>
    })

test:do_test(
    "where7-2.992.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.992.1>
        45, 63, "scan", 0, "sort", 0
        -- </where7-2.992.1>
    })

test:do_test(
    "where7-2.992.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (d>=45.0 AND d<46.0 AND d IS NOT NULL)
         OR (d>=63.0 AND d<64.0 AND d IS NOT NULL)
  ]])
    end, {
        -- <where7-2.992.2>
        45, 63, "scan", 0, "sort", 0
        -- </where7-2.992.2>
    })

test:do_test(
    "where7-2.993.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 16 AND 18) AND a!=17)
         OR b=872
         OR c=31031
  ]])
    end, {
        -- <where7-2.993.1>
        16, 18, 91, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.993.1>
    })

test:do_test(
    "where7-2.993.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 16 AND 18) AND a!=17)
         OR b=872
         OR c=31031
  ]])
    end, {
        -- <where7-2.993.2>
        16, 18, 91, 92, 93, "scan", 0, "sort", 0
        -- </where7-2.993.2>
    })

test:do_test(
    "where7-2.994.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR a=13
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR b=322
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR b=377
         OR f='cdefghijk'
         OR b=286
         OR ((a BETWEEN 61 AND 63) AND a!=62)
  ]])
    end, {
        -- <where7-2.994.1>
        1, 2, 13, 17, 26, 27, 28, 33, 35, 43, 53, 54, 61, 63, 69, 79, 80, 95, "scan", 0, "sort", 0
        -- </where7-2.994.1>
    })

test:do_test(
    "where7-2.994.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE (f GLOB '?cdef*' AND f GLOB 'bcde*')
         OR a=13
         OR (f GLOB '?stuv*' AND f GLOB 'rstu*')
         OR b=322
         OR ((a BETWEEN 33 AND 35) AND a!=34)
         OR b=377
         OR f='cdefghijk'
         OR b=286
         OR ((a BETWEEN 61 AND 63) AND a!=62)
  ]])
    end, {
        -- <where7-2.994.2>
        1, 2, 13, 17, 26, 27, 28, 33, 35, 43, 53, 54, 61, 63, 69, 79, 80, 95, "scan", 0, "sort", 0
        -- </where7-2.994.2>
    })

test:do_test(
    "where7-2.995.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=41
         OR b=990
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR b=605
         OR (g='srqponm' AND f GLOB 'cdefg*')
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
         OR b=968
         OR a=66
  ]])
    end, {
        -- <where7-2.995.1>
        16, 28, 36, 41, 55, 66, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.995.1>
    })

test:do_test(
    "where7-2.995.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=41
         OR b=990
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR b=605
         OR (g='srqponm' AND f GLOB 'cdefg*')
         OR (d>=36.0 AND d<37.0 AND d IS NOT NULL)
         OR (g='vutsrqp' AND f GLOB 'qrstu*')
         OR b=968
         OR a=66
  ]])
    end, {
        -- <where7-2.995.2>
        16, 28, 36, 41, 55, 66, 88, 90, "scan", 0, "sort", 0
        -- </where7-2.995.2>
    })

test:do_test(
    "where7-2.996.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1059
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR ((a BETWEEN 39 AND 41) AND a!=40)
  ]])
    end, {
        -- <where7-2.996.1>
        17, 19, 32, 37, 39, 41, 52, 57, "scan", 0, "sort", 0
        -- </where7-2.996.1>
    })

test:do_test(
    "where7-2.996.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1059
         OR (g='srqponm' AND f GLOB 'ghijk*')
         OR (g='utsrqpo' AND f GLOB 'tuvwx*')
         OR (g='nmlkjih' AND f GLOB 'fghij*')
         OR (d>=17.0 AND d<18.0 AND d IS NOT NULL)
         OR (d>=37.0 AND d<38.0 AND d IS NOT NULL)
         OR (g='onmlkji' AND f GLOB 'abcde*')
         OR ((a BETWEEN 39 AND 41) AND a!=40)
  ]])
    end, {
        -- <where7-2.996.2>
        17, 19, 32, 37, 39, 41, 52, 57, "scan", 0, "sort", 0
        -- </where7-2.996.2>
    })

test:do_test(
    "where7-2.997.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE ((a BETWEEN 41 AND 43) AND a!=42)
         OR f='nopqrstuv'
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR a=42
         OR b=729
         OR b=297
         OR a=77
         OR b=781
         OR ((a BETWEEN 36 AND 38) AND a!=37)
  ]])
    end, {
        -- <where7-2.997.1>
        13, 27, 36, 38, 39, 41, 42, 43, 44, 65, 71, 77, 91, "scan", 0, "sort", 0
        -- </where7-2.997.1>
    })

test:do_test(
    "where7-2.997.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE ((a BETWEEN 41 AND 43) AND a!=42)
         OR f='nopqrstuv'
         OR (g='ponmlkj' AND f GLOB 'stuvw*')
         OR a=42
         OR b=729
         OR b=297
         OR a=77
         OR b=781
         OR ((a BETWEEN 36 AND 38) AND a!=37)
  ]])
    end, {
        -- <where7-2.997.2>
        13, 27, 36, 38, 39, 41, 42, 43, 44, 65, 71, 77, 91, "scan", 0, "sort", 0
        -- </where7-2.997.2>
    })

test:do_test(
    "where7-2.998.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE a=12
         OR f='qrstuvwxy'
         OR a=47
         OR b=135
         OR a=25
  ]])
    end, {
        -- <where7-2.998.1>
        12, 16, 25, 42, 47, 68, 94, "scan", 0, "sort", 0
        -- </where7-2.998.1>
    })

test:do_test(
    "where7-2.998.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE a=12
         OR f='qrstuvwxy'
         OR a=47
         OR b=135
         OR a=25
  ]])
    end, {
        -- <where7-2.998.2>
        12, 16, 25, 42, 47, 68, 94, "scan", 0, "sort", 0
        -- </where7-2.998.2>
    })

test:do_test(
    "where7-2.999.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=451
         OR b=660
         OR (g='onmlkji' AND f GLOB 'yzabc*')
         OR b=781
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR b=198
         OR b=1023
         OR a=98
         OR d<0.0
         OR ((a BETWEEN 79 AND 81) AND a!=80)
  ]])
    end, {
        -- <where7-2.999.1>
        18, 41, 50, 60, 71, 74, 79, 81, 93, 98, "scan", 0, "sort", 0
        -- </where7-2.999.1>
    })

test:do_test(
    "where7-2.999.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=451
         OR b=660
         OR (g='onmlkji' AND f GLOB 'yzabc*')
         OR b=781
         OR (g='jihgfed' AND f GLOB 'wxyza*')
         OR b=198
         OR b=1023
         OR a=98
         OR d<0.0
         OR ((a BETWEEN 79 AND 81) AND a!=80)
  ]])
    end, {
        -- <where7-2.999.2>
        18, 41, 50, 60, 71, 74, 79, 81, 93, 98, "scan", 0, "sort", 0
        -- </where7-2.999.2>
    })

test:do_test(
    "where7-2.1000.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=685
         OR a=86
         OR c=17017
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR a=80
         OR b=773
  ]])
    end, {
        -- <where7-2.1000.1>
        49, 50, 51, 80, 85, 86, 87, 90, "scan", 0, "sort", 0
        -- </where7-2.1000.1>
    })

test:do_test(
    "where7-2.1000.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=685
         OR a=86
         OR c=17017
         OR ((a BETWEEN 85 AND 87) AND a!=86)
         OR (g='gfedcba' AND f GLOB 'mnopq*')
         OR a=80
         OR b=773
  ]])
    end, {
        -- <where7-2.1000.2>
        49, 50, 51, 80, 85, 86, 87, 90, "scan", 0, "sort", 0
        -- </where7-2.1000.2>
    })

test:do_test(
    "where7-2.1001.1",
    function()
        return count_steps_sort([[
     SELECT a FROM t2
      WHERE b=1092
         OR a=23
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR d<0.0
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR a=91
  ]])
    end, {
        -- <where7-2.1001.1>
        2, 22, 23, 28, 54, 80, 91, "scan", 0, "sort", 0
        -- </where7-2.1001.1>
    })

test:do_test(
    "where7-2.1001.2",
    function()
        return count_steps_sort([[
     SELECT a FROM t3
      WHERE b=1092
         OR a=23
         OR (f GLOB '?defg*' AND f GLOB 'cdef*')
         OR d<0.0
         OR (d>=22.0 AND d<23.0 AND d IS NOT NULL)
         OR a=91
  ]])
    end, {
        -- <where7-2.1001.2>
        2, 22, 23, 28, 54, 80, 91, "scan", 0, "sort", 0
        -- </where7-2.1001.2>
    })

-- # test case for the performance regression fixed by
-- # check-in 28ba6255282b on 2010-10-21 02:05:06
-- #
-- # The test case that follows is code from an actual
-- # application with identifiers change and unused columns
-- # removed.
-- #
-- do_execsql_test where7-3.1 {
--   CREATE TABLE t301 (
--       c8 INTEGER PRIMARY KEY,
--       c6 INTEGER,
--       c4 INTEGER,
--       c7 INTEGER,
--       FOREIGN KEY (c4) REFERENCES series(c4)
--   );
--   CREATE INDEX t301_c6 on t301(c6);
--   CREATE INDEX t301_c4 on t301(c4);
--   CREATE INDEX t301_c7 on t301(c7);
--   CREATE TABLE t302 (
--       c1 INTEGER PRIMARY KEY,
--       c8 INTEGER,
--       c5 INTEGER,
--       c3 INTEGER,
--       c2 INTEGER,
--       c4 INTEGER,
--       FOREIGN KEY (c8) REFERENCES t301(c8)
--   );
--   CREATE INDEX t302_c3 on t302(c3);
--   CREATE INDEX t302_c8_c3 on t302(c8, c3);
--   CREATE INDEX t302_c5 on t302(c5);
--   EXPLAIN QUERY PLAN
--   SELECT t302.c1 
--     FROM t302 JOIN t301 ON t302.c8 = +t301.c8
--     WHERE t302.c2 = 19571
--       AND t302.c3 > 1287603136
--       AND (t301.c4 = 1407449685622784
--            OR t301.c8 = 1407424651264000)
--    ORDER BY t302.c5 LIMIT 200;
-- } {
--   0 0 1 {SEARCH TABLE t301 USING COVERING INDEX t301_c4 (c4=?)} 
--   0 0 1 {SEARCH TABLE t301 USING INTEGER PRIMARY KEY (rowid=?)} 
--   0 1 0 {SEARCH TABLE t302 USING INDEX t302_c8_c3 (c8=? AND c3>?)} 
--   0 0 0 {USE TEMP B-TREE FOR ORDER BY}
-- }


test:finish_test()
