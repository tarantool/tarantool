local ffi = require('ffi')
local key_def = require('key_def')
local key_def_t = ffi.typeof('struct key_def')

local methods = {
    ['extract_key'] = key_def.extract_key,
    ['validate_key'] = key_def.validate_key,
    ['validate_full_key'] = key_def.validate_full_key,
    ['validate_tuple'] = key_def.validate_tuple,
    ['compare'] = key_def.compare,
    ['compare_with_key'] = key_def.compare_with_key,
    ['compare_keys'] = key_def.compare_keys,
    ['merge'] = key_def.merge,
    ['totable'] = key_def.totable,
    ['__serialize'] = key_def.totable,
}

ffi.metatype(key_def_t, {
    __index = function(self, key)
        return methods[key]
    end,
    __tostring = function(self) return "<struct key_def &>" end,
    __len = key_def.part_count,
})
