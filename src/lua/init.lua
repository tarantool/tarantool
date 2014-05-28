-- init.lua -- internal file

-- Override pcall to support Tarantool exceptions

local ffi = require('ffi')
ffi.cdef[[
char *
tarantool_error_message(void);
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
