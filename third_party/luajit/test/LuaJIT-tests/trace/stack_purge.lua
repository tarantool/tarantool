-- Must preserve the modified function slot in the RET snapshot.
local function a()
  local _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_
  local _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_
  local _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_
  return 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
end

local function b()
  return a()
end

local function c()
  for _ = 1, 10 do
    for _ = 1, 50 do b() b() b() end
    collectgarbage()
    local t = {}
    for _ = 1, 50 do t = {t} end
  end
end

do --- Don't purge the function to return from SNAP.
  jit.off(c)
  c()
end
