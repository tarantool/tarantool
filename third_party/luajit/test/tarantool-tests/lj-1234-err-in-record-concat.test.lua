local tap = require('tap')

-- Test file to demonstrate the crash during the concat recording
-- if it throws an error.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1234.

local test = tap.test('lj-1234-err-in-record-concat'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

jit.opt.start('hotloop=1')

local __concat = function()
  return ''
end

-- Need to use metamethod call in the concat recording.
-- We may use any object with a metamethod, but let's use a table
-- as the most common one.
local concatable_t = setmetatable({}, {
  __concat = __concat,
})

local function test_concat_p()
  local counter = 0
  while counter < 1 do
    counter = counter + 1
    -- The first result is placed on the Lua stack before the
    -- error is raised. When the error is raised, it is handled by
    -- the trace recorder, but since neither `rec_cat()` nor
    -- `lj_record_ret()` restore the Lua stack (before the patch),
    -- it becomes unbalanced after the instruction recording
    -- attempt.
    local _ = {} .. (concatable_t .. concatable_t)
  end
end

local result, errmsg = pcall(test_concat_p)

test:ok(not result, 'the error is raised')
test:like(errmsg, 'attempt to concatenate a table value', 'correct error')

test:done(true)
