local tap = require('tap')
local ffi = require('ffi')
local test = tap.test('lj-920-fix-dangling-reference-to-ctype'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Impossible to predict the value of cts->top'] = _TARANTOOL,
})

test:plan(1)

-- This test demonstrates LuaJIT's incorrect behaviour when the
-- reallocation of `cts->tab` strikes during the recording of the
-- cdata metamethod arithmetic.
-- Before the patch, the test failed only under ASAN.

-- XXX: Just some C functions to be casted. There is no need to
-- declare their prototypes correctly.
ffi.cdef[[
  int malloc(void);
  int fprintf(void);
  int printf(void);
  int memset(void);
  int memcpy(void);
  int memmove(void);
  int getppid(void);
]]

local cfunc_type = ffi.metatype(ffi.typeof('struct {int a;}'), {
  -- Just some metatable with reloaded arithmetic operator.
  __add = function(o1, _) return o1 end
})
-- Just some cdata with metamethod.
local test_value = cfunc_type(1)

-- XXX: structure to set `cts->top` to 112.
local _ = ffi.new('struct {int a; long b; float c; double d;}', 0)

-- Anchor table to prevent cdata objects from being collected.
local anchor = {}
-- Each call to this function grows `cts->top` by 3.
-- `lj_ctype_new()` and `lj_ctype_intern()` during the parsing of
-- the `CType` declaration in the `ffi.cast()` plus
-- `lj_ctype_intern()` during the conversion to another `CType`.
local function save_new_func(func)
  anchor[#anchor + 1] = ffi.cast('void (*)(void)', func)
end

save_new_func(ffi.C.fprintf) -- `cts->top` = 112
save_new_func(ffi.C.printf)  -- `cts->top` = 115
save_new_func(ffi.C.memset)  -- `cts->top` = 118
save_new_func(ffi.C.memcpy)  -- `cts->top` = 121
save_new_func(ffi.C.malloc)  -- `cts->top` = 124

-- Assertions to check the `cts->top` value and step between
-- calls.
assert(ffi.typeinfo(124), 'cts->top >= 124')
assert(not ffi.typeinfo(125), 'cts->top < 125')

save_new_func(ffi.C.memmove) -- `cts->top` = 127

jit.opt.start('hotloop=1')

-- Just some function to record trace and cause reallocation.
local function recorded_func(func_arg)
  local res = test_value + func_arg
  return res
end
recorded_func(ffi.C.malloc)

assert(ffi.typeinfo(127), 'cts->top >= 127')
assert(not ffi.typeinfo(128), 'cts->top < 128')

-- Last call to grow `cts->top` up to 129, so this causes
-- `cts->tab` reallocation. Need to use different functions as
-- an argument.
recorded_func(ffi.C.getppid)

test:ok(true, 'no heap-use-after-free in recff_cdata_arith')

test:done(true)
