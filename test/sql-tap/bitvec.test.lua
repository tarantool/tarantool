#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(28)

--!./tcltestrunner.lua
-- 2008 February 18
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
-- Unit testing of the Bitvec object.
--
-- $Id: bitvec.test,v 1.4 2009/04/01 23:49:04 drh Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- The built-in test logic must be operational in order for
-- this test to work.

local ffi = require("ffi")
ffi.cdef[[
int sqlite3BitvecBuiltinTest(int sz, int *aOp);int printf(const char *fmt, ...);
]]

-- Test that sqlite3BitvecBuiltinTest correctly reports errors
-- that are deliberately introduced.
--
test:do_test(
    "bitvec-1.0.1",
    function()
        local arg = ffi.new("int[20]", {5, 1, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400, arg )
     end, 1)

test:do_test(
    "bitvec-1.0.2",
    function()
        local arg = ffi.new("int[20]", {5, 1, 234, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400, arg )
    end, 234)

-- Run test cases that set every bit in vectors of various sizes.
-- for larger cases, this should cycle the bit vector representation
-- from hashing into subbitmaps.  The subbitmaps should start as
-- hashes then change to either subbitmaps or linear maps, depending
-- on their size.
--
test:do_test(
    "bitvec-1.1",
    function()
        local arg = ffi.new("int[20]", {1, 400, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400, arg )
    end, 0)

test:do_test(
    "bitvec-1.2",
    function()
        local arg = ffi.new("int[20]", {1, 4000, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(4000, arg )
    end, 0)

test:do_test(
    "bitvec-1.3",
    function()
        local arg = ffi.new("int[20]", {1, 40000, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(40000, arg )
    end, 0)

test:do_test(
    "bitvec-1.4",
    function()
        local arg = ffi.new("int[20]", {1, 400000, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400000, arg )
    end, 0)

-- By specifying a larger increments, we spread the load around.
--
test:do_test(
    "bitvec-1.5",
    function()
        local arg = ffi.new("int[20]", {1, 400, 1, 7, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400, arg )
    end, 0)

test:do_test(
    "bitvec-1.6",
    function()
        local arg = ffi.new("int[20]", {1, 4000, 1, 7, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(4000, arg )
    end, 0)

test:do_test(
    "bitvec-1.7",
    function()
        local arg = ffi.new("int[20]", {1, 40000, 1, 7, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(40000, arg )
    end, 0)

test:do_test(
    "bitvec-1.8",
    function()
        local arg = ffi.new("int[20]", {1, 400000, 1, 7, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400000, arg )
    end, 0)

-- First fill up the bitmap with ones,  then go through and
-- clear all the bits.  This will stress the clearing mechanism.
--
test:do_test(
    "bitvec-1.9",
    function()
        local arg = ffi.new("int[20]", {1, 400, 1, 1, 2, 400, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400, arg )
    end, 0)

test:do_test(
    "bitvec-1.10",
    function()
        local arg = ffi.new("int[20]", {1, 4000, 1, 1, 2, 4000, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(4000, arg )
    end, 0)

test:do_test(
    "bitvec-1.11",
    function()
        local arg = ffi.new("int[20]", {1, 40000, 1, 1, 2, 40000, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(40000, arg )
    end, 0)

test:do_test(
    "bitvec-1.12",
    function()
        local arg = ffi.new("int[20]", {1, 400000, 1, 1, 2, 400000, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400000, arg )
    end, 0)

test:do_test(
    "bitvec-1.13",
    function()
        local arg = ffi.new("int[20]", {1, 400, 1, 1, 2, 400, 1, 7, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400, arg )
    end, 0)

test:do_test(
    "bitvec-1.15",
    function()
        local arg = ffi.new("int[20]", {1, 4000, 1, 1, 2, 4000, 1, 7, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(4000, arg )
    end, 0)

test:do_test(
    "bitvec-1.16",
    function()
        local arg = ffi.new("int[20]", {1, 40000, 1, 1, 2, 40000, 1, 77, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(40000, arg )
    end, 0)

test:do_test(
    "bitvec-1.17",
    function()
        local arg = ffi.new("int[20]", {1, 400000, 1, 1, 2, 400000, 1, 777, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400000, arg )
    end, 0)

test:do_test(
    "bitvec-1.18",
    function()
        local arg = ffi.new("int[20]", {1, 5000, 100000, 1, 2, 400000, 1, 37, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400000, arg )
    end, 0)

-- Attempt to induce hash collisions.  
--
-- ["unset","-nocomplain","start"]
-- ["unset","-nocomplain","incr"]
for start = 1, 8, 1 do
    for incr = 124, 125, 1 do
        test:do_test(
            string.format("bitvec-1.20.%s.%s", start, incr),
            function()
                local prog = { 1, 60, start, incr, 2, 5000, 1, 1, 0 }
                local arg = ffi.new("int[20]", prog)
                return ffi.C.sqlite3BitvecBuiltinTest(5000, arg )
            end, 0)

    end
end
test:do_test(
    "bitvec-1.30.big_and_slow",
    function()
        local arg = ffi.new("int[20]", {1, 17000000, 1, 1, 2, 17000000, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(17000000, arg )
    end, 0)

-- Test setting and clearing a random subset of bits.
--
test:do_test(
    "bitvec-2.1",
    function()
        local arg = ffi.new("int[20]", {3, 2000, 4, 2000, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(4000, arg )
    end, 0)

test:do_test(
    "bitvec-2.2",
    function()
        local arg = ffi.new("int[30]", {3, 1000, 4, 1000, 3, 1000, 4, 1000, 3, 1000, 4,
            1000, 3, 1000, 4, 1000, 3, 1000, 4, 1000, 3, 1000, 4, 1000, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(4000, arg )
    end, 0)

test:do_test(
    "bitvec-2.3",
    function()
        local arg = ffi.new("int[20]", {3, 10, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(400000, arg )
    end, 0)

test:do_test(
    "bitvec-2.4",
    function()
        local arg = ffi.new("int[20]", {3, 10, 2, 4000, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(4000, arg )
    end, 0)

test:do_test(
    "bitvec-2.5",
    function()
        local arg = ffi.new("int[20]", {3, 20, 2, 5000, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(5000, arg )
    end, 0)

test:do_test(
    "bitvec-2.6",
    function()
        local arg = ffi.new("int[20]", {3, 60, 2, 50000, 1, 1, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(50000, arg )
    end, 0)

test:do_test(
    "bitvec-2.7",
    function()
        local arg = ffi.new("int[20]", {1, 25, 121, 125, 1, 50, 121, 125, 2, 25, 121, 125, 0})
        return ffi.C.sqlite3BitvecBuiltinTest(5000, arg )
    end, 0)

-- This procedure runs sqlite3BitvecBuiltinTest with argments "n" and
-- "program".  But it also causes a malloc error to occur after the
-- "failcnt"-th malloc.  The result should be "0" if no malloc failure
-- occurs or "-1" if there is a malloc failure.
--
-- MUST_WORK_TEST sqlite3_memdebug_fail func was removed (with test_malloc.c)
if 0>0 then
local function bitvec_malloc_test(label, failcnt, n, program)
--    do_test $label [subst {
--    sqlite3_memdebug_fail $failcnt
--    set x \[sqlite3BitvecBuiltinTest $n [list $program]\]
--    set nFail \[sqlite3_memdebug_fail -1\]
--    if {\$nFail==0} {
--        set ::go 0
--        set x -1
--        }
--        set x
--        }] -1
end

-- Make sure malloc failures are handled sanily.
--
-- ["unset","-nocomplain","n"]
-- ["unset","-nocomplain","go"]
go = 1
X(177, "X!cmd", [=[["save_prng_state"]]=])
for _ in X(0, "X!for", [=[["set n 0","$go","incr n"]]=]) do
    X(180, "X!cmd", [=[["restore_prng_state"]]=])
    bitvec_malloc_test("bitvec-3.1."..n, n, 5000, [[
      3 60 2 5000 1 1 3 60 2 5000 1 1 3 60 2 5000 1 1 0
  ]])
end
go = 1
for _ in X(0, "X!for", [=[["set n 0","$go","incr n"]]=]) do
    X(187, "X!cmd", [=[["restore_prng_state"]]=])
    bitvec_malloc_test("bitvec-3.2."..n, n, 5000, [[
      3 600 2 5000 1 1 3 600 2 5000 1 1 3 600 2 5000 1 1 0
  ]])
end
go = 1
for _ in X(0, "X!for", [=[["set n 1","$go","incr n"]]=]) do
    bitvec_malloc_test("bitvec-3.3."..n, n, 50000, "1 50000 1 1 0")
end
end

test:finish_test()
