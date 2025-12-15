local tap = require('tap')
local test = tap.test('or-144-gc64-asmref-l'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- luacheck: push no max_comment_line_length
--
-- Test file to demonstrate LuaJIT `IR_LREF` assembling incorrect
-- behaviour.
-- See also:
-- * https://github.com/openresty/lua-resty-core/issues/144.
-- * https://www.freelists.org/post/luajit/Consistent-SEGV-on-x64-with-the-latest-LuaJIT-v21-GC64-mode.
--
-- luacheck: pop

jit.opt.start('hotloop=1')

local global_env
local _
for i = 1, 4 do
  -- Test `IR_LREF` assembling: using `ASMREF_L` (`REF_NIL`).
  global_env = getfenv(0)
  -- Need to reuse the register, to cause emitting of `mov`
  -- instruction (see `ra_left()` in <src/lj_asm.c>).
  _ = tostring(i)
end

test:ok(global_env == getfenv(0), 'IR_LREF assembling correctness')

test:done(true)
