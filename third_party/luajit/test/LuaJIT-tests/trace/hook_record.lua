do --- Recording traces inside the hook.
  jit.flush()
  debug.sethook(function() for _ = 1, 1000 do end end, "", 10)
  for _ = 1, 10 do end
  debug.sethook()
  assert((require("jit.util").traceinfo(1)))
end
