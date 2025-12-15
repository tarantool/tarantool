local tap = require('tap')

-- Test file to demonstrate LuaJIT's incorrect recording of cdata
-- arithmetic looks like pointer arithmetic, which raises the
-- error.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1224.

local test = tap.test('lj-1224-fix-cdata-arith-ptr'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

-- XXX: This test leads to the assertion failure before the patch
-- only for non-GC64 build.
local function arith_nil_protected()
  local i = 1
  while i < 3 do
    -- Before the patch, `nil` was "cast" to `IR_KPTR(NULL)`
    -- during the recording of the cdata arith metamethod, and the
    -- fold engine tried to add this value to the `-1LL`. This
    -- obviously leads to the value being out-of-range for the GC
    -- pointer.
    local _ = -1LL + nil
    i = i + 1
  end
end

-- The similar test but for the string value instead of nil.
local function arith_str_protected()
  local i = 1
  while i < 3 do
    local _ = 1LL + ''
    i = i + 1
  end
end

local function check_error(subtest, test_f)
  subtest:plan(2)
  -- Reset counters.
  jit.opt.start('hotloop=1')
  local result, errmsg = pcall(test_f)
  subtest:ok(not result, 'correct recording error with bad cdata arithmetic')
  subtest:like(errmsg, 'attempt to perform arithmetic', 'correct error message')
end

test:test('cdata arithmetic with nil',    check_error, arith_nil_protected)
test:test('cdata arithmetic with string', check_error, arith_str_protected)

test:done(true)
