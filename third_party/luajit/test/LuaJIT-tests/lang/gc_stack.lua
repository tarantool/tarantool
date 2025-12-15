do --- Marking sparse stack.
  local t = setmetatable({}, { __index = function(t, k)
    k = k - 1
    if k == 0 then
      collectgarbage() -- Mark stack, including holes.
      return 0
    else
      return t[k] -- Leaves holes in each frame.
    end
    do
      -- Ensure bigger frame size.
      local a, b, c, d, e, f, g, h, i, j, k, l, m, n
    end
  end})
  local x = t[50]
end
