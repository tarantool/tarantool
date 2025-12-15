local tap = require('tap')
local test = tap.test('lj-839-concat-recording'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})
test:plan(1)

-- Test file to demonstrate LuaJIT overriding stack slots without
-- restoration during the recording of the concat metamethod.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/839.

-- Setup value with the `__concat` metamethod.
local v1 = setmetatable({}, {
  __concat = function(_, op2) return op2 end,
});

jit.opt.start('hotloop=1')
local result
for _ = 1, 4 do
  -- `savetv` in `rec_cat` handles only up to 5 slots.
  result = v1 .. '' .. '' .. '' .. '' .. 'canary'
end

-- Failure results in a LuaJIT assertion failure.
-- The issue is GC64-specific, yet it is still being tested for
-- other builds.
test:is(result, 'canary', 'correct stack restoration')
test:done(true)
