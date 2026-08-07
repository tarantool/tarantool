-- buffer.lua (internal file)

---The buffer module returns a dynamically resizable buffer which is solely
---for optional use by methods of the net.box module or the msgpack module.
---
---Ordinarily the `net.box` methods return a Lua table. If a `buffer` option
---is used, then the `net.box` methods return a raw MsgPack string. This
---saves time on the server, if the client application has its own routine
---for decoding raw MsgPack strings.
---
---The buffer uses four pointers to manage its capacity:
---
---* `buf` -- a pointer to the beginning of the buffer
---* `rpos` -- a pointer to the beginning of the range; available for reading
---  data ("read position")
---* `wpos` -- a pointer to the end of the range; available for reading data,
---  and to the beginning of the range for writing new data ("write position")
---* `epos` -- a pointer to the end of the range; available for writing new
---  data ("end position")
---
---**Module buffer and skip_header**
---
---The `buffer.ibuf()` example shows that, ordinarily, the response from
---net.box includes a header -- 48 (hexadecimal 30) that is the key for
---IPROTO_DATA. But in some situations, for example when passing the buffer
---to a C function that expects a MsgPack byte array without a header, the
---header can be skipped. This is done by specifying `skip_header=true` as
---an option to `conn.space.space-name:select{...}` or
---`conn.space.space-name:insert{...}` or
---`conn.space.space-name:replace{...}` or
---`conn.space.space-name:update{...}` or
---`conn.space.space-name:upsert{...}` or
---`conn.space.space-name:delete{...}`. The default is `skip_header=false`.
---
---Now here is the end of the same example, except that `skip_header=true`
---is used.
---
---```lua
---ibuf = buffer.ibuf()
---
---conn = net_box.connect('usr1:pwd1@localhost:3301')
---conn.space.tester:select({}, {buffer=ibuf, skip_header=true})
---
---msgpack.decode_unchecked(ibuf.rpos)
---```
---
---The result of the final request looks like this:
---
---```tarantoolsession
---tarantool> msgpack.decode_unchecked(ibuf.rpos)
---    ---
---    - [['ABCDE', 12345]]
---    - 'cdata<char *>: 0x7f8fd102803f'
---    ...
---```
---
---Notice that the IPROTO_DATA header (48) is gone.
---
---The result is still inside an array, as is clear from the fact that it is
---shown inside square brackets. It is possible to skip the array header
---too, with `msgpack.decode_array_header()`.
---
local buffer = {}

---@class buffer_object: ffi.cdata*
---@field buf ffi.cdata* A pointer to the beginning of the buffer
---@field rpos ffi.cdata* A pointer to the beginning of the range;
---    available for reading data ("read position")
---@field wpos ffi.cdata* A pointer to the end of the range; available for
---    reading data, and to the beginning of the range for writing new data
---    ("write position")
---@field epos ffi.cdata* A pointer to the end of the range; available for
---    writing new data ("end position")
---@field alloc fun(self: buffer_object, size: integer): ffi.cdata* Allocate
---    `size` bytes for the buffer.
---@field capacity fun(self: buffer_object): integer Return the capacity of
---    the buffer.
---@field checksize fun(self: buffer_object, size: integer): ffi.cdata* Check
---    if `size` bytes are available for reading.
---@field pos fun(self: buffer_object): integer Return the size of the
---    range occupied by data.
---@field read fun(self: buffer_object, size: integer) Read `size` bytes
---    from the buffer.
---@field consume fun(self: buffer_object, size: integer) Consume `size`
---    bytes from the buffer.
---@field recycle fun(self: buffer_object) Clear the memory slots allocated
---by buffer_object.
---
---**Example:**
---
---```tarantoolsession
---tarantool> ibuf:recycle()
---    ---
---    ...
---tarantool> ibuf.buf, ibuf.rpos, ibuf.wpos, ibuf.epos
---    ---
---    - 'cdata<char *>: NULL'
---    - 'cdata<char *>: NULL'
---    - 'cdata<char *>: NULL'
---    - 'cdata<char *>: NULL'
---    ...
---```
---@field reset fun(self: buffer_object) Clear the memory slots used by
---buffer_object. This method allows to keep the buffer but remove data
---from it. It is useful when you want to use the buffer further.
---
---**Example:**
---
---```tarantoolsession
---tarantool> ibuf:reset()
---    ---
---    ...
---tarantool> ibuf.buf, ibuf.rpos, ibuf.wpos, ibuf.epos
---    ---
---    - 'cdata<char *>: 0x010cc28030'
---    - 'cdata<char *>: 0x010cc28030'
---    - 'cdata<char *>: 0x010cc28030'
---    - 'cdata<char *>: 0x010cc2c000'
---    ...
---```
---@field reserve fun(self: buffer_object, size: integer) Reserve memory
---    for the buffer.
---@field size fun(self: buffer_object): integer Return a range available
---    for reading data.
---@field unused fun(self: buffer_object): integer Return a range for
---    writing data.

local ffi = require('ffi')
local asan = require('internal.asan')

---Default initial capacity of buffers created by `buffer.ibuf()`.
---@type integer
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

---Return the capacity of the buffer_object.
---
---@param buf buffer_object
---@return integer capacity `epos - buf`
local function ibuf_capacity(buf)
    checkibuf(buf, 'capacity')
    return tonumber(buf.epos - buf.buf) --[[@as integer]]
end

