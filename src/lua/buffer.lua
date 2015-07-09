-- buffer.lua (internal file)

local ffi = require('ffi')

ffi.cdef[[
struct slab_cache;
struct slab_cache *
tarantool_lua_slab_cache();

struct ibuf
{
    struct slab_cache *slabc;
    char *buf;
    /** Start of input. */
    char *rpos;
    /** End of useful input */
    char *wpos;
    /** End of ibuf. */
    char *epos;
};

void
ibuf_create(struct ibuf *ibuf, struct slab_cache *slabc);

void
ibuf_destroy(struct ibuf *ibuf);

void
ibuf_reinit(struct ibuf *ibuf);

int
ibuf_reserve_nothrow_slow(struct ibuf *ibuf, size_t size);
]]

local builtin = ffi.C
local ibuf_t = ffi.typeof('struct ibuf')

local function errorf(method, s, ...)
    error(string.format(s, ...))
end

local function checkibuf(buf, method)
    if not ffi.istype(ibuf_t, buf) then
        errorf('Attempt to call method without object, use ibuf:%s()', method)
    end
end

local function ibuf_capacity(buf)
    checkibuf(buf, 'capacity')
    return tonumber(buf.epos - buf.buf)
end

local function ibuf_pos(buf)
    checkibuf(buf, 'pos')
    return tonumber(buf.rpos - buf.buf)
end

local function ibuf_size(buf)
    checkibuf(buf, 'size')
    return tonumber(buf.wpos - buf.rpos)
end

local function ibuf_unused(buf)
    checkibuf(buf, 'unused')
    return tonumber(buf.epos - buf.wpos)
end

local function ibuf_recycle(buf)
    checkibuf(buf, 'recycle')
    builtin.ibuf_reinit(buf)
end

local function ibuf_reset(buf)
    checkibuf(buf, 'reset')
    buf.rpos = buf.buf
    buf.wpos = buf.buf
end

local function ibuf_reserve_slow(buf, size)
    if builtin.ibuf_reserve_nothrow_slow(buf, size) ~= 0 then
        errorf("Failed to allocate %d bytes in ibuf", size)
    end
end

local function ibuf_reserve(buf, size)
    checkibuf(buf, 'reserve')
    if buf.wpos + size <= buf.epos then
        return buf.wpos
    end
    ibuf_reserve_slow(buf, size)
    return buf.wpos
end

local function ibuf_alloc(buf, size)
    checkibuf(buf, 'alloc')
    if buf.wpos + size > buf.epos then
        ibuf_reserve_slow(buf, size)
    end
    local wpos = buf.wpos
    buf.wpos = wpos + size
    return wpos
end

local function checksize(buf, size)
    if ibuf.rpos + size > ibuf.wpos then
        errorf("Attempt to read out of range bytes: needed=%d size=%d",
            tonumber(size), ibuf_size(buf))
    end
end

local function ibuf_checksize(buf, size)
    checkibuf(buf, 'checksize')
    checksize(buf, size)
    return buf.rpos
end

local function ibuf_read(buf, size)
    checkibuf(buf, 'read')
    checksize(buf, size)
    local rpos = buf.rpos
    buf.rpos = rpos + size
    return rpos
end

local ibuf_properties = {
    size = ibuf_size;
    capacity = ibuf_capacity;
    pos = ibuf_pos;
    unused = ibuf_unused;
}

local function ibuf_serialize(buf)
    local properties = { rpos = buf.rpos, wpos = buf.wpos }
    for key, getter in pairs(ibuf_properties) do
        properties[key] = getter(buf)
    end
    return { ibuf = properties }
end

local ibuf_methods = {
    recycle = ibuf_recycle;
    reset = ibuf_reset;

    reserve = ibuf_reserve;
    alloc = ibuf_alloc;

    checksize = ibuf_checksize;
    read = ibuf_read;
    __serialize = ibuf_serialize;
}

local function ibuf_index(buf, key)
    local property = ibuf_properties[key]
    if property ~= nil then
        return property(buf)
    end
    local method = ibuf_methods[key]
    if method ~= nil then
        return method
    end
    return nil
end

local function ibuf_tostring(ibuf)
    return '<ibuf>'
end
local ibuf_mt = {
    __gc = ibuf_recycle;
    __index = ibuf_index;
    __tostring = ibuf_tostring;
};

ffi.metatype(ibuf_t, ibuf_mt);

local function ibuf_new(arg, arg2)
    local buf = ffi.new(ibuf_t)
    local slabc = builtin.tarantool_lua_slab_cache()
    builtin.ibuf_create(buf, slabc)
    if arg == nil then
        return buf
    elseif type(arg) == 'number' then
        ibuf_reserve(buf, arg)
        return buf
    end
    errorf('Usage: ibuf([size])')
end

return {
    ibuf = ibuf_new;
    IBUF_SHARED = ibuf_new();
}
