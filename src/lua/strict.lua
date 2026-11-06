-- strict.lua
-- checks uses of undeclared global variables
-- All global variables must be 'declared' through a regular assignment
-- (even assigning nil will do) in a main chunk before being used
-- anywhere or assigned to inside a function.
--

---The `strict` module has functions for turning "strict mode" on or off.
---When strict mode is on, an attempt to use an undeclared global variable
---will cause an error. A global variable is considered "undeclared" if it
---has never had a value assigned to it. Often this is an indication of a
---programming error.
---
---By default strict mode is off, unless tarantool was built with the
---`-DCMAKE_BUILD_TYPE=Debug` option.
---
---**Example:**
---
---```lua
---    tarantool> strict = require('strict')
---    ---
---    ...
---    tarantool> strict.on()
---    ---
---    ...
---    tarantool> a = b -- strict mode is on so this will cause an error
---    ---
---    - error: ... variable 'b' is not declared
---    ...
---    tarantool> strict.off()
---    ---
---    ...
---    tarantool> a = b -- strict mode is off so this will not cause an error
---    ---
---    ...
---```
---@class strict
---@field on fun(): nil @Enable strict mode.
---@field off fun(): nil @Disable strict mode.
local declared = {}

local mt = {}

mt.__declared = declared

local getinfo, error, rawset, rawget = debug.getinfo, error, rawset, rawget

local function what ()
  local d = getinfo(3, "S")
  return d and d.what or "C"
end

mt.__newindex = function (t, n, v)
  if not mt.__declared[n] then
    local w = what()
    if w ~= "main" and w ~= "C" then
      error("assign to undeclared variable '"..n.."'", 2)
    end
    mt.__declared[n] = true
  end
  rawset(t, n, v)
end

mt.__index = function (t, n)
  if not mt.__declared[n] and what() ~= "C" then
    error("variable '"..n.."' is not declared", 2)
  end
  return rawget(t, n)
end

---Disable strict mode.
---
---When strict mode is off, an attempt to use an undeclared global variable
---will not cause an error.
---
---**Example:**
---
--- ```lua
--- strict = require('strict')
--- strict.off()
--- ```
---
local function off()
    mt.__declared = {}
    local m = getmetatable(_G)
    if m == nil then
        return
    end
    if m == mt then
        setmetatable(_G, nil)
    else
        m.__newindex = nil
        m.__index = nil
    end
end

---Enable strict mode.
---
---When strict mode is on, an attempt to use an undeclared global variable
---will cause an error.
---
---**Example:**
---
--- ```lua
--- strict = require('strict')
--- strict.on()
--- ```
---
local function on()
    local m = getmetatable(_G)
    if  m == mt then
        return
    end
    if m == nil then
        setmetatable(_G, mt)
    else
        m.__newindex = mt.__newindex
        m.__index = mt.__index
    end
end

on()

---@type strict
return {
    on = on,
    off = off,
}
