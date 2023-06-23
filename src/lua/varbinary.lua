local ffi = require('ffi')

ffi.cdef([[
    int memcmp(const char *s1, const char *s2, size_t n);
]])

local memcmp = ffi.C.memcmp

local const_char_ptr_t = ffi.typeof('const char *')
local varbinary_t = ffi.typeof('struct varbinary')

local function is_varbinary(obj)
    return ffi.istype(varbinary_t, obj)
end

local function new_varbinary(data, size)
    if data == nil then
        size = 0
    elseif type(data) == 'string' then
        size = #data
    elseif ffi.istype(varbinary_t, data) then
        size = ffi.sizeof(data)
    elseif not ffi.istype(const_char_ptr_t, data) or type(size) ~= 'number' then
        error('Usage: varbinary.new(str) or varbinary.new(ptr, size)', 2)
    end
    local bin = ffi.new(varbinary_t, size)
    ffi.copy(bin, data, size)
    return bin
end

local function varbinary_len(bin)
    assert(ffi.istype(varbinary_t, bin))
    return ffi.sizeof(bin)
end

local function varbinary_tostring(bin)
    assert(ffi.istype(varbinary_t, bin))
    return ffi.string(bin, ffi.sizeof(bin))
end

local function varbinary_eq(a, b)
    if not (type(a) == 'string' or ffi.istype(varbinary_t, a)) or
            not (type(b) == 'string' or ffi.istype(varbinary_t, b)) then
        return false
    end
    local size_a = #a
    local size_b = #b
    if size_a ~= size_b then
        return false
    end
    local data_a = ffi.cast(const_char_ptr_t, a)
    local data_b = ffi.cast(const_char_ptr_t, b)
    return memcmp(data_a, data_b, size_a) == 0
end

ffi.metatype(varbinary_t, {
    __len = varbinary_len,
    __tostring = varbinary_tostring,
    __eq = varbinary_eq,
})

return {
    is = is_varbinary,
    new = new_varbinary,
}
