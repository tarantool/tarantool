do --- Abort trace recording on any hook call.
  local called = false
  local function f() local x = "wrong"; called = true end
  jit.off(f)
  jit.flush()
  debug.sethook(f, "", 5)
  for _ = 1, 1000 do local a, b, c, d, e, f = 1, 2, 3, 4, 5, 6 end
  assert(called)
  -- Check that no trace was generated.
  assert(require("jit.util").traceinfo(1) == nil)
  debug.sethook()
end
