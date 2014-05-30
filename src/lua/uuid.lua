-- uuid.lua (internal file)

local ffi = require("ffi")
local builtin = ffi.C

ffi.cdef[[
        /* from <uuid/uuid.h> */
        typedef unsigned char uuid_t[16];
        void uuid_generate(uuid_t out);

        /* from libc */
        int snprintf(char *str, size_t size, const char *format, ...);
]]

local uuid_bin = function()
    local uuid = ffi.new('uuid_t')
    builtin.uuid_generate(uuid)
    return ffi.string(uuid, 16)
end

local uuid_hex = function()
    local uuid = ffi.new('uuid_t')
    builtin.uuid_generate(uuid)
    local uuid_hex = ffi.new('char[33]')
    for i = 0,ffi.sizeof('uuid_t'),1 do
        builtin.snprintf(uuid_hex + i * 2, 3, "%02x",
            ffi.cast('unsigned int',uuid[i]))
    end
    return ffi.string(uuid_hex, 32)
end

return setmetatable({
    bin = uuid_bin,
    hex = uuid_hex
}, {
    __call = uuid_bin
})
