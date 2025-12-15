local tap = require('tap')

-- Test file to demonstrate LuaJIT's UBSan failures during
-- `INT*_MIN` negation.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/928.

local test = tap.test('lj-928-int-min-negation'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local INT32_MIN = -0x80000000
local INT64_MIN = -0x8000000000000000
local TOBIT_CHAR_MAX = 254

-- XXX: Many tests (`tonumber()`-related) are failing under UBSan
-- with DUALNUM enabled. They are included to avoid regressions in
-- the future if such a build becomes the default.
local ffi = require('ffi')
local LL_T = ffi.typeof(1LL)

test:plan(14)

jit.opt.start('hotloop=1')

-- Temporary variable for the results.
local result

-- <src/lj_vmmath.c>:`lj_vm_modi()`
for _ = 1, 4 do
  -- Use additional variables to avoid folding during parsing.
  -- Operands should be constants on the trace.
  local x = -0x80000000
  local y = -0x80000000
  result = x % y
end
test:is(result, 0, 'no UB during lj_vm_modi')

-- <src/lj_strfmt.c>:`lj_strfmt_wint()`
for _ = 1, 4 do
  -- Operand should be a constant on the trace.
  result = tostring(bit.tobit(0x80000000))
end
test:is(result, '-2147483648', 'no UB during lj_strfmt_wint')

-- <src/lj_strfmt.c>:`lj_strfmt_putfxint()`
test:is(('%d'):format(INT64_MIN), '-9223372036854775808',
        'no UB during lj_strfmt_putfxint')

-- <src/lj_parse.c>:`bcemit_unop()`
local int64_min_cdata = -0x8000000000000000LL
test:ok(true, 'no UB during bcemit_unop')

-- <src/lj_carith.c>:`carith_int64()`
-- Use the additional variable to avoid folding during
-- `bcemit_unop()`.
test:is(-int64_min_cdata, int64_min_cdata, 'no UB during carith_int64')

-- <src/lj_ctype.c>:`lj_ctype_repr_int64()`
-- Use cast to separate the test case from `bcemit_unop()`.
test:is(tostring(LL_T(INT64_MIN)), '-9223372036854775808LL',
        'no UB during lj_ctype_repr_int64')

local TOHEX_EXPECTED = ('0'):rep(TOBIT_CHAR_MAX)
-- <src/lib_bit.c>:`bit_tohex()`
-- The second argument is the number of bytes to be represented.
-- The negative value stands for uppercase.
test:is(bit.tohex(0, INT32_MIN), TOHEX_EXPECTED, 'no UB during bit_tohex')

-- <src/lj_crecord.c>:`recff_bit64_tohex()`
-- The second argument is the number of bytes to be represented.
-- The negative value stands for uppercase.
for _ = 1, 4 do
  -- The second argument should be a constant on the trace.
  result = bit.tohex(0, -0x80000000)
end
test:is(result, TOHEX_EXPECTED, 'no UB during recording bit.tohex')

-- <src/lj_opt_fold.c>:`simplify_intsub_k()`
result = 0
for _ = 1, 4 do
  result = result - 0x8000000000000000LL
end
test:is(result, 0LL, 'no UB during simplify_intsub_k')

-- <src/lj_strscan.c>:`strscan_hex()`
test:is(tonumber('-0x80000000'), INT32_MIN, 'no UB during strscan_hex')

-- <src/lj_strscan.c>:`strscan_bin()`
test:is(tonumber('-0b10000000000000000000000000000000'), INT32_MIN,
        'no UB during strscan_bin')

-- <src/lj_strscan.c>:`lj_strscan_scan()`
test:is(tonumber('-2147483648'), INT32_MIN, 'no UB during strscan_scan')

-- Test for 32bit long, just in case.
-- <src/lib_base.c>:`tonumber()`
test:is(tonumber('-2000000000000000', 4), INT32_MIN,
        'no UB during tonumber, base 4')

-- <src/lj_cparse.c>:`cp_expr_prefix()`
-- According to ISO/IEC 9899:2023 [1]:
-- | Each constant expression shall evaluate to a constant that is
-- | in the range of representable values for its type.
-- It means that since 0x80000000 does not fit in the int32_t
-- range, -0x80000000 does not fit in the int32_t range either.
--
-- In the case when the enumeration has no fixed underlying type,
-- the type of the enum is implementation defined [2][3].
--
-- Hence, we used -INT32_MAX - 1 since both values fit into
-- int32_t, so it can't be ambiguous.
--
-- luacheck: ignore (too long line)
-- [1]: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3096.pdf#subsection.6.2.6
-- [2]: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf#%5B%7B%22num%22%3A232%2C%22gen%22%3A0%7D%2C%7B%22name%22%3A%22Fit%22%7D%5D
-- [3]: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3096.pdf#subsubsection.6.7.2.2
ffi.cdef[[typedef enum {enum_int32_min = -0x7fffffff - 1} enum_t;]]
test:is(ffi.new('enum_t', 'enum_int32_min'), LL_T(INT32_MIN),
        'no UB during cp_expr_prefix')

test:done(true)
