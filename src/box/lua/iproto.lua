local ffi = require('ffi')
local utils = require('internal.utils')

ffi.cdef[[
    bool
    cord_is_main(void);
]]

-- Makes a function callable over the IPROTO protocol by the given name.
box.iproto.export = function(func_name, func)
    utils.check_param(func_name, 'function name', 'string', 2)
    utils.check_param(func, 'function object', 'function', 2)
    if box.internal.func_registry[func_name] ~= nil then
        box.error(box.error.FUNCTION_EXISTS, func_name, 2)
    end
    box.internal.func_registry[func_name] = func
end

-- box.iproto.override is available in the main thread only
if ffi.C.cord_is_main() then

-- Sets IPROTO request handler callback (second argument) for the given request
-- type (first argument, number).
-- Passing nil as the callback resets the corresponding request handler.
box.iproto.override = function(request_type, callback)
    local trigger = require('trigger')

    if request_type == nil then
        box.error(box.error.PROC_LUA,
                  'Usage: box.iproto.override(request_type, callback)', 2)
    end
    if type(request_type) ~= 'number' and type(request_type) ~= 'string' then
        box.error(box.error.PROC_LUA,
                  ("bad argument #1 to 'override' (number or string " ..
                   "expected, got %s)"):format(type(request_type)), 2)
    end
    if callback ~= nil and type(callback) ~= 'function' then
        box.error(box.error.PROC_LUA,
                  ("bad argument #2 to 'override' (function expected, " ..
                   "got %s)"):format(type(callback)), 2)
    end

    local event_name
    if type(request_type) == 'number' then
        event_name = ('box.iproto.override[%d]'):format(request_type)
    elseif type(request_type) == 'string' then
        event_name = 'box.iproto.override.' .. (request_type):lower()
    end
    local trigger_name = 'box.iproto.override.lua.user_handler'

    trigger.del(event_name, trigger_name)

    if callback ~= nil then
        trigger.set(event_name, trigger_name, callback)
    end
end

end -- cord_is_main
