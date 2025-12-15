local tap = require('tap')
local test = tap.test('or-524-fold-icorrect-behavior'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local ffi = require('ffi')
-- Test file to demonstrate LuaJIT folding machinery incorrect
-- behaviour, details:
--     https://github.com/LuaJIT/LuaJIT/issues/524
--     https://github.com/moonjit/moonjit/issues/37

jit.opt.start(0, "fold", "cse", "fwd", "hotloop=1")

local sq = ffi.cast("uint32_t", 42)

for _ = 1, 3 do
    sq = ffi.cast("uint32_t", sq * sq)
end

test:is(tonumber(sq), math.fmod(math.pow(42, 8), math.pow(2, 32)))

test:done(true)
