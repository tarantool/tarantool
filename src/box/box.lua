box.flags = { BOX_RETURN_TUPLE = 0x01, BOX_ADD = 0x02, BOX_REPLACE = 0x04 }

--
--
--
function box.select_limit(space, index, offset, limit, ...)
    local part_count = select('#', ...)
    return box.process(17,
                       box.pack('iiiiii'..string.rep('p', part_count),
                                 space,
                                 index,
                                 offset,
                                 limit,
                                 1, -- key count
                                 part_count, -- key part count
                                 ...))
end

function box.dostring(s, ...)
    local chunk, message = loadstring(s)
    if chunk == nil then
        error(message, 2)
    end
    return chunk(...)
end

--
--
--
function box.select(space, index, ...)
    local part_count = select('#', ...)
    return box.process(17,
                       box.pack('iiiiii'..string.rep('p', part_count),
                                 space,
                                 index,
                                 0, -- offset
                                 4294967295, -- limit
                                 1, -- key count
                                 part_count, -- key part count
                                 ...))
end

--
-- Select a range of tuples in a given namespace via a given
-- index. If key is NULL, starts from the beginning, otherwise
-- starts from the key.
--
function box.select_range(sno, ino, limit, ...)
    return box.space[tonumber(sno)].index[tonumber(ino)]:select_range(tonumber(limit), ...)
end

--
-- Select a range of tuples in a given namespace via a given
-- index in reverse order. If key is NULL, starts from the end, otherwise
-- starts from the key.
--
function box.select_reverse_range(sno, ino, limit, ...)
    return box.space[tonumber(sno)].index[tonumber(ino)]:select_reverse_range(tonumber(limit), ...)
end

--
-- delete can be done only by the primary key, whose
-- index is always 0. It doesn't accept compound keys
--
function box.delete(space, ...)
    local part_count = select('#', ...)
    return box.process(21,
                       box.pack('iii'..string.rep('p', part_count),
                                 space,
                                 box.flags.BOX_RETURN_TUPLE,  -- flags
                                 part_count, -- key part count
                                 ...))
end

-- insert or replace a tuple
function box.replace(space, ...)
    local part_count = select('#', ...)
    return box.process(13,
                       box.pack('iii'..string.rep('p', part_count),
                                 space,
                                 box.flags.BOX_RETURN_TUPLE,  -- flags
                                 part_count, -- key part count
                                 ...))
end

-- insert a tuple (produces an error if the tuple already exists)
function box.insert(space, ...)
    local part_count = select('#', ...)
    return box.process(13,
                       box.pack('iii'..string.rep('p', part_count),
                                space,
                                bit.bor(box.flags.BOX_RETURN_TUPLE,
                                        box.flags.BOX_ADD),  -- flags
                                part_count, -- key part count
                                ...))
end

--
function box.update(space, key, format, ...)
    local op_count = select('#', ...)/2
    if type(key) == 'table' then
        part_count = #key
        return box.process(19,
                    box.pack('iii'..string.rep('p', part_count),
                        space, box.flags.BOX_RETURN_TUPLE, part_count,
                        unpack(key))..
                    box.pack('i'..format, op_count, ...))
    else
        return box.process(19,
                    box.pack('iiipi'..format,
                        space, box.flags.BOX_RETURN_TUPLE, 1,
                        key, op_count, ...))
    end
end

-- Assumes that spaceno has a TREE int32 (NUM) primary key
-- inserts a tuple after getting the next value of the
-- primary key and returns it back to the user
function box.auto_increment(spaceno, ...)
    spaceno = tonumber(spaceno)
    local max_tuple = box.space[spaceno].index[0].idx:max()
    local max = -1
    if max_tuple ~= nil then
        max = box.unpack('i', max_tuple[0])
    end
    return box.insert(spaceno, max + 1, ...)
end

--
-- Simple counter.
--
box.counter = {}

--
-- Increment counter identified by primary key.
-- Create counter if not exists.
-- Returns updated value of the counter.
--
function box.counter.inc(space, ...)
    local key = {...}
    local cnt_index = #key

    local tuple
    while true do
        tuple = box.update(space, key, '+p', cnt_index, 1)
        if tuple ~= nil then break end
        local data = {...}
        table.insert(data, 1)
        tuple = box.insert(space, unpack(data))
        if tuple ~= nil then break end
    end

    return box.unpack('i', tuple[cnt_index])
end

