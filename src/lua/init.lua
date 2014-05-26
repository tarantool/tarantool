-- init.lua -- internal file

-- Override pcall to support Tarantool exceptions

local ffi = require('ffi')
ffi.cdef[[
char *
tarantool_error_message(void);
]]

local pcall_lua = pcall
pcall = function(fun, ...)
    local status, msg = pcall_lua(fun, ...)
    if status == false and msg == 'C++ exception' then
        return false, ffi.string(ffi.C.tarantool_error_message())
    end
    return status, msg
end
