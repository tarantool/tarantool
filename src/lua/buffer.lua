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

void *
lua_static_aligned_alloc(size_t size, size_t alignment);

/**
 * Register is a buffer to use with FFI functions, which usually
 * operate with pointers to scalar values like int, char, size_t,
 * void *. To avoid doing 'ffi.new(<type>[1])' on each such FFI
 * function invocation, a module can use one of attributes of the
 * register union.
 *
 * Naming policy of the attributes is easy to remember:
 * 'a' for array type + type name first letters + 'p' for pointer.
 *
 * For example:
 * - int[1] - <a>rray of <i>nt - ai;
 * - const unsigned char *[1] -
 *       <a>rray of <c>onst <u>nsigned <c>har <p> pointer - acucp.
 */
union c_register {
    size_t as[1];
    void *ap[1];
    int ai[1];
    char ac[1];
    const unsigned char *acucp[1];
    unsigned long aul[1];
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    int64_t i64;
    int64_t ai64[1];
};
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

local function ibuf_tostring(self)
    return '<ibuf>'
end
local ibuf_mt = {
    __gc = ibuf_recycle;
    __index = ibuf_methods;
    __tostring = ibuf_tostring;
};

ffi.metatype(ibuf_t, ibuf_mt);

local function ibuf_new(arg)
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

--
-- NOTE: ffi.new() with inlined size <= 128 works even faster
--       than this allocator. If your size is a constant <= 128 -
--       use ffi.new(). This function is faster than malloc,
--       ffi.new(> 128), and ffi.new() with any size not known in
--       advance like this:
--
--           local size = <any expression>
--           ffi.new('char[?]', size)
--
-- Allocate a chunk of static BSS memory, or use ordinal ffi.new,
-- when too big size.
-- @param type C type - a struct, a basic type, etc. Should be a
--        string: 'int', 'char *', 'struct tuple', etc.
-- @param size Optional argument, number of elements of @a type to
--        allocate. 1 by default.
-- @return Cdata pointer to @a type.
--
local function static_alloc(type, size)
    size = size or 1
    local bsize = size * ffi.sizeof(type)
    local ptr = builtin.lua_static_aligned_alloc(bsize, ffi.alignof(type))
    if ptr ~= nil then
        return ffi.cast(type..' *', ptr)
    end
    return ffi.new(type..'[?]', size)
end

--
-- Sometimes it is wanted to use several temporary registers at
-- once. For example, when a C function takes 2 C pointers. Then
-- one register is not enough - its attributes share memory. Note,
-- registers are not allocated with separate ffi.new calls
-- deliberately. With a single allocation they fit into 1 cache
-- line, and reduce the heap fragmentation.
--
local reg_array = ffi.new('union c_register[?]', 2)

return {
    ibuf = ibuf_new;
    IBUF_SHARED = ffi.C.tarantool_lua_ibuf;
    READAHEAD = READAHEAD;
    static_alloc = static_alloc,
    -- Keep reference.
    reg_array = reg_array,
    reg1 = reg_array[0],
    reg2 = reg_array[1]
}
