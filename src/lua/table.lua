---The `table` module has everything in the standard Lua table library, and
---some Tarantool extensions.
---
---Write `table` to see the list of functions:
---
---* `clear` (LuaJIT extension = erase all elements)
---* `concat` (concatenate)
---* `copy` (make a copy of an array)
---* `deepcopy` (see below)
---* `foreach`
---* `foreachi`
---* `getn` (get the number of elements in an array)
---* `insert` (insert an element into an array)
---* `maxn` (get the largest index)
---* `move` (move elements between tables)
---* `new` (LuaJIT extension = return a new table with pre-allocated
---  elements)
---* `remove` (remove an element from an array)
---* `sort` (sort the elements of an array)
---
---In this section we describe the additional functions added by Tarantool:
---`copy`, `deepcopy`, and `equals`.
---
---@class table
---The Tarantool extensions of the `table` module.
---
---@field copy fun(input_table: table): table @Copy a table at the top level.
---
---    Supports the `__copy` metamethod for copying custom tables with
---    metatables.
---@field deepcopy fun(input_table: table): table
---@field sort fun(input_table: table,
---    comparison_function?: fun(a: any, b: any): boolean): nil
---@field equals fun(a: table, b: table): boolean @Compare two Lua tables.
---
---    Supports the `__eq` metamethod for comparing custom tables with
---    metatables.

---@type table
local table = require('table')

local function table_deepcopy_internal(orig, cyclic)
    cyclic = cyclic or {}
    local copy = orig
    if type(orig) == 'table' then
        local mt, copy_function = getmetatable(orig), nil
        if mt then copy_function = mt.__copy end
        if copy_function == nil then
            copy = {}
            if cyclic[orig] ~= nil then
                copy = cyclic[orig]
            else
                cyclic[orig] = copy
                for orig_key, orig_value in pairs(orig) do
                    local key = table_deepcopy_internal(orig_key, cyclic)
                    copy[key] = table_deepcopy_internal(orig_value, cyclic)
                end
                if mt ~= nil then setmetatable(copy, mt) end
            end
        else
            copy = copy_function(orig)
        end
    end
    return copy
end

---Return a "deep" copy of the table -- a copy which follows nested
---structures to any depth and does not depend on pointers, it copies the
---contents.
---
---Supports the `__copy` metamethod for copying custom tables with
---metatables.
---
---**Example:**
---
--- ```lua
--- local input_table = {1, {'a', 'b'}}
--- local output_table = table.deepcopy(input_table)
--- ```
---
---@param orig table the table to copy.
---@return table result the copy of the table.
local function table_deepcopy(orig)
    return table_deepcopy_internal(orig, nil)
end

---Copy any table, only the top level.
---
---Supports the `__copy` metamethod for copying custom tables with
---metatables.
---
---@param orig table the table to copy.
---@return table result the copy of the table.
local function table_shallowcopy(orig)
    local copy = orig
    if type(orig) == 'table' then
        local mt, copy_function = getmetatable(orig), nil
        if mt then copy_function = mt.__copy end
        if copy_function == nil then
            copy = {}
            for orig_key, orig_value in pairs(orig) do
                copy[orig_key] = orig_value
            end
            if mt ~= nil then setmetatable(copy, mt) end
        else
            copy = copy_function(orig)
        end
    end
    return copy
end

---Compare two Lua tables.
---
---Supports the `__eq` metamethod for comparing custom tables with
---metatables.
---
---**Example:**
---
--- ```lua
--- table.equals({1, 2}, {1, 2}) -- true
--- ```
---
---@param a table a table to compare.
---@param b table a table to compare with.
---@return boolean result true when the two tables are equal, false
---    otherwise.
local function table_equals(a, b)
    if type(a) ~= 'table' or type(b) ~= 'table' then
        return type(a) == type(b) and a == b
    end
    local mta = getmetatable(a)
    local mtb = getmetatable(b)
    -- Let Lua decide what should happen when at least one of the tables has a
    -- metatable.
    if mta and mta.__eq or mtb and mtb.__eq then
        return a == b
    end
    for k, v in pairs(a) do
        if not table_equals(v, b[k]) then
            return false
        end
    end
    for k, _ in pairs(b) do
        if type(a[k]) == 'nil' then
            return false
        end
    end
    return true
end

-- table library extension
-- require modifies global "table" module and adds functions "clear" and "new".
-- Lua applications like Cartridge rely on it.
require('table.clear')
require('table.new')

table.copy     = table_shallowcopy
table.deepcopy = table_deepcopy
table.equals   = table_equals
