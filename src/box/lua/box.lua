-- box.lua (internal file)

function box.dostring(s, ...)
    local chunk, message = loadstring(s)
    if chunk == nil then
        error(message, 2)
    end
    return chunk(...)
end

require("bit")

-- vim: set et ts=4 sts
