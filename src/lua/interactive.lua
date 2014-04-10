-- interctive.lua -- internal file
--
local ffi = require('ffi')
ffi.cdef([[
    void tarantool_lua_interactive();
    void free(void *ptr);
]])

function interactive()
        ffi.C.tarantool_lua_interactive()
end

jit.off(interactive)
