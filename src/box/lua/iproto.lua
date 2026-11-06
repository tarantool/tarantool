local fiber = require('fiber')
local utils = require('internal.utils')

local may_register_funcs = true

--
-- Makes a flat table by concatenating table keys with nested table keys using
-- a dot as a separator.
--
local function flatten_func_table(arg, level)
    level = level or 3
    local ret = {}
    for k, v in pairs(arg) do
        utils.check_param(k, 'function name', 'string', level)
        if utils.is_callable(v) then
            ret[k] = v
        elseif type(v) == 'table' then
            v = flatten_func_table(v, level + 1)
            for k1, v1 in pairs(v) do
                ret[k .. '.' .. k1] = v1
            end
        end
    end
    return ret
end

--
-- Makes a function callable over the IPROTO protocol by the given name.
--
-- The function takes either a function name and a function or a table
-- {[name] = function}, where nested table keys are concatenated using a dot
-- as a separator, which works as if the table was merged into the global
-- Lua namespace _G.
--
box.iproto.export = function(name_or_table, func_or_nil)
    local func_table
    if type(name_or_table) == 'table' then
        func_table = flatten_func_table(name_or_table)
    elseif name_or_table ~= nil and func_or_nil ~= nil then
        utils.check_param(name_or_table, 'function name', 'string', 2)
        utils.check_param(func_or_nil, 'function object', 'function', 2)
        func_table = {[name_or_table] = func_or_nil}
    else
        box.error(box.error.ILLEGAL_PARAMS,
                  'Usage: box.iproto.export(name, func) or '..
                  'box.iproto.export({[name] = func})', 2)
    end
    for name in pairs(func_table) do
        if box.internal.func_registry[name] ~= nil then
            box.error(box.error.FUNCTION_EXISTS, name, 2)
        end
    end
    for name, func in pairs(func_table) do
        box.internal.func_registry[name] = func
        if may_register_funcs then
            box.iproto.internal.register_func(name)
        end
    end
end

--
-- Cancels the effect of box.iproto.export().
--
-- Similarly to box.iproto.export(), the function takes either the name of
-- an exported function or a table {[name] = function}.
--
box.iproto.unexport = function(name_or_table)
    local func_table
    if type(name_or_table) == 'table' then
        func_table = flatten_func_table(name_or_table)
    elseif name_or_table ~= nil then
        utils.check_param(name_or_table, 'function name', 'string', 2)
        func_table = {[name_or_table] = box.NULL}
    else
        box.error(box.error.ILLEGAL_PARAMS,
                  'Usage: box.iproto.unexport(name) or '..
                  'box.iproto.unexport({[name] = func})', 2)
    end
    for name in pairs(func_table) do
        if box.internal.func_registry[name] == nil then
            box.error(box.error.NO_SUCH_FUNCTION, name, 2)
        end
    end
    for name in pairs(func_table) do
        box.internal.func_registry[name] = nil
        if may_register_funcs then
            box.iproto.internal.unregister_func(name)
        end
    end
end

if fiber._internal.cord_is_main then

--
-- box.iproto.export() may be called in the main thread before the IPROTO
-- threads are started. In this case exported functions will be registered
-- in IPROTO after box.cfg() is done.
--
may_register_funcs = false

box.iproto.internal.register_funcs_after_box_cfg = function()
    for func_name in pairs(box.internal.func_registry) do
        box.iproto.internal.register_func(func_name)
    end
    may_register_funcs = true
end

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
