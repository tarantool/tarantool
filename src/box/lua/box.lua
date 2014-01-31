-- box.lua (internal file)

function box.dostring(s, ...)
    local chunk, message = loadstring(s)
    if chunk == nil then
        error(message, 2)
    end
    return chunk(...)
end

-- User can redefine the hook
function box.on_reload_configuration()
end

require("bit")

-- vim: set et ts=4 sts
