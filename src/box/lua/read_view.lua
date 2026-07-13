local utils = require('internal.utils')

local check_index_arg = box.internal.check_index_arg
local check_param_table = utils.check_param_table
local check_primary_index = box.internal.check_primary_index
local check_iterator_type = box.internal.check_iterator_type
local check_select_opts = box.internal.check_select_opts
local check_pairs_opts = box.internal.check_pairs_opts
local check_space_arg = box.internal.check_space_arg

local tuple_bless = box.internal.tuple.bless
local tuple_encode = box.internal.tuple.encode

local buffer = require('buffer')
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

local msgpackffi = require('msgpackffi')

local ffi = require('ffi')
ffi.cdef([[
    struct index_read_view_iterator { char pad[80]; };
    int box_index_read_view_tuple_position(
            struct index_read_view *rv,
            const char *tuple, const char *tuple_end,
            const char **packed_pos, const char **packed_pos_end);
    int box_index_read_view_get(struct index_read_view *rv, const char *key,
                                const char *key_end, struct tuple **result);
    int box_index_read_view_count(struct index_read_view *rv, int iterator,
                                  const char *key, const char *key_end);
    int box_index_read_view_quantile(
        struct index_read_view *rv, double level, const char *begin_key,
            const char *begin_key_end, const char *end_key,
            const char *end_key_end, const char **quantile_key,
            const char **quantile_key_end);
    int box_index_read_view_select(struct index_read_view *rv, int iterator,
                                   uint32_t offset, uint32_t limit,
                                   const char *key, const char *key_end,
                                   const char **packed_pos,
                                   const char **packed_pos_end,
                                   bool update_pos, struct port *port);
    int box_index_read_view_create_iterator_with_offset(
            struct index_read_view *rv, int iterator,
            const char *key, const char *key_end,
            const char *packed_pos, const char *packed_pos_end,
            uint32_t offset, struct index_read_view_iterator *it);
    int box_index_read_view_iterator_next(
            struct index_read_view_iterator *it, struct tuple **result);
    void box_index_read_view_iterator_destroy(
            struct index_read_view_iterator *it);
]])
local builtin = ffi.C

-- Static objects used by the FFI API to store results.
local ptuple = ffi.new('struct tuple *[1]')
local port = ffi.new('struct port')
local port_c = ffi.cast('struct port_c *', port)
local iterator_pos = ffi.new('const char *[1]')
local iterator_pos_end = ffi.new('const char *[1]')

local internal = box.internal.read_view

-- box.read_view.open() options template.
local READ_VIEW_OPTIONS_TEMPLATE = {
    name = 'string',
}

-- Helper function that returns a copy of the given object with private fields
-- (fields whose names start with an underscore) filtered out. It is used for
-- object serialization.
local function filter_private_fields(t)
    local ret = {}
    for k, v in pairs(t) do
        assert(type(k) == 'string')
        if not k:startswith('_') then
            ret[k] = v
        end
    end
    return ret
end

local read_view_space_list_mt = {
    -- Returns a table of spaces keyed by space name with system spaces
    -- (spaces whose names start with an underscore) filtered out.
    __serialize = function(self)
        local ret = {}
        for k, space in pairs(self) do
            if type(k) == 'string' and not k:startswith('_') then
                ret[k] = {
                    id = space.id,
                }
            end
        end
        return ret
    end
}

local read_view_space_methods = {}
local read_view_space_mt = {
    __index = read_view_space_methods,
}

local read_view_index_methods_common = {}
local read_view_index_mt_base = {
    __serialize = filter_private_fields,
}

local read_view_index_methods_luac = {}
local read_view_index_mt_luac = {
    __index = read_view_index_methods_luac,
}

local read_view_index_methods_ffi = {}
local read_view_index_mt_ffi = {
    __index = read_view_index_methods_ffi,
}

for k, v in pairs(read_view_index_mt_base) do
    read_view_index_mt_luac[k] = v
    read_view_index_mt_ffi[k] = v
end

