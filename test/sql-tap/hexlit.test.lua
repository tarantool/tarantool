#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(128)

--!./tcltestrunner.lua
-- 2014-07-23
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
-- This file implements tests for hexadecimal literals
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local function hexlit1(tnum, val, ans)
    test:do_execsql_test(
        "hexlit-"..tnum,
        "SELECT "..val.."", {
            ans
        })

end

hexlit1(100, "0x0", 0)
hexlit1(101, "0x0000000000000000000000000000000000000000000001", 1)
hexlit1(102, "0x2", 2)
hexlit1(103, "0x4", 4)
hexlit1(104, "0x8", 8)
hexlit1(105, "0x00000000000000000000000000000000000000000000010", 16)
hexlit1(103, "0x20", 32)
hexlit1(106, "0x40", 64)
hexlit1(107, "0x80", 128)
hexlit1(108, "0x100", 256)
hexlit1(109, "0x200", 512)
hexlit1(110, "0X400", 1024)
hexlit1(111, "0x800", 2048)
hexlit1(112, "0x1000", 4096)
hexlit1(113, "0x2000", 8192)
hexlit1(114, "0x4000", 16384)
hexlit1(115, "0x8000", 32768)
hexlit1(116, "0x10000", 65536)
hexlit1(117, "0x20000", 131072)
hexlit1(118, "0x40000", 262144)
hexlit1(119, "0x80000", 524288)
hexlit1(120, "0x100000", 1048576)
hexlit1(121, "0x200000", 2097152)
hexlit1(122, "0x400000", 4194304)
hexlit1(123, "0x800000", 8388608)
hexlit1(124, "0x1000000", 16777216)
hexlit1(125, "0x2000000", 33554432)
hexlit1(126, "0x4000000", 67108864)
hexlit1(127, "0x8000000", 134217728)
hexlit1(128, "0x10000000", 268435456)
hexlit1(129, "0x20000000", 536870912)
hexlit1(130, "0x40000000", 1073741824)
hexlit1(131, "0x80000000", 2147483648)
hexlit1(132, "0x100000000", 4294967296)
hexlit1(133, "0x200000000", 8589934592)
hexlit1(134, "0x400000000", 17179869184)
hexlit1(135, "0x800000000", 34359738368)
hexlit1(136, "0x1000000000", 68719476736)
hexlit1(137, "0x2000000000", 137438953472)
hexlit1(138, "0x4000000000", 274877906944)
hexlit1(139, "0x8000000000", 549755813888)
hexlit1(140, "0x10000000000", 1099511627776)
hexlit1(141, "0x20000000000", 2199023255552)
hexlit1(142, "0x40000000000", 4398046511104)
hexlit1(143, "0x80000000000", 8796093022208)
hexlit1(144, "0x100000000000", 17592186044416)
hexlit1(145, "0x200000000000", 35184372088832)
hexlit1(146, "0x400000000000", 70368744177664)
hexlit1(147, "0x800000000000", 140737488355328LL)
hexlit1(148, "0x1000000000000", 281474976710656LL)
hexlit1(149, "0x2000000000000", 562949953421312LL)
hexlit1(150, "0x4000000000000", 1125899906842624LL)
hexlit1(151, "0x8000000000000", 2251799813685248LL)
hexlit1(152, "0x10000000000000", 4503599627370496LL)
hexlit1(153, "0x20000000000000", 9007199254740992LL)
hexlit1(154, "0x40000000000000", 18014398509481984LL)
hexlit1(155, "0x80000000000000", 36028797018963968LL)
hexlit1(156, "0x100000000000000", 72057594037927936LL)
hexlit1(157, "0x200000000000000", 144115188075855872LL)
hexlit1(158, "0x400000000000000", 288230376151711744LL)
hexlit1(159, "0x800000000000000", 576460752303423488LL)
hexlit1(160, "0X1000000000000000", 1152921504606846976LL)
hexlit1(161, "0x2000000000000000", 2305843009213693952LL)
hexlit1(162, "0X4000000000000000", 4611686018427387904LL)
hexlit1(163, "0x8000000000000000", -9223372036854775808LL)
hexlit1(164, "0XFFFFFFFFFFFFFFFF", 18446744073709551615LL)
for n = 1, 0x10 -1, 1 do
    hexlit1("200."..n..".1", "0X"..string.format("%03X",n), n)
    hexlit1("200."..n..".2", "0x"..string.format("%03X",n), n)
    hexlit1("200."..n..".3", "0X"..string.format("%03X",n), n)
    hexlit1("200."..n..".4", "0x"..string.format("%03X",n), n)
end

-- Oversized hex literals are rejected
--
test:do_catchsql_test(
    "hexlist-400",
    [[
        SELECT 0x10000000000000000;
    ]], {
        -- <hexlist-400>
        1, "Hex literal 0x10000000000000000 length 17 exceeds the supported limit (16)"
        -- </hexlist-400>
    })

test:do_catchsql_test(
    "hexlist-410",
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(x INT primary key);
        INSERT INTO t1 VALUES(1+0x10000000000000000);
    ]], {
        -- <hexlist-410>
        1, "Hex literal 0x10000000000000000 length 17 exceeds the supported limit (16)"
        -- </hexlist-410>
    })



test:finish_test()
