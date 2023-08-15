-- tuple.lua (internal file)

local ffi = require('ffi')
local msgpackffi = require('msgpackffi')
local fun = require('fun')
local buffer = require('buffer')
local compat = require('compat')
local utils = require('internal.utils')

local internal = box.internal
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put
local check_param_table = utils.check_param_table

ffi.cdef[[
/** \cond public */

typedef struct tuple box_tuple_t;

int
box_tuple_ref(box_tuple_t *tuple);

void
box_tuple_unref(box_tuple_t *tuple);

uint32_t
box_tuple_field_count(box_tuple_t *tuple);

size_t
box_tuple_bsize(box_tuple_t *tuple);

ssize_t
box_tuple_to_buf(box_tuple_t *tuple, char *buf, size_t size);

const char *
box_tuple_field(box_tuple_t *tuple, uint32_t i);

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
box_tuple_upsert(box_tuple_t *tuple, const char *expr, const char *expr_end);
]]

local builtin = ffi.C

local tuple_t = ffi.typeof('box_tuple_t')
local const_tuple_ref_t = ffi.typeof('box_tuple_t&')

local NEW_OPTION_TYPES = {
    format = function(format)
        if type(format) ~= 'table' and not box.tuple.format.is(format) then
            return false, "table, box.tuple.format"
        end
        return true
    end
}

local new_tuple = function(...)
    if compat.box_tuple_new_vararg:is_old() then
        return internal.tuple.new{...}
    end
    local tuple, options = ...
    if type(tuple) ~= 'table' and not box.tuple.is(tuple) then
        tuple = {tuple}
    end
    check_param_table(options, NEW_OPTION_TYPES)
    if options == nil then
        return internal.tuple.new(tuple)
    end
    local format
    if box.tuple.format.is(options.format) then
        format = options.format
    else
        format = box.tuple.format.new(options.format)
    end
    return internal.tuple.new(tuple, format)
end

local is_tuple = function(tuple)
    return tuple ~= nil and type(tuple) == 'cdata' and ffi.istype(const_tuple_ref_t, tuple)
end

local encode_fix = msgpackffi.internal.encode_fix
local encode_array = msgpackffi.internal.encode_array
local encode_r = msgpackffi.internal.encode_r

local tuple_encode = function(tmpbuf, obj)
    if obj == nil then
        encode_fix(tmpbuf, 0x90, 0)  -- empty array
    elseif is_tuple(obj) then
        encode_r(tmpbuf, obj, 1)
    elseif type(obj) == "table" then
        local obj_size = #obj
        if obj_size == 0 and not rawequal(next(obj), nil) then
            -- dictionary cannot represent a tuple
            return box.error(box.error.TUPLE_NOT_ARRAY)
        end
        encode_array(tmpbuf, obj_size)
        for i = 1, obj_size, 1 do
            encode_r(tmpbuf, obj[i], 1)
        end
    else
        encode_fix(tmpbuf, 0x90, 1)  -- array of one element
        encode_r(tmpbuf, obj, 1)
    end
    return tmpbuf.rpos, tmpbuf.wpos
end

local tuple_gc = function(tuple)
    builtin.box_tuple_unref(tuple)
end

local tuple_bless = function(tuple)
    -- overflow checked by tuple_bless() in C
    builtin.box_tuple_ref(tuple)
    -- must never fail:
    -- XXX: If we use tail call (instead of creating a new frame
    -- for a call just use the top one) here, then JIT tries to
    -- compile return from `ffi.gc()` to the frame below. This
    -- aborts the trace recording with the error "NYI: return to
    -- lower frame". So avoid tail call and use additional stack
    -- slots (for the local variable and the frame).
    local tuple_ref = ffi.gc(ffi.cast(const_tuple_ref_t, tuple), tuple_gc)
    return tuple_ref
end

local tuple_check = function(tuple, usage)
    if not is_tuple(tuple) then
        error('Usage: ' .. usage)
    end
end

local tuple_iterator_t = ffi.typeof('box_tuple_iterator_t')
local tuple_iterator_ref_t = ffi.typeof('box_tuple_iterator_t &')

local function tuple_iterator(tuple)
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
    tuple_check(tuple, "tuple:next(tuple[, pos])")
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
    tuple_check(tuple, "tuple:pairs(tuple[, pos])")
    local it = tuple_iterator(tuple)
    return fun.wrap(it, tuple, pos)
end

local function tuple_totable(tuple, i, j)
    tuple_check(tuple, "tuple:totable([from[, to]])");
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
    tuple_check(tuple, "tuple:find([offset, ]val)");
    if val == nil then
        val = offset
        offset = 0
    end
    local r = tuple:pairs(offset):index(val)
    return r ~= nil and offset + r or nil
