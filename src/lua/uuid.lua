-- uuid.lua (internal file)
(function(box)
    local ffi = require("ffi")
    ffi.cdef[[
        /* from <uuid/uuid.h> */
        typedef unsigned char uuid_t[16];
        void uuid_generate(uuid_t out);

        /* from libc */
        int snprintf(char *str, size_t size, const char *format, ...);
    ]]

    local libuuid = nil
    local builtin = ffi.C
    function check_libs()
        if libuuid then return end
        libuuid = ffi.load('uuid.so.1')
    end
    box.uuid = function()
        check_libs()
        local uuid = ffi.new('uuid_t')
        libuuid.uuid_generate(uuid)
        return ffi.string(uuid, 16)
    end
    box.uuid_hex = function()
        check_libs()
        local uuid = ffi.new('uuid_t')
        libuuid.uuid_generate(uuid)
        local uuid_hex = ffi.new('char[33]')
        for i = 0,ffi.sizeof('uuid_t'),1 do
            builtin.snprintf(uuid_hex + i * 2, 3, "%02x",
                ffi.cast('unsigned int',uuid[i]))
        end
        return ffi.string(uuid_hex, 32)
    end
end)(box)
