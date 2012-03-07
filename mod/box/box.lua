
--
--
--
function box.select_limit(space, index, offset, limit, ...)
    local key = {...}
    return box.process(17,
                       box.pack('iiiiii'..string.rep('p', #key),
                                 space,
                                 index,
                                 offset,
                                 limit,
                                 1, -- key count
                                 #key, -- key cardinality
                                 unpack(key)))
end

--
--
--
function box.select(space, index, ...)
    local key = {...}
    return box.process(17,
                       box.pack('iiiiii'..string.rep('p', #key),
                                 space,
                                 index,
                                 0, -- offset
                                 4294967295, -- limit
                                 1, -- key count
                                 #key, -- key cardinality
                                 unpack(key)))
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
-- delete can be done only by the primary key, whose
-- index is always 0. It doesn't accept compound keys
--
function box.delete(space, key)
    return box.process(21,
                       box.pack('iiip', space,
                                 1, -- flags, BOX_RETURN_TUPLE
                                 1, -- cardinality
                                 key))
end

-- insert or replace a tuple
function box.replace(space, ...)
    local tuple = {...}
    return box.process(13,
                       box.pack('iii'..string.rep('p', #tuple),
                                 space,
                                 1, -- flags, BOX_RETURN_TUPLE 
                                 #tuple, -- cardinality
                                 unpack(tuple)))
end

-- insert a tuple (produces an error if the tuple already exists)
function box.insert(space, ...)
    local tuple = {...}
    return box.process(13,
                       box.pack('iii'..string.rep('p', #tuple),
                                space,
                                3, -- flags, BOX_RETURN_TUPLE  | BOX_ADD
                                #tuple, -- cardinality
                                unpack(tuple)))
end

--
function box.update(space, key, format, ...)
    local ops = {...}
    return box.process(19,
                       box.pack('iiipi'..format,
                                  space,
                                  1, -- flags, BOX_RETURN_TUPLE
                                  1, -- cardinality
                                  key, -- primary key
                                  #ops/2, -- op count
                                  unpack(ops)))
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
    index_mt.pairs = function(index)
        return index.idx.next, index.idx, nil end
    --
    index_mt.next = function(index, ...)
        return index.idx:next(...) end
    --
    index_mt.select_range = function(index, limit, ...)
        local range = {}
        for k, v in index.idx.next, index.idx, ... do
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
    space_mt.select_limit = function(space, ino, offset, limit, ...)
	return box.select_limit(space.n, ino, offset, limit, ...)
    end
    space_mt.insert = function(space, ...) return box.insert(space.n, ...) end
    space_mt.update = function(space, ...) return box.update(space.n, ...) end
    space_mt.replace = function(space, ...) return box.replace(space.n, ...) end
    space_mt.delete = function(space, ...) return box.delete(space.n, ...) end
    space_mt.truncate = function(space)
        while true do
            local k, v = space.index[0].idx:next()
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
        if type(space.index) == 'table' and space.enabled then
            for j, index in pairs(space.index) do
                rawset(index, 'idx', box.index.new(i, j))
                setmetatable(index, index_mt)
            end
        end
    end
end
local initfile = io.open("init.lua")
if initfile ~= nil then
    io.close(initfile)
    dofile("init.lua")
end
-- security: nullify some of the most serious os.* holes
--
os.execute = nil
os.exit = nil
os.rename = nil
os.tmpname = nil
os.remove = nil
require = nil
