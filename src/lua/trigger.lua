local fun = require('fun')
local log = require('log')

local table_clear = require('table.clear')

local function is_callable(arg)
    if arg ~= nil then
        local mt = (type(arg) == 'table' and getmetatable(arg) or nil)
        if type(arg) == 'function' or mt ~= nil and type(mt.__call) == 'function' then
            return true
        end
    end
    return false
end

local trigger_list_mt = {
    __call = function(self, new_trigger, old_trigger)
        -- prepare, check arguments
        local tnewt, toldt = type(new_trigger), type(old_trigger)
        if (type(new_trigger) ~= 'nil' and not is_callable(new_trigger)) then
            error("Bad type for new_trigger. Expected nil or callable, got '%s'",
                  type(new_trigger))
        end
        if (type(old_trigger) ~= 'nil' and not is_callable(old_trigger)) then
            error("Bad type for old_trigger. Expected nil or callable, got '%s'",
                  type(old_trigger))
        end
        -- do something
        if new_trigger == nil and old_trigger == nil then
            -- list all the triggers
            return fun.iter(ipairs(self)):totable()
        elseif new_trigger ~= nil and old_trigger == nil then
            -- append new trigger
            return table.insert(self, new_trigger)
        elseif new_trigger == nil and old_trigger ~= nil then
            -- delete old trigger
            for pos, func in ipairs(self) do
                if old_trigger == func then
                    table.remove(self, pos)
                    return old_trigger
                end
            end
            error(string.format("Trigger '%s' wasn't set for '%s'", old_trigger, self.name))
        else
            -- if both of the arguments are functions, then
            -- we'll replace triggers and return the old one
            for pos, func in ipairs(self) do
                if old_trigger == func then
                    self[pos] = new_trigger
                    return old_trigger
                end
            end
            error(string.format("Trigger '%s' wasn't set for '%s'", old_trigger, self.name))
        end
    end,
    __index = {
        run = function(self, ...)
            for _, func in ipairs(self) do
                local ok, err = pcall(func, ...)
                if not ok then
                    log.info(
                        "Error, while executing '%s' trigger: %s",
                        self.name, tostring(err)
                    )
                end
            end
        end,
    }
}

local function trigger_list_new(name)
    return setmetatable({
        name = name
    }, trigger_list_mt)
end

return {
    new = trigger_list_new
}
