local tap = require('tap')

-- luacheck: push no max_comment_line_length
--
-- Test file to demonstrate incorrect recording of `BC_VARG` that
-- is given to the `select()` function. See also:
-- https://www.freelists.org/post/luajit/Possible-issue-during-register-allocation-ra-alloc1.
--
-- luacheck: pop

local test = tap.test('fix-recording-bc-varg-used-in-select'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- XXX: Simplify `jit.dump` output.
local modf = math.modf

local EXPECTED = 'canary'
local function test_func(...)
  local first_varg_item
  for _ = 1, 4 do
    -- `modf()` is used to create a stitching with a meaningful
    -- index value, that equals 1, i.e. refers to the first item
    -- in `...`. The second trace started after stitching does not
    -- contain the stack slot for the first argument of the
    -- `select()`. Before the patch, there is no loading of
    -- this slot for the trace and the 0 value is taken instead.
    -- Hence, this leads to an incorrect recording of the
    -- `BC_VARG` with detected `select()`.
    first_varg_item = select(modf(1, 0), ...)
  end
  return first_varg_item
end

jit.opt.start('hotloop=1')
test:is(test_func(EXPECTED), EXPECTED, 'correct BC_VARG recording')

test:done(true)
