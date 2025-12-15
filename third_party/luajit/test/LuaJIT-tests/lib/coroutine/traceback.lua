do --- traceback
  local function badfunc()
    local x = nil
    local y = x.x
  end

  local co = coroutine.create(badfunc)
  assert(coroutine.resume(co) == false)

  local traceback = debug.traceback(co)
  local line = debug.getinfo(badfunc).linedefined

  assert(traceback:match('traceback:.*:' .. line))
end
