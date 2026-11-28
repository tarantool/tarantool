-- SPDX-License-Identifier: BSD-2-Clause

local ffi = require('ffi')
local builtin = ffi.C

ffi.cdef([[
int memcmp(const char *s1, const char *s2, size_t n);
int embedding_f32_from_double(double value, float *out);
float embedding_f32le_load(const void *data, uint32_t index);
void embedding_f32le_store(void *data, uint32_t index, float value);
int mp_validate_embedding(const char *data, uint32_t len);
uint32_t mp_sizeof_embedding(uint32_t dimension);
char *mp_encode_embedding(char *data, const char *coordinates,
                          uint32_t dimension);
]])

local embedding_t = ffi.typeof('struct embedding')

local function is(value)
    return ffi.istype(embedding_t, value)
end

local function check(value)
    if not is(value) or
       builtin.mp_validate_embedding(value.data, ffi.sizeof(value)) ~= 0 then
        error('expected an embedding of finite float32 coordinates', 3)
    end
end

local function new(array)
    if type(array) ~= 'table' then
        error('Usage: embedding.new(array)', 2)
    end
    local dimension = #array
    if dimension < 1 or dimension > 4096 then
        error('embedding dimension must be in [1, 4096]', 2)
    end
    for key in next, array do
        if type(key) ~= 'number' or key % 1 ~= 0 or
           key < 1 or key > dimension then
            error('embedding must be a dense numeric array', 2)
        end
    end
    local value = ffi.new(embedding_t, dimension * 4)
    local coordinate = ffi.new('float[1]')
    for i = 1, dimension do
        local input = rawget(array, i)
        if type(input) ~= 'number' or
           builtin.embedding_f32_from_double(input, coordinate) ~= 0 then
            error('embedding coordinates must be finite float32', 2)
        end
        builtin.embedding_f32le_store(value.data, i - 1, coordinate[0])
    end
    return value
end

local function totable(value)
    check(value)
    local array = {}
    for i = 1, ffi.sizeof(value) / 4 do
        array[i] = tonumber(builtin.embedding_f32le_load(value.data, i - 1))
    end
    return array
end

ffi.metatype(embedding_t, {
    __len = function(value) return ffi.sizeof(value) / 4 end,
    __serialize = totable,
    __eq = function(a, b)
        return is(a) and is(b) and ffi.sizeof(a) == ffi.sizeof(b) and
               builtin.memcmp(a.data, b.data, ffi.sizeof(a)) == 0
    end,
    __tostring = function(value)
        return 'embedding([' .. table.concat(totable(value), ', ') .. '])'
    end,
})

return {new = new, is = is, totable = totable}
