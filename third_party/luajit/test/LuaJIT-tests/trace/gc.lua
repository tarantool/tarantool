local jutil = require("jit.util")

do --- Collect dead traces.
  jit.flush()
  collectgarbage()
  -- Prevent the creation of side traces.
  jit.off()
  for _ = 1, 100 do
    jit.on()
    loadstring("for _ = 1, 100 do end")()
    jit.off()
  end
  jit.on()
  collectgarbage()
  assert(jutil.traceinfo(1) == nil)
  assert(jutil.traceinfo(2) == nil)
  assert(jutil.traceinfo(3) == nil)
end

do --- Check KGC marking.
  local f
  local function reccb(tr)
    if f == nil then
      collectgarbage()
      local info = jutil.traceinfo(tr)
      jutil.tracek(tr, -info.nk)
      -- Error in lj_ir_kvalue() if KGC not marked.
      -- Only caught with assertions or Valgrind.
    end
  end
  jit.attach(reccb, "record")
  for i = 1, 200 do
    if i % 5 == 0 then
      f = function() end
    elseif f then
      f()
      f = nil
    end
  end
  jit.attach(reccb)
end
