local tap = require('tap')
-- Test file to demonstrate the incorrect JIT behaviour for HREFK
-- IR compilation on arm64.
-- See also https://github.com/LuaJIT/LuaJIT/issues/357.
local test = tap.test('lj-357-arm64-hrefk'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

jit.opt.start('hotloop=1', 'hotexit=1')

local t = {hrefk = 0}

-- XXX: Need to generate a bunch of side traces (starts a new one
-- when the hmask is changed) to wait, when the register allocator
-- chooses the same register as a base register for offset and
-- destination in LDR instruction.
-- We need 1028 iterations = 1024 iteration for 10 table rehashing
-- (create side traces for invariant and variant loop part) +
-- 3 for trace initialize + 1 to run the bad trace.
local START = 1028
local STOP = 1
for i = START, STOP, -1 do
  t.hrefk = t.hrefk - 1
  t[t.hrefk] = i
end

test:is(t.hrefk, -START)
test:is(t[t.hrefk], STOP)

test:done(true)
