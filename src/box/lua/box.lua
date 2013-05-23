box.flags = { BOX_RETURN_TUPLE = 0x01, BOX_ADD = 0x02, BOX_REPLACE = 0x04 }



--
--
--
function box.select_limit(space, index, offset, limit, ...)
    return box.net.self:select_limit(space, index, offset, limit, ...)
end


--
--
--
function box.select(space, index, ...)
    return box.net.self:select(space, index, ...)
end

--
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

--
-- delete can be done only by the primary key, whose
-- index is always 0. It doesn't accept compound keys
--
function box.delete(space, ...)
    return box.net.self:delete(space, ...)
end

-- insert or replace a tuple
function box.replace(space, ...)
    return box.net.self:replace(space, ...)
end

-- insert a tuple (produces an error if the tuple already exists)
function box.insert(space, ...)
    return box.net.self:insert(space, ...)
end

--
function box.update(space, key, format, ...)
    return box.net.self:update(space, key, format, ...)
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
