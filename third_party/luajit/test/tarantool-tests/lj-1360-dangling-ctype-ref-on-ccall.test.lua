local tap = require('tap')
local ffi = require('ffi')

-- This test demonstrates LuaJIT's incorrect behaviour when the
-- reallocation of `cts->tab` strikes during the setup arguments
-- for the FFI call.
-- Before the patch, the test failed only under ASAN.
-- See also https://github.com/LuaJIT/LuaJIT/issues/1360.
local test = tap.test('lj-1360-dangling-ctype-ref-on-ccall'):skipcond({
  ['Impossible to predict the value of cts->top'] = _TARANTOOL,
})

test:plan(1)

-- XXX: Declare the structure to increase `cts->top` up to 128
-- slots. The setting up of function's arguments to the next call
-- will reallocate the `cts->tab` during `lj_ccall_ctid_vararg()`
-- and `ccall_set_args()` will use a dangling reference.
ffi.cdef[[
  struct test {
      int a;
      int b;
      int c;
      int d;
      int e;
      int f;
      int g;
      int h;
      int i;
      int j;
      int k;
      int l;
      int m;
      int n;
      int o;
      int p;
      int q;
      int r;
      int s;
      int t;
      int u;
      int v;
      int w;
      int x;
      int y;
      int z;
      int aa;
      int ab;
      int ac;
      int ad;
  };
  // Use an existing function that actually takes no arguments.
  // We can declare it however we want.
  // Need a vararg function for this issue.
  int getppid(...);
]]

local arg_call = ffi.new('struct test')

-- Assertions to check the `cts->top` value.
assert(ffi.typeinfo(127), 'cts->top >= 127')
assert(not ffi.typeinfo(128), 'cts->top < 128')

-- Don't check the result, just check that there is no invalid
-- memory access.
ffi.C.getppid(arg_call)

test:ok(true, 'no heap-use-after-free in C call')

test:done(true)
