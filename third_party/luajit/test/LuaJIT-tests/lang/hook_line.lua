-- The test depends on the number of lines in the file.
local lines = {}
local function hook()
  lines[#lines + 1] = debug.getinfo(2).currentline
end

local function dummy()
end -- <-- line 8

do --- Base test: cycles, function calls.
  debug.sethook(hook, "l", 0)
  -- <-- line 12
  local x
  dummy()
  local y = 1
  dummy() dummy()
  local z = 2; local r = true
  while y < 4 do y = y + 1 end
  while z < 4 do
    z = z + 1
  end
  -- <-- line 22
  local v
  debug.sethook(nil, "", 0)

  assert(#lines > 0)
  while lines[1] < 12 do table.remove(lines, 1) end
  while lines[#lines] > 22 do table.remove(lines) end

  local s = table.concat(lines, " ")
  assert(s == "13 14 8 15 16 8 8 17 18 18 18 18 19 20 19 20 19" or
         s == "13 14 8 15 16 8 16 8 17 18 18 18 18 19 20 19 20 19")
end

do --- Not visited the end of the function definition.
  lines = {}
  local function f()
    if true then return end
    local function x() end
  end -- <-- line 40
  debug.sethook(hook, "l", 0)
  f()
  debug.sethook(nil, "", 0)
  for i = 1, #lines do assert(lines[i] ~= 40) end
end
