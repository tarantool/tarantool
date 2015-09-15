-- fiber.lua (internal file)

local fiber = require('fiber')
local ffi = require('ffi')
ffi.cdef[[
double
fiber_time(void);
uint64_t
fiber_time64(void);
]]
local C = ffi.C

local function fiber_time()
    return tonumber(C.fiber_time())
end

local function fiber_time64()
    return C.fiber_time64()
end

fiber.time = fiber_time
fiber.time64 = fiber_time64
return fiber
