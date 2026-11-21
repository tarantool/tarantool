local ffi = require('ffi')
local fun = require('fun')

---# Builtin `merger` module
---
---The `merger` module takes a stream of tuples and provides access to them
---as tables.
---
---@class merger
---@field new fun(key_def: any, sources: merger_object[],
---    options?: {reverse?: boolean}): merger_object Create a new merger
---    instance from a set of sources. It performs a merge sort: on each
---    step it chooses a source with a minimal or maximal tuple.
---@field new_tuple_source fun(gen: function, param?: any,
---    state?: any): merger_object Create a new merger instance from a
---    tuple source.
---@field new_buffer_source fun(gen: function, param?: any,
---    state?: any): merger_object Create a new merger instance from a
---    buffer source.
---@field new_table_source fun(gen: function, param?: any,
---    state?: any): merger_object Create a new merger instance from a
---    table source.
---@field new_source_frombuffer fun(buf: ffi.cdata*): merger_object Create
---    a source from one buffer.
---@field new_source_fromtable fun(tbl: table): merger_object Create a
---    source from one table.
---@field private internal {select: function, ipairs: function} Internal
---    functions of the merger module.

---@class merger_object: ffi.cdata*
---A merger object, created by the `merger` module functions. It provides
---access to a stream of tuples.
---
---@field select fun(self: merger_object,
---    options?: {buffer?: ffi.cdata*, limit?: integer}): any[] Access the
---    contents of a merger object with the familiar `select` syntax.
---
---**Example with `new_table_source()`:**
---
---```lua
---merger = require('merger')
---k = 0
---function merger_function(param)
---    k = k + 1
---    if param[k] == nil then return nil end
---    return box.NULL, param[k]
---end
---chunks = {}
---chunks[1] = {{100}}
---chunks[2] = {{200}}
---chunks[3] = nil
---s = merger.new_table_source(merger_function, chunks)
---s:select()
---    ---
---    - - [100]
---      - [200]
---    ...
---```
---@field pairs fun(self: merger_object): fun(...): (integer, any) Return
---    a luafun iterator over the tuples of the merger object.
---
---**Example with `new_tuple_source()`:**
---
---```lua
---merger = require('merger')
---box.schema.space.create('s')
---box.space.s:create_index('i')
---box.space.s:insert({100})
---box.space.s:insert({200})
---so = merger.new_tuple_source(box.space.s:pairs())
---so:pairs():totable()
---    ---
---    - - [100]
---      - [200]
---    ...
---```
---
---**Example with two mergers:**
---
---```lua
----- Create the key_def object
---merger = require('merger')
---key_def_lib = require('key_def')
---key_def = key_def_lib.new({{
---    fieldno = 1,
---    type = 'string',
---}})
----- Create the table source
---data = {{'a'}, {'b'}, {'c'}}
---source = merger.new_source_fromtable(data)
---i1 = merger.new(key_def, {source}):pairs()
---i2 = merger.new(key_def, {source}):pairs()
----- t1 will be 'a' (tuple 1 from merger 1)
---t1 = i1:head():totable()
----- t3 will be 'c' (tuple 3 from merger 2)
---t3 = i2:head():totable()
----- t2 will be 'b' (tuple 2 from merger 1)
---t2 = i1:head():totable()
----- i1:is_null() will be true (merger 1 ends)
---i1:is_null()
----- i2:is_null() will be true (merger 2 ends)
---i2:is_null()
---```
---@field ipairs fun(self: merger_object): fun(...): (integer, any) An
---    alias of `pairs`.

---@type merger
local merger = require('merger')

local ibuf_t = ffi.typeof('struct ibuf')
local merge_source_t = ffi.typeof('struct merge_source')

---Create a source from one buffer.
---
---@param buf ffi.cdata* a buffer (cdata of type `struct ibuf`).
---@return merger_object source a new merger source.
merger.new_source_frombuffer = function(buf)
    local func_name = 'merger.new_source_frombuffer'
    if type(buf) ~= 'cdata' or not ffi.istype(ibuf_t, buf) then
        error(('Usage: %s(<cdata<struct ibuf>>)'):format(func_name), 0)
    end

    return merger.new_buffer_source(fun.iter({buf}))
end

---Create a source from one table.
---
---@param tbl table the table to create a source from.
---@return merger_object source a new merger source.
merger.new_source_fromtable = function(tbl)
    local func_name = 'merger.new_source_fromtable'
    if type(tbl) ~= 'table' then
        error(('Usage: %s(<table>)'):format(func_name), 0)
    end

    return merger.new_table_source(fun.iter({tbl}))
end

local methods = {
    ['select'] = merger.internal.select,
    ['pairs']  = merger.internal.ipairs,
    ['ipairs']  = merger.internal.ipairs,
}

ffi.metatype(merge_source_t, {
    __index = function(self, key)
        return methods[key]
    end,
    -- Lua 5.2 compatibility
    __pairs = merger.internal.ipairs,
    __ipairs = merger.internal.ipairs,
})

---@type merger
return merger
