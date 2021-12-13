-- compression.lua (internal file)

local ffi = require("ffi")
local builtin = ffi.C

ffi.cdef[[
enum compression_type {
        COMPRESSION_TYPE_NONE = 0,
        COMPRESSION_TYPE_ZSTD5,
        compression_type_MAX
};
struct tt_compression {
        enum compression_type type;
        uint32_t size;
        char data[0];
};
struct tt_compression *
tt_compression_new(uint32_t size, enum compression_type type);
void
tt_compression_delete(struct tt_compression *ttc);
]]
local compression_t = ffi.typeof('struct tt_compression')

local function compression_new(size, type)
    type = type or builtin.COMPRESSION_TYPE_NONE
    local ttc = builtin.tt_compression_new(size, type)
    ttc = ffi.cast('struct tt_compression &', ttc)
    ttc = ffi.gc(ttc, builtin.tt_compression_delete)
    return ttc
end

local function checkcompression(ttc, method)
    if not ffi.istype(compression_t, ttc) then
        error('Attempt to call method without object, compression:%s()',
              method)
    end
end

local function compression_eq(lhs, rhs)
    if not rhs or lhs.size ~= rhs.size then
        return false
    end
    return builtin.memcmp(lhs.data, rhs.data, lhs.size) == 0
end

local function compression_buffer(ttc)
    checkcompression(ttc)
    return ttc.data
end

local compression_methods = {
    buf = compression_buffer;
};

local compression_mt = {
    __eq = compression_eq;
    __index = compression_methods;
};

ffi.metatype(compression_t, compression_mt);

return setmetatable({
    new         = compression_new;
}, {})
