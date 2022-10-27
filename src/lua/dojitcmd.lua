local M = {}

local function runcmdopt(func, opt)
    -- Check if there are any args to
    -- command and prepare them, if present.
    if opt ~= nil and string.sub(opt, 1, 1) ~= "" then
        local args = opt:split(',')
        return pcall(func, unpack(args))
    end
    return pcall(func)
end

local function loadjitmodule(module_name)
    local full_name = 'jit.' .. module_name
    local st, module = pcall(require, full_name)
    if not st then module = {} end

    local entry_point = module['start']
    if entry_point == nil then
        local err = 'unknown luaJIT command or jit.* modules not installed\n'
        io.stderr:write(err)
    end
    return entry_point
end

M.dojitcmd = function(cmd)
    local cmd, opt = unpack(cmd:split('=', 1))
    local func = jit[cmd]
    if type(func) ~= 'function' then
        func = loadjitmodule(cmd)
        if func == nil then
            os.exit(1)
        end
    end
    return runcmdopt(func, opt)
end

return M
