local function jit_opt_is_on(needed)
  for _, opt in ipairs({jit.status()}) do
    if opt == needed then
      return true
    end
  end
  return false
end

do --- TBAR for HSTORE.
  local t = {[0]={}}
  for i = 1, 1e5 do t[i] = {t[i - 1]} end
  for i = 1, 1e5 do assert(t[i][1] == t[i - 1]) end
end

do --- OBAR for USTORE.
  local f
  do
    local x = 0
    function f()
      for i = 1, 1e5 do x = {i} end
    end
  end
  f()
end

do --- DSE of USTORE must eliminate OBAR too.
  local need_restore_sink = false
  if jit_opt_is_on("sink") then
    need_restore_sink = true
    jit.opt.start("-sink")
  end

  local f
  do
    local x
    f = function()
      local y = 0
      for _ = 1, 10000 do
        x = {1}
        if y > 0 then end
        x = 1
      end
    end
  end

  collectgarbage()
  local oldstepmul = collectgarbage("setstepmul", 1)
  collectgarbage("restart")

  f()

  collectgarbage("setstepmul", oldstepmul)
  if need_restore_sink then
    jit.opt.start("+sink")
  end
end
