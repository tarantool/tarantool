local tap = require('tap')

-- Test file to demonstrate incorrect assembling optimization
-- for x86/x64 CPUs.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1376.

local test = tap.test('lj-1376-undefined-mul-test-flag'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local a, b = 0ULL, 0ULL

jit.opt.start('hotloop=1')
for _ = 1, 4 do
  -- Before the patch, the `test` instruction is dropped by
  -- assuming the `imul` instruction before it modifies the flags
  -- register. It results in the following mcode:
  -- | imul r15, rbp
  -- | jnz 0x559415b10060        ->5
  -- Instead of the following:
  -- | imul r15, rbp
  -- | test r15, r15
  -- | jnz 0x559415b10060        ->5
  -- This leads to the incorrect branch being taken.
  if a * b ~= 0ULL then
    test:fail('the impossible branch is taken')
    test:done(true)
  end
  -- XXX: Need to update multiplier to stay in the variant part of
  -- the loop, since invariant contains IR_NOP (former unused
  -- IR_CNEW) between IRs, and the optimization is not applied.
  b = b + 1
end

test:ok(true, 'no dropping of test instruction')
test:done(true)