--
-- Opens a new read view.
--
-- Takes an optional argument that contains a table of read view options.
-- Returns a read view object on success. On error, raises an exception.
--
-- Available options:
--  - 'name' - read view name. If omitted 'unknown' is used.
--
function box.read_view.open(opts)
    check_param_table(opts, READ_VIEW_OPTIONS_TEMPLATE)
    local rv = internal.open(opts and opts.name or 'unknown')
    setmetatable(rv.space, read_view_space_list_mt)
    for space_id, space in pairs(rv.space) do
        if type(space_id) == 'number' then
            setmetatable(space, read_view_space_mt)
            for index_id, index in pairs(space.index) do
                if type(index_id) == 'number' then
                    if index._ffi then
                        setmetatable(index, read_view_index_mt_ffi)
                    else
                        setmetatable(index, read_view_index_mt_luac)
                    end
                end
            end
        end
    end
    return box.internal.read_view_register(rv)
end

--
-- If the given read view was created with box.read_view.open(), the function
-- closes it. Any attempt to use the read view afterwards will raise an error.
-- Otherwise, it's unsafe to close this read view, because it may be used from
-- the C code so in this case this function does nothing and raises an error.
--
function box.internal.read_view_close(rv, level)
    if rv._impl == nil then
        -- System read view. Can't be closed.
        box.error(box.error.READ_VIEW_BUSY, level + 1)
    end
    for _, space in pairs(rv.space) do
        for _, index in pairs(space.index) do
            -- Reset cdata because it's going to be invalidated by 'close'.
            -- From now on, FFI methods will assume that the read view is
            -- closed.
            index._cdata = nil
        end
    end
    rv._impl:close()
end

--
-- Helper function that normalizes a key passed by the user:
--
--   nil -> {}
--   table, tuple -> unchanged
--   scalar -> {scalar}
--
local function keify(key)
    if key == nil then
        return {}
    elseif type(key) == "table" or box.tuple.is(key) then
        return key
    else
        return {key}
    end
end

--
-- Checks if the given index read view is open. Used by the FFI API.
-- (The Lua C API has this check implemented in C.)
--
local function check_index_read_view_is_open_ffi(index)
    if index._cdata == nil then
        box.error(box.error.READ_VIEW_CLOSED, 3)
    end
end

--
-- Sets iterator_pos and iterator_pos_end to a user-supplied position.
--
-- The input position may be nil, string, table, or tuple. If the input
-- position is given as string, iterator_pos is set to point to its data,
-- otherwise the iterator_pos data is allocated from the fiber region.
--
-- The ibuf is used to encode a position given as table or tuple.
--
-- Returns true on success. On failure, sets box.error and returns false.
--
local function iterator_pos_set(index, pos, ibuf)
    if pos == nil then
        iterator_pos[0] = nil
        iterator_pos_end[0] = nil
        return true
    elseif type(pos) == 'string' then
        iterator_pos[0] = pos
        iterator_pos_end[0] = iterator_pos[0] + #pos
        return true
    else
        ibuf.rpos = ibuf.wpos
        local tuple, tuple_end = tuple_encode(ibuf, pos)
        return builtin.box_index_read_view_tuple_position(
                index._cdata, tuple, tuple_end,
                iterator_pos, iterator_pos_end) == 0
    end
end

--
-- Gets a tuple by key from an index read view.
-- The index must be unique and the key must be full.
-- Returns a tuple. If not found, returns nil.
--
function read_view_index_methods_luac:get(key)
    check_index_arg(self, 'get')
    key = keify(key)
    return self._impl:get(key)
end

function read_view_index_methods_ffi:get(key)
    check_index_arg(self, 'get')
    check_index_read_view_is_open_ffi(self)
    local ibuf = cord_ibuf_take()
    local raw_key, raw_key_end = tuple_encode(ibuf, key)
    local ok = builtin.box_index_read_view_get(
            self._cdata, raw_key, raw_key_end, ptuple) == 0
    cord_ibuf_put(ibuf)
    if not ok then
        box.error()
    elseif ptuple[0] ~= nil then
        return tuple_bless(ptuple[0])
    end
end

