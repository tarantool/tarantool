local tap = require('tap')

-- Test file to demonstrate stack-buffer-overflow during the
-- narrowing optimization.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1262.

local test = tap.test('lj-1262-fix-limit-narrow-conv-backprop'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- XXX: Test fails only under ASAN.
-- XXX: The original reproducer was found by fuzzer:
-- https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=70779.
-- It creates a long side trace with a huge amount of ADD IRs,
-- which are recursively used in the `narrow_conv_backprop()` many
-- enough times to catch the stack-buffer-overflow. I can't more
-- simplify the reproducer any (or write it from scratch), so I
-- leave it in that state.

local DEFAULT_NUMBER = 1
local tonumber = tonumber

local always_number = function(val)
  return tonumber(val) or DEFAULT_NUMBER
end

local add = function(v1, v2)
  return always_number(v1) + always_number(v2)
end

jit.opt.start('hotloop=1', 'hotexit=1')

local counter_0 = 0
local counter_1 = 0
local counter_2 = 0
local tmp = add(nil, 'Name')
local Name0 = add(tmp, 'Name')
-- Start a long side trace here.
for _ = 0, 0, 0 do
  if counter_0 > 5 then break end
  counter_0 = counter_0 + 1

  for _ = always_number(false), 1, always_number(Name0) do
    if counter_1 > 5 then break end
    counter_1 = counter_1 + 1

    repeat
      if counter_2 > 5 then break end
      counter_2 = counter_2 + 1

      Name0 = Name0 + Name0 + Name0
      Name0 = add(Name0, nil) + Name0
    until nil
  end
end

test:ok(true, 'no stack-buffer-overflow during narrowing')

test:done(true)
