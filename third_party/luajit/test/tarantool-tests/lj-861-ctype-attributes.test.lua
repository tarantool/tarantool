local tap = require('tap')

-- Test file to demonstrate LuaJIT incorrect behaviour during
-- parsing and working with ctypes with attributes.
-- See also:
-- * https://github.com/LuaJIT/LuaJIT/issues/861,
-- * https://github.com/LuaJIT/LuaJIT/issues/1005.

local test = tap.test('lj-861-ctype-attributes')
local ffi = require('ffi')

test:plan(6)

local EXPECTED_ALIGN = 4

ffi.cdef([[
typedef struct __attribute__((aligned($))) s_aligned {
  uint8_t a;
} s_aligned;

struct test_parsing_sizeof {
  char a[sizeof(struct s_aligned &)];
};

struct test_parsing_alignof {
  char a[__alignof__(struct s_aligned &)];
};

]], EXPECTED_ALIGN)

local ref_align = ffi.alignof(ffi.typeof('struct s_aligned &'))

test:is(ref_align, EXPECTED_ALIGN, 'the reference alignment is correct')
test:is(ref_align, ffi.alignof(ffi.typeof('struct s_aligned')),
        'the alignment of a reference is the same as for the referenced type')

test:is(ffi.sizeof('struct test_parsing_sizeof'), EXPECTED_ALIGN,
        'correct sizeof during C parsing')
test:is(ffi.sizeof('struct test_parsing_alignof'), EXPECTED_ALIGN,
        'correct alignof during C parsing')

local EXPECTED_TOSTR = '__tostring overloaded'
local ok, obj = pcall(ffi.metatype, 's_aligned', {
  __tostring = function()
    return EXPECTED_TOSTR
  end,
})

test:ok(ok, 'ffi.metatype is called at the structure with attributes')
test:is(tostring(obj()), EXPECTED_TOSTR,
        '__tostring is overloaded for the structure with attributes')

test:done(true)
