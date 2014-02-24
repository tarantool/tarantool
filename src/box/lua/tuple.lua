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
]])

local builtin = ffi.C

-- cfuncs table is set by C part
local methods = {
    ["next"]        = cfuncs.next;
    ["pairs"]       = cfuncs.pairs;
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
    end
})

-- Remove the global variable
cfuncs = nil

-- export tuple_gc  */
box.tuple._gc = tuple_gc;
