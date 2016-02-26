-- buffer.lua (internal file)

local ffi = require('ffi')
local READAHEAD = 16320

ffi.cdef[[
struct slab_cache;
struct slab_cache *
tarantool_lua_slab_cache();
extern struct ibuf *tarantool_lua_ibuf;

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
    size_t start_capacity;
};

void
ibuf_create(struct ibuf *ibuf, struct slab_cache *slabc, size_t start_capacity);

void
ibuf_destroy(struct ibuf *ibuf);

void
ibuf_reinit(struct ibuf *ibuf);

void *
ibuf_reserve_slow(struct ibuf *ibuf, size_t size);
]]

local builtin = ffi.C
local ibuf_t = ffi.typeof('struct ibuf')

local function errorf(s, ...)
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

local function ibuf_used(buf)
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
    local ptr = builtin.ibuf_reserve_slow(buf, size)
    if ptr == nil then
        errorf("Failed to allocate %d bytes in ibuf", size)
    end
    return ffi.cast('char *', ptr)
end

local function ibuf_reserve(buf, size)
    checkibuf(buf, 'reserve')
    if buf.wpos + size <= buf.epos then
        return buf.wpos
    end
    return ibuf_reserve_slow(buf, size)
end

local function ibuf_alloc(buf, size)
    checkibuf(buf, 'alloc')
    local wpos
    if buf.wpos + size <= buf.epos then
        wpos = buf.wpos
    else
        wpos = ibuf_reserve_slow(buf, size)
    end
    buf.wpos = buf.wpos + size
    return wpos
end

local function checksize(buf, size)
    if buf.rpos + size > buf.wpos then
        errorf("Attempt to read out of range bytes: needed=%d size=%d",
            tonumber(size), ibuf_used(buf))
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

local function ibuf_serialize(buf)
    local properties = { rpos = buf.rpos, wpos = buf.wpos }
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

    size = ibuf_used;
    capacity = ibuf_capacity;
    pos = ibuf_pos;
    unused = ibuf_unused;
}

local function ibuf_tostring(ibuf)
    return '<ibuf>'
end
local ibuf_mt = {
    __gc = ibuf_recycle;
    __index = ibuf_methods;
    __tostring = ibuf_tostring;
};

ffi.metatype(ibuf_t, ibuf_mt);

local function ibuf_new(arg, arg2)
    local buf = ffi.new(ibuf_t)
    local slabc = builtin.tarantool_lua_slab_cache()
    builtin.ibuf_create(buf, slabc, READAHEAD)
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
    IBUF_SHARED = ffi.C.tarantool_lua_ibuf;
    READAHEAD = READAHEAD;
}
