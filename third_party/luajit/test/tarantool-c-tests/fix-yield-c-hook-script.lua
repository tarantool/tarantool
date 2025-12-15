-- Auxiliary script to provide Lua functions to be used in the
-- test <fix-yield-c-hook.test.c>.
local M = {}

-- The function to call, when line hook (calls `lua_yield()`) is
-- set.
M.yield_in_c_hook = function()
  local co = coroutine.create(function()
    -- Just some payload, don't really matter.
    local _ = tostring(1)
  end)
  -- Enter coroutine and yield from the 1st line.
  coroutine.resume(co)
  -- Try to get the PC to return and continue to execute the first
  -- line (it will still yield from the hook).
  coroutine.resume(co)
end

return M
