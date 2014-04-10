-- interctive.lua -- internal file
--
local ffi = require('ffi')
ffi.cdef([[
    char *readline(const char *prompt);
    void tarantool_lua_interactive(char *);
    void free(void *ptr);
]])

function interactive()
    while true do
        line = ffi.C.readline("tarantool> ")
        if line == nil then
            return
        end
        ffi.C.tarantool_lua_interactive(line)
        ffi.C.free(line)
    end
end

jit.off(interactive)
