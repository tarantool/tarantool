-- buffer.lua (internal file)

local ffi = require('ffi')
local utils = require('internal.utils')
local READAHEAD = 16320

ffi.cdef[[
struct slab_cache;
struct slab_cache *
tarantool_lua_slab_cache();

struct ibuf *
cord_ibuf_take(void);

void
cord_ibuf_put(struct ibuf *ibuf);

void
cord_ibuf_drop(struct ibuf *ibuf);

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

/*
 * prbuf iterface; see src/lib/core/prbuf.c for details.
 */

struct prbuf_header;
struct prbuf_record;

struct prbuf {
    struct prbuf_header *header;
};

struct prbuf_entry {
    size_t size;
    char *ptr;
};

struct prbuf_iterator {
    struct prbuf *buf;
    struct prbuf_record *current;
};

void
prbuf_create(struct prbuf *buf, void *mem, size_t size);

int
prbuf_open(struct prbuf *buf, void *mem);

void *
prbuf_prepare(struct prbuf *buf, size_t size);

void
prbuf_commit(struct prbuf *buf);

void
prbuf_iterator_create(struct prbuf *buf, struct prbuf_iterator *iter);

int
prbuf_iterator_next(struct prbuf_iterator *iter, struct prbuf_entry *res);
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

local function ibuf_poison_unallocated(buf)
    utils.poison_memory_region(buf.wpos, buf.epos - buf.wpos);
end

local function ibuf_unpoison_unallocated(buf)
    utils.unpoison_memory_region(buf.wpos, buf.epos - buf.wpos);
end

local function ibuf_reset(buf)
    checkibuf(buf, 'reset')
    buf.rpos = buf.buf
    buf.wpos = buf.buf
    ibuf_poison_unallocated(buf)
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
        ibuf_unpoison_unallocated(buf)
        return buf.wpos
    end
    return ibuf_reserve_slow(buf, size)
end

local function ibuf_alloc(buf, size)
    checkibuf(buf, 'alloc')
    local wpos
    if buf.wpos + size <= buf.epos then
        --
        -- In case of using same buffer we need to unpoison newly
        -- allocated memory after previous ibuf_alloc or poison after
        -- newly allocated memory after previous ibuf_reserve.
        --
        ibuf_unpoison_unallocated(buf)
        wpos = buf.wpos
    else
        wpos = ibuf_reserve_slow(buf, size)
    end
    buf.wpos = buf.wpos + size
    ibuf_poison_unallocated(buf)
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

local prbuf_t = ffi.typeof('struct prbuf')
local prbuf_iterator_t = ffi.typeof('struct prbuf_iterator')
local prbuf_entry_t = ffi.typeof('struct prbuf_entry')

local function prbuf_open(mem)
    if not ffi.istype(ffi.typeof('char *'), mem) then
        errorf('Attempt to prbuf_open() with argument of wrong type, '..
               'expected <char *>')
    end
    local buf = ffi.new(prbuf_t)
    local rc = builtin.prbuf_open(buf, mem)
    if rc ~= 0 then
        errorf("Failed to open prbuf")
    end
    return buf
end

local function prbuf_create(mem, size)
    if not ffi.istype(ffi.typeof('char *'), mem) then
        errorf('Attempt to prbuf_create() with argument of wrong type, '..
               'expected <char *>')
    end
    local buf = ffi.new(prbuf_t)
    builtin.prbuf_create(buf, mem, size)
    return buf
end

local function prbuf_prepare(buf, size)
    if not ffi.istype(prbuf_t, buf) then
        errorf('Attempt to call method without object, use prbuf:prepare()')
    end
    local ptr = builtin.prbuf_prepare(buf, size)
    if ptr == nil then return nil end
    return ffi.cast('char *', ptr)
end


local function prbuf_commit(buf)
    if not ffi.istype(prbuf_t, buf) then
        errorf('Attempt to call method without object, use prbuf:commit()')
    end
    builtin.prbuf_commit(buf)
end

local function prbuf_iterator_create(buf)
    if not ffi.istype(prbuf_t, buf) then
        errorf('Attempt to call method without object, use prbuf:create()')
    end
    local iterator = ffi.new(prbuf_iterator_t)
    builtin.prbuf_iterator_create(buf, iterator)
    return iterator
end

local prbuf_methods = {
    prepare = prbuf_prepare;
    commit = prbuf_commit;
    iterator_create = prbuf_iterator_create;
}

local function prbuf_iterator_next(iterator)
    if not ffi.istype(prbuf_iterator_t, iterator) then
        errorf('Attempt to iterator:next() without object, use iterator:next()')
    end
    local entry = ffi.new(prbuf_entry_t)
    local rc = builtin.prbuf_iterator_next(iterator, entry)
    if rc ~= 0 then return nil end
    return entry
end

local function prbuf_entry_data(entry)
    if not ffi.istype(prbuf_entry_t, entry) then
        errorf('Attempt to entry:data() without object, use entry:data()')
    end
    return ffi.string(entry.ptr, tonumber(entry.size))
end

local prbuf_iterator_methods = {
    next = prbuf_iterator_next;
}

local prbuf_iterator_mt = {
    __index = prbuf_iterator_methods;
}

ffi.metatype(prbuf_iterator_t, prbuf_iterator_mt);

local prbuf_entry_methods = {
    data = prbuf_entry_data;
}

local prbuf_entry_mt = {
    __index = prbuf_entry_methods;
}

ffi.metatype(prbuf_entry_t, prbuf_entry_mt);

local function prbuf_tostring(self)
    return '<prbuf>'
end

local prbuf_mt = {
    __index = prbuf_methods;
    __tostring = prbuf_tostring;
};

ffi.metatype(prbuf_t, prbuf_mt);

--
-- Stash keeps an FFI object for re-usage and helps to ensure the proper
-- ownership. Is supposed to be used in yield-free code when almost always it is
-- possible to put the taken object back.
-- Then cost of the stash is almost the same as ffi.new() for small objects like
-- 'int[1]' even when jitted. Examples:
--
-- * ffi.new('int[1]') is about ~0.4ns, while the stash take() + put() is about
--   ~0.8ns;
--
-- * Much better on objects > 128 bytes in size. ffi.new('struct uri[1]') is
--   ~300ns, while the stash is still ~0.8ns;
--
-- * For structs not allocated as an array is also much better than ffi.new().
--   For instance, ffi.new('struct tt_uuid') is ~300ns, the stash is ~0.8ns.
--   Even though 'struct tt_uuid' is 16 bytes;
--
local function ffi_stash_new(c_type)
    local item = nil

    local function take()
        local res
        -- This line is guaranteed to be GC-safe. GC is not invoked. Because
        -- there are no allocation. So it can be considered 'atomic'.
        res, item = item, nil
        -- The next lines don't need to be atomic and can survive GC. The only
        -- important part was to take the global item and set it to nil.
        if res then
            return res
        end
        return ffi.new(c_type)
    end

    local function put(i)
        -- It is ok to rewrite the existing global item if it was set. Does
        -- not matter. They are all the same.
        item = i
    end

    -- Due to some random reason if the stash returns a table with methods it
    -- works faster than returning them as multiple values. Regardless of how
    -- the methods are used later. Even if the caller will cache take and put
    -- methods anyway.
    return {
        take = take,
        put = put,
    }
end

--
-- Cord buffer is useful for the places, where
--
-- * Want to reuse the already allocated memory which might be stored in the
--   cord buf. Although sometimes the buffer is recycled, so should not rely on
--   being able to reuse it always. When reused, the win is the biggest -
--   becomes about x20 times faster than a new buffer creation (~5ns vs ~100ns);
--
-- * Want to avoid allocation of a new ibuf because it produces a new GC object
--   which is additional load for Lua GC. Although according to benches it is
--   not super expensive;
--
-- * Almost always can put the buffer back manually. Not rely on it being
--   recycled automatically. It is recycled, but still should not rely on that;
--
-- It is important to wrap the C functions, not expose them directly. Because
-- JIT works a bit better when C functions are called as 'ffi.C.func()' than
-- 'func()' with func being cached. The only pros is to cache 'ffi.C' itself.
-- It is quite strange though how having them wrapped into a Lua function is
-- faster than cached directly as C functions.
--
local function cord_ibuf_take()
    return builtin.cord_ibuf_take()
end

local function cord_ibuf_put(buf)
    return builtin.cord_ibuf_put(buf)
end

local function cord_ibuf_drop(buf)
    return builtin.cord_ibuf_drop(buf)
end

local internal = {
    cord_ibuf_take = cord_ibuf_take,
    cord_ibuf_put = cord_ibuf_put,
    cord_ibuf_drop = cord_ibuf_drop,
}

return {
    internal = internal,
    ibuf = ibuf_new;
    prbuf_open = prbuf_open;
    prbuf_create = prbuf_create;
    READAHEAD = READAHEAD;
    ffi_stash_new = ffi_stash_new,
}
