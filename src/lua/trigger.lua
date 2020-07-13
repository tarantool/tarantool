local fun = require('fun')

--
-- Checks that argument is a callable, i.e. a function or a table
-- with __call metamethod.
--
local function is_callable(arg)
    if type(arg) == 'function' then
        return true
    elseif type(arg) == 'table' then
        local mt = getmetatable(arg)
        if mt ~= nil and type(mt.__call) == 'function' then
            return true
        end
    end
    return false
end

local trigger_list_mt = {
    __call = function(self, new_trigger, old_trigger)
        -- prepare, check arguments
        if new_trigger ~= nil and not is_callable(new_trigger) then
            error(string.format("Usage: %s(callable)", self.name))
        end
        if old_trigger ~= nil and not is_callable(old_trigger) then
            error(string.format("Usage: trigger(new_callable, old_callable)",
                  self.name))
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
            error(string.format("%s: trigger is not found", self.name))
        else
            -- if both of the arguments are functions, then
            -- we'll replace triggers and return the old one
            for pos, func in ipairs(self) do
                if old_trigger == func then
                    self[pos] = new_trigger
                    return old_trigger
                end
            end
            error(string.format("%s: trigger is not found", self.name))
        end
    end,
    __index = {
        run = function(self, ...)
            -- ipairs ignores .name
            for _, func in ipairs(self) do
                func(...)
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
