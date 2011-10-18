--
--
--
function box.select(space, index, ...)
    key = {...}
    return select(2, -- skip the first return from select, number of tuples
        box.process(17, box.pack('iiiiii'..string.rep('p', #key),
                                 space,
                                 index,
                                 0, -- offset
                                 4294967295, -- limit
                                 1, -- key count
                                 #key, -- key cardinality
                                 unpack(key))))
end

--
-- Select a range of tuples in a given namespace via a given
-- index. If key is NULL, starts from the beginning, otherwise
-- starts from the key.
--
function box.select_range(sno, ino, limit, ...)
    return box.space[tonumber(sno)].index[tonumber(ino)]:range(tonumber(limit), ...)
end

--
-- delete can be done only by the primary key, whose
-- index is always 0. It doesn't accept compound keys
--
function box.delete(space, key)
    return select(2, -- skip the first return, tuple count
        box.process(21, box.pack('iiip', space,
                                 1, -- flags, BOX_RETURN_TUPLE
                                 1, -- cardinality
                                 key)))
end

-- insert or replace a tuple
function box.replace(space, ...)
    tuple = {...}
    return select(2,
        box.process(13, box.pack('iii'..string.rep('p', #tuple),
                                 space,
                                 1, -- flags, BOX_RETURN_TUPLE 
                                 #tuple, -- cardinality
                                 unpack(tuple))))
end

-- insert a tuple (produces an error if the tuple already exists)
function box.insert(space, ...)
    tuple = {...}
    return select(2,
        box.process(13, box.pack('iii'..string.rep('p', #tuple),
                                 space,
                                 3, -- flags, BOX_RETURN_TUPLE | BOX_ADD
                                 #tuple, -- cardinality
                                 unpack(tuple))))
end

function box.update(space, key, format, ...)
    ops = {...}
    return select(2,
        box.process(19, box.pack('iiipi'..format,
                                  space,
                                  1, -- flags, BOX_RETURN_TUPLE
                                  1, -- cardinality
                                  key, -- primary key
                                  #ops/2, -- op count
                                  ...)))
end

function box.on_reload_configuration()
    index_mt = {}
    -- __len and __index
    index_mt.len = function(index) return #index.idx end
    index_mt.__newindex = function(table, index)
        return error('Attempt to modify a read-only table') end
    index_mt.__index = index_mt
    -- min and max
    index_mt.min = function(index) return index.idx:min() end
    index_mt.max = function(index) return index.idx:max() end
    -- iteration
    index_mt.pairs = function(index)
        return index.idx.next, index.idx, nil end
    --
    index_mt.range = function(index, limit, ...)
        range = {}
        for k, v in index.idx.next, index.idx, ... do
            if #range >= limit then
                break
            end
            table.insert(range, v)
        end
        return unpack(range)
    end
    --
    space_mt = {}
    space_mt.len = function(space) return space.index[0]:len() end
    space_mt.__newindex = index_mt.__newindex
    space_mt.select = function(space, ...) return box.select(space.n, ...) end
    space_mt.insert = function(space, ...) return box.insert(space.n, ...) end
    space_mt.update = function(space, ...) return box.update(space.n, ...) end
    space_mt.replace = function(space, ...) return box.replace(space.n, ...) end
    space_mt.delete = function(space, ...) return box.delete(space.n, ...) end
    space_mt.truncate = function(space)
        while true do
            k, v = space.index[0].idx:next()
            if v == nil then
                break
            end
            space:delete(v[0])
        end
    end
    space_mt.pairs = function(space) return space.index[0]:pairs() end
    space_mt.__index = space_mt
    for i, space in pairs(box.space) do
        rawset(space, 'n', i)
        setmetatable(space, space_mt)
        for j, index in pairs(space.index) do
            rawset(index, 'idx', box.index.new(i, j))
            setmetatable(index, index_mt)
        end
    end
end
local initfile = io.open("init.lua")
if initfile ~= nil then
    io.close(initfile)
    dofile("init.lua")
end