---Return the size of the range occupied by data.
---
---@param buf buffer_object
---@return integer pos `rpos - buf`
local function ibuf_pos(buf)
    checkibuf(buf, 'pos')
    return tonumber(buf.rpos - buf.buf) --[[@as integer]]
end

---Return a range, available for reading data.
---
---@param buf buffer_object
---@return integer size `wpos - rpos`
local function ibuf_used(buf)
    checkibuf(buf, 'size')
    return tonumber(buf.wpos - buf.rpos) --[[@as integer]]
end

---Return a range for writing data.
---
---@param buf buffer_object
---@return integer unused `epos - wpos`
local function ibuf_unused(buf)
    checkibuf(buf, 'unused')
    return tonumber(buf.epos - buf.wpos) --[[@as integer]]
end

---Clear the memory slots allocated by buffer_object.
---
---@param buf buffer_object
local function ibuf_recycle(buf)
    checkibuf(buf, 'recycle')
    builtin.ibuf_reinit(buf)
end

local function ibuf_poison_unallocated(buf)
    asan.poison_memory_region(buf.wpos, buf.epos - buf.wpos);
end

local function ibuf_unpoison_unallocated(buf)
    asan.unpoison_memory_region(buf.wpos, buf.epos - buf.wpos);
end

---Clear the memory slots used by buffer_object.
---
---This method allows to keep the buffer but remove data from it. It is
---useful when you want to use the buffer further.
---
---@param buf buffer_object
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

---Reserve memory for buffer_object.
---
---Check if there is enough memory to write `size` bytes after `wpos`. If
---not, `epos` shifts until `size` bytes will be available.
---
---@param buf buffer_object
---@param size integer
local function ibuf_reserve(buf, size)
    checkibuf(buf, 'reserve')
    if buf.wpos + size <= buf.epos then
        ibuf_unpoison_unallocated(buf)
        return buf.wpos
    end
    return ibuf_reserve_slow(buf, size)
end

---Allocate size bytes for buffer_object.
---
---@param buf buffer_object
---@param size integer memory in bytes to allocate
---@return ffi.cdata* wpos
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

---Check if size bytes are available for reading in buffer_object.
---
---@param buf buffer_object
---@param size integer memory in bytes to check
---@return ffi.cdata* rpos
local function ibuf_checksize(buf, size)
    checkibuf(buf, 'checksize')
    checksize(buf, size)
    return buf.rpos
end

---Read from buffer.
---
---@param buf buffer_object
---@param size integer Read size bytes from buffer.
local function ibuf_read(buf, size)
    checkibuf(buf, 'read')
    checksize(buf, size)
    local rpos = buf.rpos
    buf.rpos = rpos + size
    return rpos
end

---Consume size bytes from the buffer.
---
---Advances the read position without returning the consumed data.
---
---@param buf buffer_object
---@param size integer the number of bytes to consume.
local function ibuf_consume(buf, size)
    checkibuf(buf, 'consume')
    checksize(buf, size)
    asan.poison_memory_region(buf.rpos, size);
    buf.rpos = buf.rpos + size
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
    consume = ibuf_consume;
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

---Create a new buffer.
---
---**Example:**
---
---In this example we will show that using buffer allows you to keep the
---data in the format that you get from the server. So if you get the data
---only for sending it somewhere else, buffer fastens this a lot.
---
---```lua
---box.cfg{listen = 3301}
---buffer = require('buffer')
---net_box = require('net.box')
---msgpack = require('msgpack')
---
---box.schema.space.create('tester')
---box.space.tester:create_index('primary')
---box.space.tester:insert({1, 'ABCDE', 12345})
---
---box.schema.user.create('usr1', {password = 'pwd1'})
---box.schema.user.grant('usr1', 'read,write,execute', 'space', 'tester')
---
---ibuf = buffer.ibuf()
---
---conn = net_box.connect('usr1:pwd1@localhost:3301')
---conn.space.tester:select({}, {buffer=ibuf})
---
---msgpack.decode_unchecked(ibuf.rpos)
---```
---
---The result of the final request looks like this:
---
---```tarantoolsession
---tarantool> msgpack.decode_unchecked(ibuf.rpos)
---    ---
---    - {48: [['ABCDE', 12345]]}
---    - 'cdata<char *>: 0x7f97ba10c041'
---    ...
---```
---
---Note: before Tarantool 1.7.7, the function to use for this case is
---`msgpack.ibuf_decode(ibuf.rpos)`. Starting with Tarantool 1.7.7,
---`ibuf_decode` is deprecated.
---
---@param arg? integer buffer size in bytes.
---@return buffer_object buf the new buffer.
function buffer.ibuf(arg)
    local buf = ffi.new(ibuf_t)
    local slabc = builtin.tarantool_lua_slab_cache()
    builtin.ibuf_create(buf, slabc, READAHEAD)
    if arg ~= nil then
        if type(arg) ~= 'number' then
            errorf('Usage: ibuf([size])')
        end
        ibuf_reserve(buf, arg)
    end
    return buf
end

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
---Create a new FFI stash for re-usage of objects of the given type.
---
---The stash keeps an FFI object for re-usage and helps to ensure the proper
---ownership. Is supposed to be used in yield-free code when almost always it
---is possible to put the taken object back.
---
---@param c_type string an FFI type name.
---@return { take: fun(): ffi.cdata*, put: fun(i: ffi.cdata*) } stash
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

buffer.internal = internal
buffer.READAHEAD = READAHEAD
buffer.ffi_stash_new = ffi_stash_new

return buffer