end

local function tuple_findall(tuple, offset, val)
    tuple_check(tuple, "tuple:findall([offset, ]val)");
    if val == nil then
        val = offset
        offset = 0
    end
    return tuple:pairs(offset):indexes(val)
        :map(function(i) return offset + i end)
        :totable()
end

local function tuple_update(tuple, expr)
    tuple_check(tuple, "tuple:update({ { op, field, arg}+ })");
    if type(expr) ~= 'table' then
        error("Usage: tuple:update({ { op, field, arg}+ })")
    end
    local ibuf = cord_ibuf_take()
    local pexpr, pexpr_end = tuple_encode(ibuf, expr)
    local tuple = builtin.box_tuple_update(tuple, pexpr, pexpr_end)
    cord_ibuf_put(ibuf)
    if tuple == nil then
        return box.error()
    end
    return tuple_bless(tuple)
end

local function tuple_upsert(tuple, expr)
    tuple_check(tuple, "tuple:upsert({ { op, field, arg}+ })");
    if type(expr) ~= 'table' then
        error("Usage: tuple:upsert({ { op, field, arg}+ })")
    end
    local ibuf = cord_ibuf_take()
    local pexpr, pexpr_end = tuple_encode(ibuf, expr)
    local tuple = builtin.box_tuple_upsert(tuple, pexpr, pexpr_end)
    cord_ibuf_put(ibuf)
    if tuple == nil then
        return box.error()
    end
    return tuple_bless(tuple)
end

-- Set encode hooks for msgpackffi
local function tuple_to_msgpack(buf, tuple)
    assert(ffi.istype(tuple_t, tuple))
    local bsize = builtin.box_tuple_bsize(tuple)
    buf:reserve(bsize)
    builtin.box_tuple_to_buf(tuple, buf.wpos, bsize)
    buf.wpos = buf.wpos + bsize
end

local function tuple_bsize(tuple)
    tuple_check(tuple, "tuple:bsize()");
    return tonumber(builtin.box_tuple_bsize(tuple))
end

msgpackffi.on_encode(const_tuple_ref_t, tuple_to_msgpack)

local function tuple_field_by_path(tuple, path)
    tuple_check(tuple, "tuple['field_name']");
    return internal.tuple.tuple_field_by_path(tuple, path)
end

local methods = {
    ["next"]        = tuple_next;
    ["ipairs"]      = tuple_ipairs;
    ["pairs"]       = tuple_ipairs; -- just alias for ipairs()
    ["slice"]       = internal.tuple.slice;
    ["transform"]   = internal.tuple.transform;
    ["find"]        = tuple_find;
    ["findall"]     = tuple_findall;
    ["unpack"]      = tuple_unpack;
    ["totable"]     = tuple_totable;
    ["update"]      = tuple_update;
    ["upsert"]      = tuple_upsert;
    ["bsize"]       = tuple_bsize;
    ["tomap"]       = internal.tuple.tuple_to_map;
}

-- Aliases for tuple:methods().
for k, v in pairs(methods) do
    box.tuple[k] = v
end

methods["__serialize"] = tuple_totable -- encode hook for msgpack/yaml/json

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
    __tostring = internal.tuple.tostring;
    __index = function(tuple, key)
        if type(key) == "number" then
            return tuple_field(tuple, key)
        elseif type(key) == "string" then
            -- Try to get a field by JSON path. If it was not
            -- found (rc ~= 0) then return a method from the
            -- vtable. If a collision occurred, then fields have
            -- higher priority. For example, if a tuple T has a
            -- field with name 'bsize', then T.bsize returns
            -- field value, not tuple_bsize function. To access
            -- hidden methods use
            -- 'box.tuple.<method_name>(T, [args...])'.
            local res = tuple_field_by_path(tuple, key)
            if res ~= nil then
                return res
            end
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
    __tostring = function(self) return "<tuple iterator>" end;
})

-- Free methods, which are not needed anymore.
internal.tuple.slice = nil
internal.tuple.transform = nil
internal.tuple.tuple_to_map = nil
internal.tuple.tostring = nil

-- internal api for box.select and iterators
internal.tuple.bless = tuple_bless
internal.tuple.encode = tuple_encode

-- Public API, additional to implemented in C.

-- new() needs a wrapper in Lua, because format normalization needs to be done
-- in Lua.
box.tuple.new = new_tuple

-- is() is implemented in Lua, because then it is
-- easy to be JITed.
box.tuple.is = is_tuple
