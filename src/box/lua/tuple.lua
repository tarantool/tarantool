-- tuple.lua (internal file)

local ffi = require('ffi')
local yaml = require('yaml')
local msgpackffi = require('msgpackffi')
local fun = require('fun')
local internal = require('box.internal')

ffi.cdef([[
/** \cond public */
typedef struct tuple_format box_tuple_format_t;

box_tuple_format_t *
box_tuple_format_default(void);

typedef struct tuple box_tuple_t;

box_tuple_t *
box_tuple_new(box_tuple_format_t *format, const char *data, const char *end);

int
box_tuple_ref(box_tuple_t *tuple);

void
box_tuple_unref(box_tuple_t *tuple);

uint32_t
box_tuple_field_count(const box_tuple_t *tuple);

size_t
box_tuple_bsize(const box_tuple_t *tuple);

ssize_t
box_tuple_to_buf(const box_tuple_t *tuple, char *buf, size_t size);

box_tuple_format_t *
box_tuple_format(const box_tuple_t *tuple);

const char *
box_tuple_field(const box_tuple_t *tuple, uint32_t i);

typedef struct tuple_iterator box_tuple_iterator_t;

box_tuple_iterator_t *
box_tuple_iterator(box_tuple_t *tuple);

void
box_tuple_iterator_free(box_tuple_iterator_t *it);

uint32_t
box_tuple_position(box_tuple_iterator_t *it);

void
box_tuple_rewind(box_tuple_iterator_t *it);

const char *
box_tuple_seek(box_tuple_iterator_t *it, uint32_t field_no);

const char *
box_tuple_next(box_tuple_iterator_t *it);

/** \endcond public */

box_tuple_t *
box_tuple_update(box_tuple_t *tuple, const char *expr, const char *expr_end);

box_tuple_t *
box_tuple_upsert(box_tuple_t *tuple, const char *expr,
                 const char *expr_end);
]])

local builtin = ffi.C

local tuple_t = ffi.typeof('box_tuple_t')
local const_tuple_ref_t = ffi.typeof('const box_tuple_t&')

local tuple_gc = function(tuple)
    builtin.box_tuple_unref(tuple)
end

local tuple_bless = function(tuple)
    -- overflow checked by tuple_bless() in C
    builtin.box_tuple_ref(tuple)
    -- must never fail:
    return ffi.gc(ffi.cast(const_tuple_ref_t, tuple), tuple_gc)
end

local tuple_iterator_t = ffi.typeof('box_tuple_iterator_t')
local tuple_iterator_ref_t = ffi.typeof('box_tuple_iterator_t &')

local function tuple_iterator(tuple)
    if tuple == nil then
        error("Invalid tuple for iterator")
    end
    local it = builtin.box_tuple_iterator(tuple)
    if it == nil then
        box.error()
    end
    return ffi.gc(ffi.cast(tuple_iterator_ref_t, it),
        builtin.box_tuple_iterator_free)
end

local function tuple_iterator_next(it, tuple, pos)
    if pos == nil then
        pos = 0
    elseif type(pos) ~= "number" then
         error("error: invalid key to 'next'")
    end
    local curpos = builtin.box_tuple_position(it)
    local field
    if curpos == pos then
        -- Sequential iteration
        field = builtin.box_tuple_next(it)
    else
        -- Seek
        builtin.box_tuple_rewind(it)
        field = builtin.box_tuple_seek(it, pos);
    end
    if field == nil then
        if #tuple == pos then
            -- No more fields, stop iteration
            return nil
        else
            -- Invalid pos
            error("error: invalid key to 'next'")
        end
    end
    -- () used to shrink the return stack to one value
    return pos + 1, (msgpackffi.decode_unchecked(field))
end;

-- See http://www.lua.org/manual/5.2/manual.html#pdf-next
local function tuple_next(tuple, pos)
    if pos == nil then
        pos = 0
    end
    local field = builtin.box_tuple_field(tuple, pos)
    if field == nil then
        return nil
    end
    return pos + 1, (msgpackffi.decode_unchecked(field))
end

-- See http://www.lua.org/manual/5.2/manual.html#pdf-ipairs
local function tuple_ipairs(tuple, pos)
    local it = tuple_iterator(tuple)
    return fun.wrap(it, tuple, pos)
end

