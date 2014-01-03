-- box.lua (internal file)

box.flags = { BOX_RETURN_TUPLE = 0x01, BOX_ADD = 0x02, BOX_REPLACE = 0x04 }

--
--
--
function box.select_limit(space, index, offset, limit, ...)
    local key_part_count = select('#', ...)
    return box.process(box.net.SELECT,
        box.pack('iiiiiV',
            space,
            index,
            offset,
            limit,
            1, -- key count
            key_part_count, ...))
end

--
--
--
function box.select(space, index, ...)
    return box.select_limit(space, index, 0, 4294967295, ...)
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
    return box.process(box.net.DELETE,
        box.pack('iiV',
            space,
            box.flags.BOX_RETURN_TUPLE,  -- flags
            key_part_count, ...))
end

-- insert or replace a tuple
function box.replace(space, ...)
    local field_count = select('#', ...)
    return box.process(box.net.REPLACE,
        box.pack('iiV',
            space,
            box.flags.BOX_RETURN_TUPLE,  -- flags
            field_count, ...))
end

--replace if tuple already presented func
function box.replace_if_exists(space, ...)
    local field_count = select('#', ...)
        return box.process(box.net.REPLACE,
            box.pack('iiV',
                space,
                bit.bor(box.flags.BOX_RETURN_TUPLE, box.flags.BOX_REPLACE),
                field_count, ...))
end;

-- insert a tuple (produces an error if the tuple already exists)
function box.insert(space, ...)
    local field_count = select('#', ...)
    return box.process(box.net.INSERT,
        box.pack('iiV',
            space,
            bit.bor(box.flags.BOX_RETURN_TUPLE,
                box.flags.BOX_ADD),  -- flags
            field_count, ...))
end

--
function box.update(space, key, format, ...)
    local op_count = bit.rshift(select('#', ...), 1)
    return box.process(box.net.UPDATE,
        box.pack('iiVi'..format,
            space,
            box.flags.BOX_RETURN_TUPLE,
            1, key,
            op_count,
            ...))
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
