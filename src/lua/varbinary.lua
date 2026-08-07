local ffi = require('ffi')

ffi.cdef([[
    int memcmp(const char *s1, const char *s2, size_t n);
]])

local memcmp = ffi.C.memcmp

local const_char_ptr_t = ffi.typeof('const char *')
local varbinary_t = ffi.typeof('struct varbinary')

---*Since 3.0.0*
---
---The `varbinary` module provides functions for operating variable-length
---binary objects in Lua. It provides functions for creating `varbinary`
---objects, checking their type, and also defines basic operators on such
---objects.
---
---**Example:**
---
---```lua
---local varbinary = require('varbinary')
---
----- Create a varbinary object
---local bin = varbinary.new('data')
---local bin_hex = varbinary.new('\xFF\xFE')
---
----- Check whether a value is a varbinary object
---varbinary.is(bin) -- true
---varbinary.is(bin_hex) -- true
---varbinary.is(100) -- false
---varbinary.is('data') -- false
---
----- Check varbinary objects equality
---print(bin == varbinary.new('data')) -- true
---print(bin == 'data') -- true
---print(bin ~= 'data1') -- true
---print(bin_hex ~= '\xFF\xFE') -- false
---
----- Check varbinary objects length
---print(#bin) -- 4
---print(#bin_hex) -- 2
---
----- Print string representation
---print(tostring(bin)) -- data
---```
---
---**Encoding varbinary objects**
---
---`varbinary` objects preserve their binary type when encoded by the
---built-in MsgPack and YAML encoders. See the difference with strings:
---
---* String to MsgPack:
---
---```tarantoolsession
---tarantool> msgpack.encode('\xFF\xFE')
---    ---
---    - "\xA2\xFF\xFE"
---    ...
---```
---
---* `varbinary` to MsgPack:
---
---```tarantoolsession
---tarantool> msgpack.encode(varbinary.new('\xFF\xFE'))
---    ---
---    - "\xC4\x02\xFF\xFE"
---    ...
---```
---
---* String to YAML:
---
---```tarantoolsession
---tarantool> '\xFF\xFE'
---    ---
---    - "\xFF\xFE"
---    ...
---```
---
---* `varbinary` to YAML:
---
---```tarantoolsession
---tarantool> varbinary.new('\xFF\xFE')
---    ---
---    - !!binary //4=
---    ...
---```
---
---JSON does not support the binary type, so varbinary objects are encoded
---as plain strings in JSON:
---
---```tarantoolsession
---tarantool> json.encode('\xFF\xFE')
---    ---
---    - "\"\xFF\xFE\""
---    ...
---
---tarantool> json.encode(varbinary.new('\xFF\xFE'))
---    ---
---    - "\"\xFF\xFE\""
---    ...
---```
---
---**Decoding binary data to varbinary objects**
---
---The built-in decoders also decode binary data fields (fields with the
---`binary` tag in YAML and the `MP_BIN` type in MsgPack) to varbinary
---objects by default:
---
---```tarantoolsession
---tarantool> varbinary.is(msgpack.decode('\xC4\x02\xFF\xFE'))
---    ---
---    - true
---    ...
---
---tarantool> varbinary.is(yaml.decode('!!binary //4='))
---    ---
---    - true
---    ...
---```
---
---**Important:** This behavior is different from what it was before
---Tarantool 3.0. In earlier versions, such fields were decoded to plain
---strings. To return to this behavior, use the compat option
---`binary_data_decoding`.
---
local varbinary = {}

---@class varbinary_object: ffi.cdata*
---
---A `varbinary` object is a variable-length binary object. It equals
---another `varbinary` object or a string if it contains the same data.
---The length of the binary data in bytes is returned by the `#` operator.
---
---**Equality example:**
---
---```lua
---print(bin == varbinary.new('data')) -- true
---print(bin == 'data') -- true
---print(bin ~= 'data1') -- true
---print(bin_hex ~= '\xFF\xFE') -- false
---```
---
---**Length example:**
---
---```lua
---print(#bin) -- 4
---print(#bin_hex) -- 2
---```
---@operator eq(varbinary_object|string): boolean
---@operator len: integer
---@field __tostring fun(
---    self: varbinary_object
---): string Returns the binary data in a plain string.
---
---    Defines the `tostring()` function for varbinary objects.

---Check that the given object is a `varbinary` object.
---
---@param obj any an object to check.
---@return boolean result whether the given object is of `varbinary` type.
function varbinary.is(obj)
    return ffi.istype(varbinary_t, obj)
end

---Create a new `varbinary` object.
---
---With no arguments, creates an empty `varbinary` object. With a string
---argument, creates an object from the string data. With a cdata pointer
---and size, creates an object from that memory region.
---
---@overload fun(): varbinary_object
---@overload fun(data: ffi.cdata*, size: integer): varbinary_object
---@param data string a string object.
---@param size integer object size in bytes, mandatory if `data` is a
---    pointer.
---@return varbinary_object bin a `varbinary` object containing the data.
function varbinary.new(data, size)
    if data == nil then
        size = 0
    elseif type(data) == 'string' then
        size = #data
    elseif ffi.istype(varbinary_t, data) then
        size = ffi.sizeof(data) --[[@as integer]]
    elseif not ffi.istype(const_char_ptr_t, data) or type(size) ~= 'number' then
        error('Usage: varbinary.new(str) or varbinary.new(ptr, size)', 2)
    end
    local bin = ffi.new(varbinary_t, size)
    if size ~= 0 then
        ffi.copy(bin, data, size)
    end
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

return varbinary
