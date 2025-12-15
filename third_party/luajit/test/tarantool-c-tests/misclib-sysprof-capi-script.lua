local M = {}

-- luacheck: no global
assert(c_payload, 'c_payload global function should be set via script loader')

local function lua_payload(n)
  if n <= 1 then
    return n
  end
  return lua_payload(n - 1) + lua_payload(n - 2)
end

local function payload()
  local n_iterations = 500000

  local co = coroutine.create(function()
    for i = 1, n_iterations do
      if i % 2 == 0 then
        c_payload(10)
      else
        lua_payload(10)
      end
      coroutine.yield()
    end
  end)

  for _ = 1, n_iterations do
    coroutine.resume(co)
  end
end

M.profile_func_jiton = payload
M.profile_func_jitoff = payload

return M