--
-- Counts tuples matching a key in an index read view.
--
-- 'opts' is a table of count options:
--  - 'iterator' - iterator type (for example, 'ge', 'eq', 'lt').
--     Default: 'all' for empty key, 'eq' for other keys.
--
-- Returns a number.
--
function read_view_index_methods_luac:count(key, opts)
    check_index_arg(self, 'count')
    key = keify(key)
    local itype = check_iterator_type(opts, #key == 0)
    return self._impl:count(itype, key)
end

function read_view_index_methods_ffi:count(key, opts)
    check_index_arg(self, 'count')
    check_index_read_view_is_open_ffi(self)
    local ibuf = cord_ibuf_take()
    local raw_key, raw_key_end = tuple_encode(ibuf, key)
    local key_is_nil = raw_key + 1 >= raw_key_end
    local itype = check_iterator_type(opts, key_is_nil)
    local count = builtin.box_index_read_view_count(self._cdata, itype,
                                                    raw_key, raw_key_end)
    cord_ibuf_put(ibuf)
    if count < 0 then
        box.error()
    end
    return count
end

--
-- Selects tuples matching a key from an index read view.
--
-- 'opts' is a table of select options:
--  - 'iterator' - iterator type (for example, 'ge', 'eq', 'lt').
--     Default: 'all' for empty key, 'eq' for other keys.
--  - 'offset' - how many tuples to skip. Default: 0.
--  - 'limit' - max number of tuples to return. Default: unlimited.
--
-- Returns an array of tuples (may be empty).
--
function read_view_index_methods_luac:select(key, opts)
    check_index_arg(self, 'select')
    key = keify(key)
    local key_is_nil = #key == 0
    local iterator, offset, limit, after, fetch_pos =
        check_select_opts(opts, key_is_nil)
    return self._impl:select(iterator, offset, limit, key, after, fetch_pos)
end

function read_view_index_methods_ffi:select(key, opts)
    check_index_arg(self, 'select')
    check_index_read_view_is_open_ffi(self)
    local region_svp = builtin.box_region_used()
    local ibuf = cord_ibuf_take()
    local raw_key, raw_key_end = tuple_encode(ibuf, key)
    local key_is_nil = raw_key + 1 >= raw_key_end
    local iterator, offset, limit, after, fetch_pos =
        check_select_opts(opts, key_is_nil)
    local ok = iterator_pos_set(self, after, ibuf)
    if ok then
        ok = builtin.box_index_read_view_select(
                self._cdata, iterator, offset, limit, raw_key, raw_key_end,
                iterator_pos, iterator_pos_end, fetch_pos, port) == 0
    end
    local pos
    if ok and fetch_pos and iterator_pos[0] ~= nil then
        pos = ffi.string(iterator_pos[0], iterator_pos_end[0] - iterator_pos[0])
    end
    cord_ibuf_put(ibuf)
    builtin.box_region_truncate(region_svp)
    if not ok then
        box.error()
    end
    local ret = {}
    local entry = port_c.first
    for i = 1, tonumber(port_c.size) do
        ret[i] = tuple_bless(entry.tuple)
        entry = entry.next
    end
    builtin.port_destroy(port);
    return ret, pos
end

--
-- Creates an iterator over an index read view, given key and iterator type.
-- Returns a 'gen, param, state' triplet, suitable for a 'for' loop.
--
function read_view_index_methods_luac:pairs(key, opts)
    check_index_arg(self, 'pairs', 2)
    key = keify(key)
    local key_is_nil = #key == 0
    local iterator, after, offset = check_pairs_opts(opts, key_is_nil)
    local it = self._impl:iterator(iterator, key, after, offset)
    return it.next, it
end

local function iterator_next_ffi(it, count)
    check_index_read_view_is_open_ffi(it.index)
    if builtin.box_index_read_view_iterator_next(it.cdata, ptuple) ~= 0 then
        box.error()
    elseif ptuple[0] ~= nil then
        return count + 1, tuple_bless(ptuple[0])
    end
end

function read_view_index_methods_ffi:pairs(key, opts)
    check_index_arg(self, 'pairs')
    check_index_read_view_is_open_ffi(self)
    local region_svp = builtin.box_region_used()
    local ibuf = cord_ibuf_take()
    local raw_key, raw_key_end = tuple_encode(ibuf, key)
    local key_is_nil = raw_key + 1 >= raw_key_end
    local iterator, after, offset = check_pairs_opts(opts, key_is_nil)
    local ok = iterator_pos_set(self, after, ibuf)
    local cdata, key_buf
    if ok then
        -- A cdata iterator object assumes that the search key won't be deleted
        -- while the iterator is in use so we have to keep a reference to it in
        -- Lua.
        key_buf = ffi.string(raw_key, raw_key_end - raw_key)
        raw_key = ffi.cast('const char *', key_buf)
        raw_key_end = raw_key + #key_buf
        cdata = ffi.new('struct index_read_view_iterator')
        ok = builtin.box_index_read_view_create_iterator_with_offset(
                self._cdata, iterator, raw_key, raw_key_end,
                iterator_pos[0], iterator_pos_end[0], offset, cdata) == 0
    end
    cord_ibuf_put(ibuf)
    builtin.box_region_truncate(region_svp)
    if not ok then
        box.error()
    end
    ffi.gc(cdata, builtin.box_index_read_view_iterator_destroy)
    return iterator_next_ffi, {
        index = self,
        key_buf = key_buf,
        cdata = cdata,
    }, 0
end

function read_view_index_methods_common:quantile(level, begin_key, end_key)
    check_index_arg(self, 'quantile')
    check_index_read_view_is_open_ffi(self)
    if level == nil then
        box.error(box.error.ILLEGAL_PARAMS,
                  'Usage: index:quantile(level[, begin_key, end_key])', 2)
    end
    if type(level) ~= 'number' then
        box.error(box.error.ILLEGAL_PARAMS, 'level must be a number', 2)
    end
    -- Encode the keys on cord ibuf. Note, since the ibuf may be
    -- reallocated when we encode end_key, we can't use the pointers
    -- returned by tuple_encode(begin_key), so we set its pointers after
    -- all allocations are done.
    local ibuf = cord_ibuf_take()
    tuple_encode(ibuf, begin_key, 2)
    local end_key, end_key_end = tuple_encode(ibuf, end_key, 2)
    begin_key = ibuf.rpos
    local begin_key_end = end_key
    -- Call the C API function.
    local quantile_key = ffi.new('const char *[1]')
    local quantile_key_end = ffi.new('const char *[1]')
    local region_svp = builtin.box_region_used()
    local ok = builtin.box_index_read_view_quantile(
            self._cdata, level, begin_key, begin_key_end, end_key, end_key_end,
            quantile_key, quantile_key_end) == 0
    cord_ibuf_put(ibuf)
    if not ok then
        box.error(box.error.last(), 2)
    end
    -- Decode the result and clean up the region.
    local result
    if quantile_key[0] ~= nil then
        local ptr
        result, ptr = msgpackffi.decode_unchecked(quantile_key[0])
        assert(ptr == quantile_key_end[0])
    else
        result = nil
    end
    builtin.box_region_truncate(region_svp)
    return result
end

function read_view_index_methods_common:tuple_pos(tuple)
    check_index_arg(self, 'tuple_pos')
    check_index_read_view_is_open_ffi(self)
    local region_svp = builtin.box_region_used()
    local ibuf = cord_ibuf_take()
    local data, data_end = tuple_encode(ibuf, tuple)
    local ok = builtin.box_index_read_view_tuple_position(
            self._cdata, data, data_end, iterator_pos, iterator_pos_end) == 0
    cord_ibuf_put(ibuf)
    if not ok then
        box.error()
    end
    local ret = ffi.string(iterator_pos[0],
                           iterator_pos_end[0] - iterator_pos[0])
    builtin.box_region_truncate(region_svp)
    return ret
end

function read_view_index_methods_common:offset_of(key, opts)
    check_index_arg(self, 'offset_of')
    check_index_read_view_is_open_ffi(self)
    key = keify(key)
    local itype = check_iterator_type(opts, #key == 0)
    if itype == box.index.EQ then
        itype = box.index.GE
    elseif itype == box.index.REQ then
        itype = box.index.LE
    end
    return self:count() - self:count(key, itype)
end

for k, v in pairs(read_view_index_methods_common) do
    read_view_index_methods_luac[k] = v
    read_view_index_methods_ffi[k] = v
end

-- Shortcuts: space:foo(...) -> space.index[0]:foo(...)
local shortcuts = {'get', 'count', 'select', 'pairs', 'offset_of', 'quantile'}
for _, method in pairs(shortcuts) do
    read_view_space_methods[method] = function(space, ...)
        check_space_arg(space, method)
        local index = check_primary_index(space)
        return index[method](index, ...)
    end
end
