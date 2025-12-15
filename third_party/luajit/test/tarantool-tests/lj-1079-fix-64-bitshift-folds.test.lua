local tap = require('tap')

-- Test file to demonstrate LuaJIT misbehaviour on folding
-- for bitshift operations.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/1079.

local test = tap.test('lj-1079-fix-64-bitshift-folds'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local bit = require('bit')

test:plan(4)

-- Generic function for `bit.ror()`, `bit.rol()`.
local function bitop_rotation(bitop_func)
  local r = {}
  for i = 1, 4 do
    -- (i & k1) o k2 ==> (i o k2) & (k1 o k2)
    -- XXX: Don't use named constants here to match folding rules.
    -- `7LL` is just some mask, that doesn't change the `i` value.
    -- `32` is used for the half bit-width rotation.
    local int64 = bit.band(i, 7LL)
    r[i] = tonumber(bitop_func(int64, 32))
  end
  return r
end

-- Similar function for `bit.rshift()`.
local function bitop_rshift_signed()
  local r = {}
  for i = 1, 4 do
    -- (i & k1) o k2 ==> (i o k2) & (k1 o k2)
    -- XXX: Use `-i` instead of `i` to prevent other folding due
    -- to IR difference so the IRs don't match fold rule mask.
    -- (-i & 7LL) < 1 << 32 => result == 0.
    local int64 = bit.band(-i, 7LL)
    r[i] = tonumber(bit.rshift(int64, 32))
  end
  return r
end

-- A little bit different example, which leads to the assertion
-- failure due to the incorrect recording.
local function bitop_rshift_huge()
  local r = {}
  for i = 1, 4 do
    -- (i & k1) o k2 ==> (i o k2) & (k1 o k2)
    -- XXX: Need to use cast to the int64_t via `+ 0LL`, see the
    -- documentation [1] for the details.
    -- [1]: https://bitop.luajit.org/semantics.html
    local int64 = bit.band(2 ^ 33 + i, 2 ^ 33 + 0LL)
    r[i] = tonumber(bit.rshift(int64, 32))
  end
  return r
end

local function test_64bitness(subtest, payload_func, bitop_func)
  subtest:plan(1)

  jit.off()
  jit.flush()
  local results_joff = payload_func(bitop_func)
  jit.on()
  -- Reset hotcounters.
  jit.opt.start('hotloop=1')
  local results_jon = payload_func(bitop_func)
  subtest:is_deeply(results_jon, results_joff,
                    'same results for VM and JIT for ' .. subtest.name)
end

test:test('rshift signed', test_64bitness, bitop_rshift_signed)
test:test('rshift huge',   test_64bitness, bitop_rshift_huge)
test:test('rol',           test_64bitness, bitop_rotation, bit.rol)
test:test('ror',           test_64bitness, bitop_rotation, bit.ror)

test:done(true)
