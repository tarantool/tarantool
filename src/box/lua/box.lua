-- box.lua (internal file)

--
--
--
function box.select_limit(space, index, offset, limit, ...)
    local key_part_count = select('#', ...)
    return box.process(box.net.box.SELECT,
        box.pack('iiiiV',
            space,
            index,
            offset,
            limit,
            key_part_count, ...))
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
    local key_part_count = select('#', ...)
    return box.process(box.net.box.DELETE,
        box.pack('iV', space, key_part_count, ...))
end

-- insert or replace a tuple
function box.replace(space, ...)
    local field_count = select('#', ...)
    return box.process(box.net.box.REPLACE,
        box.pack('iV', space, field_count, ...))
end

-- insert a tuple (produces an error if the tuple already exists)
function box.insert(space, ...)
    local field_count = select('#', ...)
    return box.process(box.net.box.INSERT,
        box.pack('iV', space, field_count, ...))
end

--
function box.update(space, key, ops)
    return box.process(box.net.box.UPDATE,
        box.pack('iVa', space, 1, key, msgpack.encode(ops)))
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
