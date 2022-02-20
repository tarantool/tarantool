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

--- Deepcopy lua table (all levels)
-- Supports __copy metamethod for copying custom tables with metatables
-- @function deepcopy
-- @table         inp  original table
-- @shallow[opt]  sep  flag for shallow copy
-- @returns            table (copy)
local function table_deepcopy(orig)
    return table_deepcopy_internal(orig, nil)
end

--- Copy any table (only top level)
-- Supports __copy metamethod for copying custom tables with metatables
-- @function copy
-- @table         inp  original table
-- @shallow[opt]  sep  flag for shallow copy
-- @returns            table (copy)
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

--- Compare two lua tables
-- Supports __eq metamethod for comparing custom tables with metatables
-- @function equals
-- @return true when the two tables are equal (false otherwise).
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
local table = require('table')
-- require modifies global "table" module and adds "clear" function to it.
-- Lua applications like Cartridge relies on it.
require('table.clear')

table.copy     = table_shallowcopy
table.deepcopy = table_deepcopy
table.equals   = table_equals