--
-- Decrement counter identified by primary key.
-- Delete counter if it decreased to zero.
-- Returns updated value of the counter.
--
function box.counter.dec(space, ...)
    local key = {...}
    local cnt_index = #key

    local tuple = box.select(space, 0, ...)
    if tuple == nil then return 0 end
    if box.unpack('i', tuple[cnt_index]) == 1 then
        box.delete(space, ...)
        return 0
    else
        tuple = box.update(space, key, '-p', cnt_index, 1)
        return box.unpack('i', tuple[cnt_index])
    end
end

function box.on_reload_configuration()
    local index_mt = {}
    -- __len and __index
    index_mt.len = function(index) return #index.idx end
    index_mt.__newindex = function(table, index)
        return error('Attempt to modify a read-only table') end
    index_mt.__index = index_mt
    -- min and max
    index_mt.min = function(index) return index.idx:min() end
    index_mt.max = function(index) return index.idx:max() end
    -- iteration
    index_mt.iter = function(index, ...)
        return index.idx:iter(...)
    end
    --
    -- pairs/next/prev methods are provided for backward compatibility purposes only
    index_mt.pairs = function(index, ...)
        local iter = index:iter(...)
        iterator_state = function()
            local value = iter()
            if value == nil then
                return nil
            else
                return iterator_state, value
            end
        end

        return iterator_state
    end
    local next_prev_compat = function(index, strategy, ...)
        local arg = {...}
        local iterator_state = nil;
        if #arg == 1 and type(arg[1]) == "function" then
            -- next call, already have an iterator closure
            iterator_state = arg[1]
        else
            -- first call, create new iterator closure
            iterator_state = index:pairs(strategy, ...);
        end

        -- return iterator_state, value
        return iterator_state()
    end
    index_mt.next = function(index, ...)
        return next_prev_compat(index, box.index.ITER_GE, ...)
    end
    index_mt.prev = function(index, ...)
        return next_prev_compat(index, box.index.ITER_LE, ...)
    end
    index_mt.next_equal = function(index, ...)
        return next_prev_compat(index, box.index.ITER_EQ, ...)
    end
    -- there is no difference between next_equal and prev_equal
    index_mt.prev_equal = function(index, ...)
        return next_prev_compat(index, box.index.ITER_EQ, ...)
    end
    -- index subtree size
    index_mt.count = function(index, ...)
        return index.idx:count(...)
    end
    --
    index_mt.select_range = function(index, limit, ...)
        local range = {}
        for v in index:iter(box.index.ITER_GE, ...) do
            if #range >= limit then
                break
            end
            table.insert(range, v)
        end
        return unpack(range)
    end
    index_mt.select_reverse_range = function(index, limit, ...)
        local range = {}
        for v in index:iter(box.index.ITER_LE, ...) do
            if #range >= limit then
                break
            end
            table.insert(range, v)
        end
        return unpack(range)
    end
    --
    local space_mt = {}
    space_mt.len = function(space) return space.index[0]:len() end
    space_mt.__newindex = index_mt.__newindex
    space_mt.select = function(space, ...) return box.select(space.n, ...) end
    space_mt.select_range = function(space, ino, limit, ...)
        return space.index[ino]:select_range(limit, ...)
    end
    space_mt.select_reverse_range = function(space, ino, limit, ...)
        return space.index[ino]:select_reverse_range(limit, ...)
    end
    space_mt.select_limit = function(space, ino, offset, limit, ...)
        return box.select_limit(space.n, ino, offset, limit, ...)
    end
    space_mt.insert = function(space, ...) return box.insert(space.n, ...) end
    space_mt.update = function(space, ...) return box.update(space.n, ...) end
    space_mt.replace = function(space, ...) return box.replace(space.n, ...) end
    space_mt.delete = function(space, ...) return box.delete(space.n, ...) end
    space_mt.truncate = function(space)
        local pk = space.index[0].idx
        local part_count = pk:part_count()
        while #pk > 0 do
            for v in space.index[0]:iter() do
                space:delete(v:slice(0, part_count))
            end
        end
    end
    space_mt.pairs = function(space) return space.index[0]:pairs() end
    space_mt.__index = space_mt
    for i, space in pairs(box.space) do
        rawset(space, 'n', i)
        setmetatable(space, space_mt)
        if type(space.index) == 'table' and space.enabled then
            for j, index in pairs(space.index) do
                rawset(index, 'idx', box.index.new(i, j))
                setmetatable(index, index_mt)
            end
        end
    end
end

require("bit")

-- vim: set et ts=4 sts
