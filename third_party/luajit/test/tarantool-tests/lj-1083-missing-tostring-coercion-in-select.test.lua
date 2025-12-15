local tap = require('tap')

-- Test file to demonstrate LuaJIT incorrect recording of
-- `select()` fast function.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1083.

local test = tap.test('lj-1083-missing-tostring-coercion-in-select'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- Simplify the `jit.dump()` output.
local select = select

local function test_select(...)
  local result
  for _ = 1, 4 do
    -- Before the patch, with the missed coercion to string, the
    -- recording of `select()` below leads to the following IR:
    -- | rcx   >  int CONV   "1"   int.num index
    -- Where the operand has string type instead of number type.
    -- This leads to the corresponding mcode:
    -- | cvttsd2si ecx, xmm1
    -- Where xmm1 has an undefined value. Thus leads to the
    -- incorrect result for the call below.
    result = select('1', ...)
  end
  return result
end

jit.opt.start('hotloop=1')

-- XXX: amount of arguments is empirical, see the comment above.
local result = test_select(1, 2, 3, 4)

test:is(result, 1, 'correct select result after recording')

test:done(true)
