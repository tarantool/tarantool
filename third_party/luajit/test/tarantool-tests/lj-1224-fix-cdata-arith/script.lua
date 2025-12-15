local function test_record_protected()
  local i = 1
  while i < 3 do
    -- Use `while` to start compilation of the trace at the first
    -- iteration, before `cts->L` is uninitialized.
    local _ = 1LL + nil
    i = i + 1
  end
end

jit.opt.start('hotloop=1')

local status, errmsg = pcall(test_record_protected)

assert(not status, 'recoding correctly handling the error')
assert(errmsg:match('attempt to perform arithmetic'), 'correct error message')

print('OK')
