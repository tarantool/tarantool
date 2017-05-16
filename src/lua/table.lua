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

-- table library extension
local table = require('table')
table.copy     = table_shallowcopy
table.deepcopy = table_deepcopy
