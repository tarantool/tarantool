-- This function creates a new table with constant members.
-- A run-time error will be raised on attempt to change
-- table members.
local function create_const_table(table)
    local function newindex(table, name, value)
        error("Attempt to change constant "..tostring(name)..
              " to "..tostring(value))
    end
    return setmetatable({}, { __index = table,
                              __newindex = newindex,
                              __metatable = false })
end

--- box flags
box.flags = create_const_table(
    {
        BOX_RETURN_TUPLE = 0x01,
        BOX_ADD = 0x02,
        BOX_REPLACE = 0x04,
    })

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

box.upd = {}
--- UPDATE operations codes
box.upd.opcodes = create_const_table(
    {
        -- assign value to field
        ASSIGN = 0,
        -- add value to field's value
        ARITH_ADD = 1,
        -- apply binary AND to field's value
        ARITH_AND = 2,
        -- apply binary XOR to field's value
        ARITH_XOR = 3,
        -- apply binary OR to field's value
        ARITH_OR = 4,
        -- do splice operation
        SPLICE = 5,
        -- delete field
        DELETE = 6,
        -- insert field
        INSERT = 7,
    })

-- create ASSIGN operation for UPDATE command
function box.upd.assign(field, value)
    return { opcode = box.upd.opcodes.ASSIGN, field = field, value = value }
end

-- create ADD operation for UPDATE command
function box.upd.arith_add(field, value)
    return { opcode = box.upd.opcodes.ARITH_ADD, field = field, value = value }
end

-- create AND operation for UPDATE command
function box.upd.arith_and(field, value)
    return { opcode = box.upd.opcodes.ARITH_AND, field = field, value = value }
end

-- create XOR operation for UPDATE command
function box.upd.arith_xor(field, value)
    return { opcode = box.upd.opcodes.ARITH_XOR, field = field, value = value }
end

-- create OR operation for UPDATE command
function box.upd.arith_or(field, value)
    return { opcode = box.upd.opcodes.ARITH_OR, field = field, value = value }
end

-- create SPLICE operation for UPDATE command
function box.upd.splice(field, offset, length, list)
    return { opcode = box.upd.opcodes.SPLICE, field = field, offset = offset,
             length = length, list = list }
end

-- create DELETE operation for UPDATE command
function box.upd.delete(field)
    return { opcode = box.upd.opcodes.DELETE, field = field }
end

-- create INSERT operation for UPDATE command
function box.upd.insert(field, value)
    return { opcode = box.upd.opcodes.INSERT, field = field, value = value }
end

-- execute UPDATE command by operation list
function box.update_ol(space, ops_list, ...)
    local key = {...}

    local format = ''
    local args_list = {}

    -- fill UPDATE command header
    format = format .. 'ii'
    table.insert(args_list, space) -- space number
    table.insert(args_list, box.flags.BOX_RETURN_TUPLE) -- flags

    -- fill UPDATE command key
    format = format .. 'i'
    table.insert(args_list, #key) -- key part count
    for itr, val in ipairs(key) do
        format = format .. 'p'
        table.insert(args_list, val) -- key field
    end

    -- fill UPDATE command operations
    format = format .. "i"
    table.insert(args_list, #ops_list)
    for itr, op in ipairs(ops_list) do
        local ops_operands = nil
        if op.opcode == box.upd.opcodes.ASSIGN or
            op.opcode == box.upd.opcodes.ARITH_ADD or
            op.opcode == box.upd.opcodes.ARITH_ADD or
            op.opcode == box.upd.opcodes.ARITH_AND or
            op.opcode == box.upd.opcodes.ARITH_XOR or
            op.opcode == box.upd.opcodes.ARITH_OR or
            op.opcode == box.upd.opcodes.INSERT then
            -- single operand operation
            ops_operands = op.value
        elseif op.opcode == box.upd.opcodes.SPLICE then
            -- SPLICE operands
            ops_operands = box.pack('ppp', op.offset, op.length, op.list)
        elseif op.opcode == box.upd.opcodes.DELETE then
            -- actually delete doesn't have arguments, but we shold put empty
            -- arguments
            ops_operands = box.pack('p', '')
        else
            return error("invalid UPDATE operation")
        end

        format = format .. "ibp"
        table.insert(args_list, op.field)
        table.insert(args_list, op.opcode)
        table.insert(args_list, ops_operands)
    end

    return box.process(19, box.pack(format, unpack(args_list)))
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
        return index.idx.next, index.idx, nil
    end
    --
    index_mt.next = function(index, ...)
        return index.idx:next(...)
    end
    index_mt.prev = function(index, ...)
        return index.idx:prev(...)
    end
    index_mt.next_equal = function(index, ...)
        return index.idx:next_equal(...)
    end
    index_mt.prev_equal = function(index, ...)
        return index.idx:prev_equal(...)
    end
    -- index subtree size
    index_mt.count = function(index, ...)
        return index.idx:count(...)
    end
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
    index_mt.select_reverse_range = function(index, limit, ...)
        local range = {}
        for k, v in index.idx.prev, index.idx, ... do
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
    space_mt.update_ol = function(space, ops_list, ...)
        return box.update_ol(space.n, ops_list,...)
    end
    space_mt.replace = function(space, ...) return box.replace(space.n, ...) end
    space_mt.delete = function(space, ...) return box.delete(space.n, ...) end
    space_mt.truncate = function(space)
        local pk = space.index[0].idx
        local part_count = pk:part_count()
        while #pk > 0 do
            for k, v in pk.next, pk, nil do
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

-- vim: set et ts=4 sts
