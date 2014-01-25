-- box.lua (internal file)

-- Select a range of tuples in a given namespace via a given
-- index. If key is NULL, starts from the beginning, otherwise
-- starts from the key.
--
function box.select_range(sno, ino, limit, ...)
    return box.net.self:select_range(sno, ino, limit, ...)
end

--
-- Select a range of tuples in a given namespace via a given
-- index in reverse order. If key is NULL, starts from the end, otherwise
-- starts from the key.
--
function box.select_reverse_range(sno, ino, limit, ...)
    return box.net.self:select_reverse_range(sno, ino, limit, ...)
end

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
