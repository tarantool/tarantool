local tap = require('tap')
local test = tap.test('fix-ff-select-recording'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

jit.opt.start('hotloop=1')

-- XXX: simplify `jit.dump()` output.
local select = select

local recording = false

-- `start` is the constant on trace, see below.
local function varg_frame(start, ...)
  select(start, ...)
end

local LJ_MAX_JSLOTS = 250

local function varg_frame_wp()
  -- XXX: Need some constant negative value as the first argument
  -- of `select()` when recording the trace.
  -- Also, it should be huge enough to be greater than
  -- `J->maxslot`. The value on the first iteration is ignored.
  -- This will fail under ASAN due to a heap buffer overflow.
  varg_frame(recording and -(LJ_MAX_JSLOTS + 1) or 1)
end

jit.opt.start('hotloop=1')

-- Make the function hot.
varg_frame_wp()

-- Try to record `select()` with a negative first argument.
recording = true
local res, err = pcall(varg_frame_wp)

test:ok(not res, 'correct status')
test:like(err, "bad argument #1 to 'select' %(index out of range%)",
          'correct error message')

test:done(true)
