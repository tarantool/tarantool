local tap = require('tap')
local ffi = require('ffi')
local test = tap.test('fix-dangling-reference-to-ctype'):skipcond({
  ['Impossible to predict the value of cts->top'] = _TARANTOOL,
})

test:plan(1)

-- This test demonstrates LuaJIT's incorrect behaviour when the
-- reallocation of `cts->tab` strikes during the conversion of a
-- TValue (cdata function pointer) to a C type.
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

-- XXX: structure to set `cts->top` to 110.
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

save_new_func(ffi.C.malloc)  -- `cts->top` = 110
save_new_func(ffi.C.fprintf) -- `cts->top` = 113
save_new_func(ffi.C.printf)  -- `cts->top` = 116
save_new_func(ffi.C.memset)  -- `cts->top` = 119
save_new_func(ffi.C.memcpy)  -- `cts->top` = 122

-- Assertions to check the `cts->top` value and step between
-- calls.
assert(ffi.typeinfo(122), 'cts->top >= 122')
assert(not ffi.typeinfo(123), 'cts->top < 123')

save_new_func(ffi.C.memmove) -- `cts->top` = 125

assert(ffi.typeinfo(125), 'cts->top >= 125')
assert(not ffi.typeinfo(126), 'cts->top < 126')

-- Last call to grow `cts->top` up to 128, so this causes
-- `cts->tab` reallocation.
save_new_func(ffi.C.getppid) -- `cts->top` = 128

test:ok(true, 'no heap-use-after-free in lj_cconv_ct_tv')

test:done(true)
