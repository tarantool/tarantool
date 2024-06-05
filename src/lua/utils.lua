local ffi = require('ffi')
local tarantool = require('tarantool')

local utils = {}

-- Same as type(), but returns 'number' if 'param' is
-- of type 'cdata' and represents a 64-bit integer.
local function param_type(param)
    local t = type(param)
    if t == 'cdata' and tonumber64(param) ~= nil then
        t = 'number'
    end
    return t
end

--[[
 @brief Common function to check type parameter (of function)
 Calls box.error(box.error.ILLEGAL_PARAMS, ) on error
 @example: check_param(user, 'user', 'string')
--]]
function utils.check_param(param, name, should_be_type, level)
    if param_type(param) ~= should_be_type then
        box.error(box.error.ILLEGAL_PARAMS,
                  name .. " should be a " .. should_be_type,
                  level and level + 1)
    end
end

--[[
 @brief Common function to check table with parameters (like options)
 @param table - table with parameters
 @param template - table with expected types of expected parameters
  type could be comma separated string with lua types (number, string etc),
  or 'any' if any type is allowed. Instead of this string, function, which will
  be used to check if the parameter is correct, can be used too. It should
  accept option as an argument and return either true or false + expected_type.
 The function checks following:
 1)that parameters table is a table (or nil)
 2)all keys in parameters are present in template
 3)type of every parameter fits (one of) types described in template
 The functions calls box.error(box.error.ILLEGAL_PARAMS, ..) on error
 @example
 check_param_table(options, { user = 'string',
                              port = 'string, number',
                              data = 'any',
                              addr = function(opt)
                                if not ffi.istype(addr_t, buf) then
                                    return false, "struct addr"
                                end
                                return true
                              end} )
--]]
function utils.check_param_table(table, template, level)
    if table == nil then
        return
    end
    if type(table) ~= 'table' then
        box.error(box.error.ILLEGAL_PARAMS,
                  "options should be a table", level and level + 1)
    end
    for k,v in pairs(table) do
        if template[k] == nil then
            box.error(box.error.ILLEGAL_PARAMS,
                      "unexpected option '" .. k .. "'", level and level + 1)
        elseif type(template[k]) == 'function' then
            local res, expected_type = template[k](v, level and level + 1)
            if not res then
                box.error(box.error.ILLEGAL_PARAMS,
                          "options parameter '" .. k ..
                          "' should be of type " .. expected_type,
                          level and level + 1)
            end
        elseif template[k] == 'any' then -- luacheck: ignore
            -- any type is ok
        elseif (string.find(template[k], ',') == nil) then
            -- one type
            if param_type(v) ~= template[k] then
                box.error(box.error.ILLEGAL_PARAMS,
                          "options parameter '" .. k ..
                          "' should be of type " .. template[k],
                          level and level + 1)
            end
        else
            local good_types = string.gsub(template[k], ' ', '')
            local haystack = ',' .. good_types .. ','
            local needle = ',' .. param_type(v) .. ','
            if (string.find(haystack, needle) == nil) then
                box.error(box.error.ILLEGAL_PARAMS,
                          "options parameter '" .. k ..
                          "' should be one of types: " .. template[k],
                          level and level + 1)
            end
        end
    end
end

--[[
 Adds to a table key-value pairs from defaults table
  that is not present in original table.
 Returns updated table.
 If nil is passed instead of table, it's treated as empty table {}
 For example update_param_table({ type = 'hash', temporary = true },
                                { type = 'tree', unique = true })
  will return table { type = 'hash', temporary = true, unique = true }
--]]
function utils.update_param_table(table, defaults)
    local new_table = {}
    if defaults ~= nil then
        for k,v in pairs(defaults) do
            new_table[k] = v
        end
    end
    if table ~= nil then
        for k,v in pairs(table) do
            new_table[k] = v
        end
    end
    return new_table
end

ffi.cdef[[
void
__asan_poison_memory_region(void const volatile *addr, size_t size);
void
__asan_unpoison_memory_region(void const volatile *addr, size_t size);
int
__asan_address_is_poisoned(void const volatile *addr);
]]

if tarantool.build.asan then
    utils.poison_memory_region = function(start, size)
        ffi.C.__asan_poison_memory_region(start, size)
    end
    utils.unpoison_memory_region = function(start, size)
        ffi.C.__asan_unpoison_memory_region(start, size)
    end
    utils.memory_region_is_poisoned = function(start, size)
        for i = 0, size - 1 do
            if ffi.C.__asan_address_is_poisoned(start + i) == 0 then
                return false
            end
        end
        return true
    end
else
    utils.poison_memory_region = function() end
    utils.unpoison_memory_region = function() end
    utils.memory_region_is_poisoned = function() return false end
end

--[[ Throw an error, if box is unconfigured. --]]
function utils.box_check_configured(level)
    if type(box.cfg) == 'function' then
        box.error(box.error.UNCONFIGURED, level and level + 1)
    end
end

-- Checks if value() is valid expression.
function utils.is_callable(value)
    if type(value) == 'function' then
        return true
    end
    local mt = debug.getmetatable(value)
    if mt ~= nil and type(mt.__call) == 'function' then
        return true
    end
    return false
end

--
-- Return args as table with 'n' set to args number.
--
function utils.table_pack(...)
    return {n = select('#', ...), ...}
end

--
-- Call fn with given variable number of arguments. In case of error
-- if error is box.error then update trace frame to the given level.
-- If level is nil then trace frame will not be updated.
--
function utils.call_at(level, fn, ...)
    local res = utils.table_pack(pcall(fn, ...))
    if not res[1] then
        local err = res[2]
        if box.error.is(err) then
            box.error(err, level and level + 1)
        else
            -- Keep original trace and append trace of level + 1 as
            -- in case of box.error.
            error(tostring(err), level and level + 1)
        end
    end
    return unpack(res, 2, res.n)
end

return utils
