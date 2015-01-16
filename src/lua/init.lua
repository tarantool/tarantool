-- init.lua -- internal file

-- Override pcall to support Tarantool exceptions

local ffi = require('ffi')
ffi.cdef[[
char *
tarantool_error_message(void);
const char *
tarantool_version(void);
double
tarantool_uptime(void);
]]

local pcall_lua = pcall

local function pcall_wrap(status, ...)
    if status == false and ... == 'C++ exception' then
        return false, ffi.string(ffi.C.tarantool_error_message())
    end
    return status, ...
end
pcall = function(fun, ...)
    return pcall_wrap(pcall_lua(fun, ...))
end

dostring = function(s, ...)
    local chunk, message = loadstring(s)
    if chunk == nil then
        error(message, 2)
    end
    return chunk(...)
end

-- This function automatically called by the server for
-- any new admin client.
function motd()
	return "Tarantool " .. ffi.string(ffi.C.tarantool_version()),
		   "Uptime: " .. math.floor(ffi.C.tarantool_uptime())
end
