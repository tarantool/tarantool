local ctest = require("libctest")

local lightud = ctest.lightud
local assert = assert

-- x64 lightud tests
do --- light userdata comparison and tostring +x64
  local ud1 = lightud(0x12345678)
  local ud2 = lightud(0x12345678)
  assert(ud1 == ud2)
  assert(tostring(ud1) == "userdata: 0x12345678")
end

do --- unequal light userdata comparison +x64
  local ud1 = lightud(1)
  local ud2 = lightud(2)
  assert(ud1 ~= ud2)
end

do --- big light userdata comparison and tostring +x64
  local ud1 = lightud(2^47-1)
  local ud2 = lightud(2^47-1)
  assert(ud1 == ud2)
  assert(tostring(ud1) == "userdata: 0x7fffffffffff")
end

do --- unequal light userdata comparison JIT +x64
  local ud1 = lightud(0x12345678+123*2^32)
  local ud2 = lightud(0x12345678+456*2^32)
  for i=1,100 do assert(ud1 ~= ud2) end
end

do --- 47 bits light userdata +x64
  assert(tostring(lightud(0x5abc*2^32 + 0xdef01234)) == "userdata: 0x5abcdef01234")
  -- Now more than 47 bits are available.
  assert(pcall(lightud, 2^47) == true)
  assert(pcall(lightud, 2^64-2048) == true)
end

do --- metatable check
  assert(getmetatable(lightud(1)) == nil)
end

do --- lightuserdata SLOAD value and HREF key
  local ud = lightud(12345)
  local t = {[ud] = 42}
  for i=1,100 do
    assert(t[ud] == 42)
  end
end

do --- lightuserdata NEWREF key
  local ud = lightud(12345)
  for i=1,100 do
    local t = {[ud] = 42}
    assert(t[ud] == 42)
  end
end

do --- lightuserdata ASTORE/HSTORE value
  local ud = lightud(12345)
  local t = {}
  for i=1,100 do
    t[i] = ud
  end
  assert(t[100] == ud)
end

do --- lightuserdata sync to stack
  local ud = lightud(12345)
  local x = nil
  for j=1,20 do
    for i=1,50 do
      x = ud
    end
    assert(x == ud)
  end
end

do --- lightuserdata vs. number type check
  local t = {}
  for i=1,200 do t[i] = i end
  t[180] = lightud(12345)
  local x = 0
  assert(not pcall(function(t)
    for i=1,200 do x = x + t[i] end
  end, t))
  assert(x == 16110)
end
