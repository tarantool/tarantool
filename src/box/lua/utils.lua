local msgpack = require('msgpack')

-- performance fixup for hot functions
local is_tuple = box.tuple.is
assert(is_tuple ~= nil)

box.NULL = msgpack.NULL

-- Helper function to check space:method() usage
local function check_space_arg(space, method, level)
    if type(space) ~= 'table' or (space.id == nil and space.name == nil) then
        local fmt = 'Use space:%s(...) instead of space.%s(...)'
        box.error(box.error.ILLEGAL_PARAMS, string.format(fmt, method, method),
                  level and level + 1)
    end
end
box.internal.check_space_arg = check_space_arg

-- Helper function to check index:method() usage
local function check_index_arg(index, method, level)
    if type(index) ~= 'table' or (index.id == nil and index.name == nil) then
        local fmt = 'Use index:%s(...) instead of index.%s(...)'
        box.error(box.error.ILLEGAL_PARAMS, string.format(fmt, method, method),
                  level and level + 1)
    end
end
box.internal.check_index_arg = check_index_arg

-- Helper function to check that space have primary key and return it
local function check_primary_index(space, level)
    local pk = space.index[0]
    if pk == nil then
        box.error(box.error.NO_SUCH_INDEX_ID, 0, space.name,
                  level and level + 1)
    end
    return pk
end
box.internal.check_primary_index = check_primary_index

box.index = {
    EQ = 0,
    REQ = 1,
    ALL = 2,
    LT = 3,
    LE = 4,
    GE = 5,
    GT = 6,
    BITS_ALL_SET = 7,
    BITS_ANY_SET = 8,
    BITS_ALL_NOT_SET = 9,
    OVERLAPS = 10,
    NEIGHBOR = 11,
    NP = 12,
    PP = 13,
}

box.index.FORWARD_INCLUSIVE = box.index.GE
box.index.FORWARD_EXCLUSIVE = box.index.GT
box.index.REVERSE_INCLUSIVE = box.index.LE
box.index.REVERSE_EXCLUSIVE = box.index.LT

local function check_iterator_type(opts, key_is_nil, level)
    local opts_type = type(opts)
    if opts ~= nil and opts_type ~= "table" and opts_type ~= "string" and
            opts_type ~= "number" then
        box.error(box.error.ITERATOR_TYPE, opts, level and level + 1)
    end

    local itype
    if opts_type == "table" and opts.iterator then
        if type(opts.iterator) == "number" then
            itype = opts.iterator
        elseif type(opts.iterator) == "string" then
            itype = box.index[string.upper(opts.iterator)]
            if itype == nil then
                box.error(box.error.ITERATOR_TYPE, opts.iterator,
                          level and level + 1)
            end
        else
            box.error(box.error.ITERATOR_TYPE, tostring(opts.iterator),
                      level and level + 1)
        end
    elseif opts_type == "number" then
        itype = opts
    elseif opts_type == "string" then
        itype = box.index[string.upper(opts)]
        if itype == nil then
            box.error(box.error.ITERATOR_TYPE, opts, level and level + 1)
        end
    else
        -- Use ALL for {} and nil keys and EQ for other keys
        itype = key_is_nil and box.index.ALL or box.index.EQ
    end
    return itype
end
box.internal.check_iterator_type = check_iterator_type

local function check_pairs_opts(opts, key_is_nil, level)
    local iterator = check_iterator_type(opts, key_is_nil, level and level + 1)
    local offset = 0
    local after = nil
    if opts ~= nil and type(opts) == "table" then
        if opts.offset ~= nil then
            offset = opts.offset
        end
        if opts.after ~= nil then
            after = opts.after
            if after ~= nil and type(after) ~= "string" and
                    type(after) ~= "table" and not is_tuple(after) then
                box.error(box.error.ITERATOR_POSITION, level and level + 1)
            end
        end
    end
    return iterator, after, offset
end
box.internal.check_pairs_opts = check_pairs_opts

local function check_select_opts(opts, key_is_nil, level)
    local offset = 0
    local limit = 4294967295
    local iterator = check_iterator_type(opts, key_is_nil,
                                         level and level + 1)
    local after = nil
    local fetch_pos = false
    if opts ~= nil and type(opts) == "table" then
        if opts.offset ~= nil then
            offset = opts.offset
        end
        if opts.limit ~= nil then
            limit = opts.limit
        end
        if opts.after ~= nil then
            after = opts.after
            if type(after) ~= "string" and type(after) ~= "table" and
                    not is_tuple(after) then
                box.error(box.error.ITERATOR_POSITION, level and level + 1)
            end
        end
        if opts.fetch_pos ~= nil then
            fetch_pos = opts.fetch_pos
        end
    end
    return iterator, offset, limit, after, fetch_pos
end
box.internal.check_select_opts = check_select_opts

-- Public isolation level map string -> number.
box.txn_isolation_level = {
    ['default'] = 0,
    ['DEFAULT'] = 0,
    ['read-committed'] = 1,
    ['READ_COMMITTED'] = 1,
    ['read-confirmed'] = 2,
    ['READ_CONFIRMED'] = 2,
    ['best-effort'] = 3,
    ['BEST_EFFORT'] = 3,
    ['linearizable'] = 4,
    ['LINEARIZABLE'] = 4,
}

-- Create private isolation level map anything-correct -> number.
local function create_txn_isolation_level_map()
    local res = {}
    for k,v in pairs(box.txn_isolation_level) do
        res[k] = v
        res[v] = v
    end
    return res
end

-- Private isolation level map anything-correct -> number.
local txn_isolation_level_map = create_txn_isolation_level_map()
box.internal.txn_isolation_level_map = txn_isolation_level_map

-- Convert to numeric the value of txn isolation level, raise if failed.
local function normalize_txn_isolation_level(txn_isolation, level)
    txn_isolation = txn_isolation_level_map[txn_isolation]
    if txn_isolation == nil then
        box.error(box.error.ILLEGAL_PARAMS,
                  "txn_isolation must be one of box.txn_isolation_level" ..
                  " (keys or values)", level and level + 1)
    end
    return txn_isolation
end
box.internal.normalize_txn_isolation_level = normalize_txn_isolation_level