local function tuple_totable(tuple, i, j)
    local it = tuple_iterator(tuple)
    builtin.box_tuple_rewind(it)
    local field
    if i ~= nil then
        if i < 1 then
            error('tuple.totable: invalid second argument')
        end
        field = builtin.box_tuple_seek(it, i - 1)
    else
        i = 1
        field = builtin.box_tuple_next(it)
    end
    if j ~= nil then
        if j <= 0 then
            error('tuple.totable: invalid third argument')
        end
    else
        j = 4294967295
    end
    local ret = {}
    while field ~= nil and i <= j do
        local val = msgpackffi.decode_unchecked(field)
        table.insert(ret, val)
        i = i + 1
        field = builtin.box_tuple_next(it)
    end
    return setmetatable(ret, msgpackffi.array_mt)
end

local function tuple_unpack(tuple, i, j)
    return unpack(tuple_totable(tuple, i, j))
end

local function tuple_find(tuple, offset, val)
    if val == nil then
        val = offset
        offset = 0
    end
    local r = tuple:pairs(offset):index(val)
    return r ~= nil and offset + r or nil
end

local function tuple_findall(tuple, offset, val)
    if val == nil then
        val = offset
        offset = 0
    end
    return tuple:pairs(offset):indexes(val)
        :map(function(i) return offset + i end)
        :totable()
end

local function tuple_update(tuple, expr)
    if type(expr) ~= 'table' then
        error("Usage: tuple:update({ { op, field, arg}+ })")
    end
    local pexpr, pexpr_end = msgpackffi.encode_tuple(expr)
    local tuple = builtin.box_tuple_update(tuple, pexpr, pexpr_end)
    if tuple == nil then
        return box.error()
    end
    return tuple_bless(tuple)
end

local function tuple_upsert(tuple, expr)
    if type(expr) ~= 'table' then
        error("Usage: tuple:upsert({ { op, field, arg}+ })")
    end
    local pexpr, pexpr_end = msgpackffi.encode_tuple(expr)
    local tuple = builtin.box_tuple_upsert(tuple, pexpr, pexpr_end)
    if tuple == nil then
        return box.error()
    end
    return tuple_bless(tuple)
end

-- Set encode hooks for msgpackffi
local function tuple_to_msgpack(buf, tuple)
    local bsize = tuple:bsize()
    buf:reserve(bsize)
    builtin.box_tuple_to_buf(tuple, buf.wpos, bsize)
    buf.wpos = buf.wpos + bsize
end

msgpackffi.on_encode(const_tuple_ref_t, tuple_to_msgpack)


-- cfuncs table is set by C part

local methods = {
    ["next"]        = tuple_next;
    ["ipairs"]      = tuple_ipairs;
    ["pairs"]       = tuple_ipairs; -- just alias for ipairs()
    ["slice"]       = cfuncs.slice;
    ["transform"]   = cfuncs.transform;
    ["find"]        = tuple_find;
    ["findall"]     = tuple_findall;
    ["unpack"]      = tuple_unpack;
    ["totable"]     = tuple_totable;
    ["update"]      = tuple_update;
    ["upsert"]      = tuple_upsert;
    ["bsize"]       = function(tuple)
        return tonumber(builtin.box_tuple_bsize(tuple))
    end;
    ["__serialize"] = tuple_totable; -- encode hook for msgpack/yaml/json
}

local tuple_field = function(tuple, field_n)
    local field = builtin.box_tuple_field(tuple, field_n - 1)
    if field == nil then
        return nil
    end
    -- Use () to shrink stack to the first return value
    return (msgpackffi.decode_unchecked(field))
end


ffi.metatype(tuple_t, {
    __len = function(tuple)
        return builtin.box_tuple_field_count(tuple)
    end;
    __tostring = function(tuple)
        -- Unpack tuple, call yaml.encode, remove yaml header and footer
        -- 5 = '---\n\n' (header), -6 = '\n...\n' (footer)
        return yaml.encode(methods.totable(tuple)):sub(5, -6)
    end;
    __index = function(tuple, key)
        if type(key) == "number" then
            return tuple_field(tuple, key)
        end
        return methods[key]
    end;
    __eq = function(tuple_a, tuple_b)
        -- Two tuple are considered equal if they have same memory address
        return ffi.cast('void *', tuple_a) == ffi.cast('void *', tuple_b);
    end;
    __pairs = tuple_ipairs;  -- Lua 5.2 compatibility
    __ipairs = tuple_ipairs; -- Lua 5.2 compatibility
})

ffi.metatype(tuple_iterator_t, {
    __call = tuple_iterator_next;
    __tostring = function(it) return "<tuple iterator>" end;
})

-- Remove the global variable
cfuncs = nil

-- internal api for box.select and iterators
box.tuple.bless = tuple_bless
