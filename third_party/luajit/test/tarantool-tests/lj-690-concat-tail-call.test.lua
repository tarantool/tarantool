local tap = require('tap')
local test = tap.test('lj-690-concat-tail-call'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- XXX: Test execution results in an `unbalanced stack after
-- hot instruction` assertion fail, only if the LuaJIT is built
-- with assertion support. Otherwise, the behavior is undefined
-- (it usually fails with a bus error instead).

-- The 'setmetatable' tailcall is delayed, but the non-trivial
-- MM_concat continuation is not delayed, and recording fails
-- since there is no metatable with a defined concat for one of
-- the arguments. During the continuation processing, the stack
-- delta is altered and fixed up after the continuation frame
-- recording. Since continuation frame recording fails, the
-- stack delta is not restored, and the stack becomes unbalanced.

local t = {}
t.__concat = function()
  return setmetatable({}, t)
end
local a = setmetatable({}, t)

jit.opt.start('hotloop=1')
for _ = 1, 4 do
  -- XXX: Extra concat is needed for a non-trivial continuation.
  local _ =  '' .. '' .. a
end

test:ok(true, 'stack is balanced')
test:done(true)
