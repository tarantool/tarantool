local tap = require('tap')
local test = tap.test('lj-611-gc64-inherit-frame-slot-orig'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

jit.opt.start('hotloop=1', 'hotexit=1')

-- The test reproduces the bug "GC64: Function missing in snapshot
-- for non-base frame" [1], and is based on the reproducer
-- described in [2].
--
-- luacheck: push no max_comment_line_length
-- [1]: https://github.com/LuaJIT/LuaJIT/issues/611
-- [2]: https://github.com/LuaJIT/LuaJIT/issues/611#issuecomment-679228156
-- luacheck: pop
--
-- Function `outer` is recorded to a trace and calls a built-in
-- function that is not JIT-compilable and therefore triggers
-- exit to the interpreter, and then it resumes tracing just after
-- the call returns - this is trace stitching. Then, within
-- the call, we need the potential for a side trace. Finally,
-- we need that side exit to be taken enough times for the exit
-- to be compiled into a trace. This compilation hits
-- the assertion failure.

local inner
for _ = 1, 3 do
  inner = function(_, i)
    return i < 4
  end
end

-- The function `string.gsub` is not JIT-compilable and triggers
-- a trace exit. For example, `string.gmatch` and `string.match`
-- are suitable as well.
local function outer(i)
  inner(string.gsub('', '', ''), i)
end

for i = 1, 4 do
  outer(i)
end

test:ok(true, 'function is present in the snapshot')
test:done(true)
