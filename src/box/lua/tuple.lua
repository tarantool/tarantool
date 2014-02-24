-- tuple.lua (internal file)

local ffi = require('ffi')
local yaml = require('yaml')
local msgpackffi = require('msgpackffi')

ffi.cdef([[
struct tuple
{
    uint32_t _version;
    uint16_t _refs;
    uint16_t _format_id;
    uint32_t _bsize;
    char data[0];
} __attribute__((packed));

uint32_t
tuple_arity(const struct tuple *tuple);
const char *
tuple_field(const struct tuple *tuple, uint32_t i);

struct tuple_iterator {
    const struct tuple *tuple;
    const char *pos;
    int fieldno;
};

void
tuple_rewind(struct tuple_iterator *it, const struct tuple *tuple);

const char *
tuple_seek(struct tuple_iterator *it, uint32_t field_no);

const char *
tuple_next(struct tuple_iterator *it);
]])

local builtin = ffi.C

local tuple_iterator_t = ffi.typeof('struct tuple_iterator')

local function tuple_iterator_next(it, tuple, pos)
    if pos == nil then
        pos = 0
    elseif type(pos) ~= "number" then
         error("error: invalid key to 'next'")
    end
    local field
    if it.tuple == tuple and it.fieldno == pos then
        -- Sequential iteration
        field = builtin.tuple_next(it)
    else
        -- Seek
        builtin.tuple_rewind(it, tuple)
        field = builtin.tuple_seek(it, pos);
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
    return it.fieldno, (msgpackffi.decode_unchecked(field))
end;

-- precreated iterator for tuple_next
local next_it = ffi.new(tuple_iterator_t)

-- See http://www.lua.org/manual/5.2/manual.html#pdf-next
local function tuple_next(tuple, pos)
    return tuple_iterator_next(next_it, tuple, pos);
end

-- See http://www.lua.org/manual/5.2/manual.html#pdf-ipairs
local function tuple_ipairs(tuple, pos)
    local it = ffi.new(tuple_iterator_t)
    return it, tuple, pos
end

-- cfuncs table is set by C part

local methods = {
    ["next"]        = tuple_next;
    ["ipairs"]      = tuple_ipairs;
    ["pairs"]       = tuple_ipairs; -- just alias for ipairs()
    ["slice"]       = cfuncs.slice;
    ["transform"]   = cfuncs.transform;
    ["find"]        = cfuncs.find;
    ["findall"]     = cfuncs.findall;
    ["unpack"]      = cfuncs.unpack;
    ["totable"]     = cfuncs.totable;
    ["bsize"]       = function(tuple)
        return tonumber(tuple._bsize)
    end
}

local tuple_gc = cfuncs.__gc;
local const_struct_tuple_ref_t = ffi.typeof('const struct tuple&')
local tuple_field = function(tuple, field_n)
    local field = builtin.tuple_field(tuple, field_n)
    if field == nil then
        return nil
    end
    -- Use () to shrink stack to the first return value
    return (msgpackffi.decode_unchecked(field))
end

ffi.metatype('struct tuple', {
    __gc = tuple_gc;
    __len = function(tuple)
        return builtin.tuple_arity(tuple)
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

-- export tuple_gc  */
box.tuple._gc = tuple_gc;
