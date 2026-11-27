local ffi = require('ffi')

---The `key_def` module has a function for defining the field numbers and
---types of a tuple. The definition is usually used with an index definition
---to extract or compare the index key values.
---
---@class key_def
---@field new fun(parts: key_def.part[]): key_def_object Create a new key_def
---    instance.
---
---There must be at least one part.
---
---The parts table has components which are the same as the `parts` option in
---the index creation options.
---
---**Example:**
---
---```lua
---key_def = require('key_def')
---key_def.new({{type = 'unsigned', fieldno = 1}})
---key_def.new({{type = 'string', collation = 'unicode', field = 2}})
---```
---
---Since version 3.2.0, use the `#` operator to check the key definition
---length (parts count):
---
---```lua
---function is_full_pkey(space, key)
---        return #space.index[0].parts == #key
---end
---```
---@type key_def
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

---@class key_def_object: ffi.cdata*
---A `key_def` object created by `key_def.new()`.
---
---`#key_def` returns the number of parts in the key definition.
---
---@operator len: integer
---@field extract_key fun(self: key_def_object, tuple: table): table Return
---    a tuple containing only the fields of the key_def object.
---
---**Example #1:**
---
---```lua
----- Suppose an item has five fields
----- 1, 99.5, 'X', nil, 99.5
----- and the fields that we care about are
----- #3 (a string) and #1 (an integer).
----- We can define those fields with k = key_def.new
----- and extract the values with k:extract_key.
---
---key_def = require('key_def')
---k = key_def.new({{type = 'string', fieldno = 3},
---                  {type = 'unsigned', fieldno = 1}})
---k:extract_key({1, 99.5, 'X', nil, 99.5})
---```
---
---**Example #2:**
---
---```lua
----- Now suppose the item is a tuple in a space with
----- an index on field #3 plus field #1.
----- We can use key_def.new with the index definition
----- instead of filling it out (Example #1).
----- The result will be the same.
---key_def = require('key_def')
---box.schema.space.create('T')
---i = box.space.T:create_index('I',
---     {parts = {3, 'string', 1, 'unsigned'}})
---box.space.T:insert{1, 99.5, 'X', nil, 99.5}
---k = key_def.new(i.parts)
---k:extract_key(box.space.T:get({'X', 1}))
---```
---
---**Example #3:**
---
---```lua
----- Iterate through the tuples in a secondary non-unique index
----- extracting the tuples' primary-key values, so they could be deleted
----- using a unique index. This code should be a part of a Lua function.
---local key_def_lib = require('key_def')
---local s = box.schema.space.create('test')
---local pk = s:create_index('pk')
---local sk = s:create_index('test', {unique = false, parts = {
---     {2, 'number', path = 'a'}, {2, 'number', path = 'b'}}})
---s:insert{1, {a = 1, b = 1}}
---s:insert{2, {a = 1, b = 2}}
---local key_def = key_def_lib.new(pk.parts)
---for _, tuple in sk:pairs({1}) do
---     local key = key_def:extract_key(tuple)
---     pk:delete(key)
---end
---```
---@field validate_key fun(self: key_def_object, key: table) Validate all
---    parts of the specified key match the key definition.
---
---**Example:**
---
---```lua
----- Create a rule: key = {1 ('unsigned'), 2 (string)}
----- Validate key {1001} (only id data type). Returns nothing
----- Validate key {'x'}. ER_KEY_PART_TYPE is raised
----- Validate key ({1000, 2000}). ER_KEY_PART_TYPE is raised
----- Validate key ({1000, 'abc', 'xyz'}). ER_KEY_PART_COUNT is raised
---key_def = require('key_def').new({{fieldno = 1, type = 'unsigned'},
---                                   {fieldno = 2, type = 'string'}})
---key_def:validate_key({1001})
---key_def:validate_key({'x'})
----- error: 'Supplied key type of part 0 does not match index part type:
----- expected unsigned'
---key_def:validate_key({1000, 2000})
----- error: 'Supplied key type of part 1 does not match index part type:
----- expected string'
---key_def:validate_key({1000, 'abc', 'xyz'})
----- error: 'Invalid key part count (expected [0..2], got 3)'
---```
---@field validate_full_key fun(self: key_def_object, key: table) Validate
---    that the input key contains all fields and matches the key definition.
---
---**Example:**
---
---```lua
----- Create a rule: key = {1 ('unsigned'), 2 (string)}
----- Validate key {100, "Testuser"}. Returns nothing
----- Validate key ({100}). ER_EXACT_MATCH is raised
---key_def = require('key_def').new({{fieldno = 1, type = 'unsigned'},
---                                   {fieldno = 2, type = 'string'}})
---key_def:validate_full_key({100, "Testuser"})
---key_def:validate_full_key({100})
----- error: 'Invalid key part count in an exact match (expected 2, got 1)'
---```
---@field validate_tuple fun(self: key_def_object, tuple: table) Validate
---    that the tuple matches the rules of the key definition.
---
---**Example:**
---
---```lua
----- Create a rule: tuple = {id (number), name (string), age (number)}
----- Validate tuple {1001, "Testuser", 28}. Returns nothing
---key_def = require('key_def').new({
---     {fieldno = 1, type = 'number'},
---     {fieldno = 2, type = 'string'},
---     {fieldno = 3, type = 'number'}})
---key_def:validate_tuple({1001, "Testuser", 28})
---```
---@field compare fun(self: key_def_object, tuple_a: table,
---    tuple_b: table): integer Compare the key fields of `tuple_a` with the
---    key fields of `tuple_b`.
---
---**Example:**
---
---```lua
----- This will return 0
---key_def = require('key_def')
---k = key_def.new({{type = 'string', fieldno = 3,
---                   collation = 'unicode_ci'},
---                  {type = 'unsigned', fieldno = 1}})
---k:compare({1, 99.5, 'X', nil, 99.5}, {1, 99.5, 'x', nil, 99.5})
---```
---@field compare_with_key fun(self: key_def_object, tuple: table,
---    key: table): integer Compare the key fields of `tuple` with all the
---    fields of `key`.
---
---**Example:**
---
---```lua
----- Returns 0
---key_def = require('key_def')
---k = key_def.new({{type = 'string', fieldno = 3,
---                   collation = 'unicode_ci'},
---                  {type = 'unsigned', fieldno = 1}})
---k:compare_with_key({1, 99.5, 'X', nil, 99.5}, {'x', 1})
---```
---@field compare_keys fun(self: key_def_object, key_a: table,
---    key_b: table): integer Compare two keys against each other according
---    to the key definition.
---
---**Example:**
---
---```lua
----- Create a rule: key = {1 ('unsigned'), 2 (string)}
----- Validate keys ({1000, 'x'}, {1000, 'y'}). Returns -1
----- Validate keys ({1000, 'x'}, {1000, 'x'}). Returns 0
----- Validate keys ({1000, 'x'}, {1000}). Returns 0
----- Validate keys ({2000, 'x'}, {1000, 'x'}). Returns 1
---key_def = require('key_def').new({{fieldno = 1, type = 'unsigned'},
---                                   {fieldno = 2, type = 'string'}})
---key_def:compare_keys({1000, 'x'}, {1000, 'y'})
---key_def:compare_keys({1000, 'x'}, {1000, 'x'})
---key_def:compare_keys({1000, 'x'}, {1000})
---key_def:compare_keys({2000, 'x'}, {1000, 'x'})
---```
---@field merge fun(self: key_def_object,
---    other: key_def_object): key_def_object Combine the key definition with
---    `other` and return a new key definition.
---
---**Example:**
---
---```lua
----- Returns a key definition with fieldno = 3 and fieldno = 1.
---key_def = require('key_def')
---k = key_def.new({{type = 'string', fieldno = 3}})
---k2 = key_def.new({{type = 'unsigned', fieldno = 1},
---                   {type = 'string', fieldno = 3}})
---k:merge(k2)
---```
---@field totable fun(self: key_def_object): key_def.part[] Return a table
---    containing the fields of the key_def object.
---
---**Example:**
---
---```lua
----- Returns a table with type = 'string', fieldno = 3
---key_def = require('key_def')
---k = key_def.new({{type = 'string', fieldno = 3}})
---k:totable()
---```

---@class key_def.part
---A part of a key definition: the field number and type, and other
---optional attributes, same as the `parts` option in the index creation
---options.
---
---Every part must contain `type` and one of `fieldno`/`field`. Other
---components are optional.
---
---@field fieldno? integer The field number. Required unless `field` is
---    used.
---@field field? integer The field number, an alternative to `fieldno`.
---    Required unless `fieldno` is used.
---@field type string The field type. Required.
---@field scale? integer The scale for fixed-point decimal types. Required
---    for `decimal32`, `decimal64`, `decimal128` and `decimal256`.
---@field sort_order? string The sort order. Optional.
---@field collation? string The collation name. Optional.
---@field is_nullable? boolean Whether the field is nullable. Optional.
---@field exclude_null? boolean Whether null values are excluded. Optional.
---@field path? string The JSON path to the field. Optional.

return key_def
