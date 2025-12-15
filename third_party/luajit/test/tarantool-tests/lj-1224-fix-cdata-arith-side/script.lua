local function test_record_protected()
  -- Start the root trace here.
  for _ = 1, 3 do end
  -- An exit from the root trace triggered the side trace
  -- recording.
  local _ = 1LL + ''
end

jit.opt.start('hotloop=1', 'hotexit=1')

local status, errmsg = pcall(test_record_protected)

assert(not status, 'recoding correctly handling the error')
assert(errmsg:match('attempt to perform arithmetic'), 'correct error message')

print('OK')
