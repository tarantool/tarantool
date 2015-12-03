-- schema.lua (internal file)
--
local ffi = require('ffi')
local msgpack = require('msgpack')
local msgpackffi = require('msgpackffi')
local fun = require('fun')
local session = box.session
local internal = require('box.internal')

local builtin = ffi.C

ffi.cdef[[
    struct space *space_by_id(uint32_t id);
    void space_run_triggers(struct space *space, bool yesno);

    typedef struct tuple box_tuple_t;
    typedef struct iterator box_iterator_t;

    /** \cond public */
    box_iterator_t *
    box_index_iterator(uint32_t space_id, uint32_t index_id, int type,
                       const char *key, const char *key_end);
    int
    box_iterator_next(box_iterator_t *itr, box_tuple_t **result);
    void
    box_iterator_free(box_iterator_t *itr);
    /** \endcond public */
    /** \cond public */
    size_t
    box_index_len(uint32_t space_id, uint32_t index_id);
    size_t
    box_index_bsize(uint32_t space_id, uint32_t index_id);
    int
    box_index_random(uint32_t space_id, uint32_t index_id, uint32_t rnd,
                     box_tuple_t **result);
    int
    box_index_get(uint32_t space_id, uint32_t index_id, const char *key,
                  const char *key_end, box_tuple_t **result);
    int
    box_index_min(uint32_t space_id, uint32_t index_id, const char *key,
                  const char *key_end, box_tuple_t **result);
    int
    box_index_max(uint32_t space_id, uint32_t index_id, const char *key,
                  const char *key_end, box_tuple_t **result);
    ssize_t
    box_index_count(uint32_t space_id, uint32_t index_id, int type,
                    const char *key, const char *key_end);
    /** \endcond public */
    /** \cond public */
    int
    box_txn_begin();
    void
    box_txn_rollback();
    /** \endcond public */

    struct port
    {
        struct port_vtab *vtab;
    };

    struct port_buf_entry {
        struct port_buf_entry *next;
        struct tuple *tuple;
    };

    struct port_buf {
        struct port base;
        size_t size;
        struct port_buf_entry *first;
        struct port_buf_entry *last;
        struct port_buf_entry first_entry;
    };

    void
    port_buf_create(struct port_buf *port_buf);

    void
    port_buf_destroy(struct port_buf *port_buf);

    void
    port_buf_transfer(struct port_buf *port_buf);

    int
    box_select(struct port_buf *port, uint32_t space_id, uint32_t index_id,
               int iterator, uint32_t offset, uint32_t limit,
               const char *key, const char *key_end);
    void password_prepare(const char *password, int len,
                          char *out, int out_len);
]]

local function user_or_role_resolve(user)
    local _user = box.space[box.schema.USER_ID]
    local tuple
    if type(user) == 'string' then
        tuple = _user.index.name:get{user}
    else
        tuple = _user:get{user}
    end
    if tuple == nil then
        return nil
    end
    return tuple[1]
end

local function role_resolve(name_or_id)
    local _user = box.space[box.schema.USER_ID]
    local tuple
    if type(name_or_id) == 'string' then
        tuple = _user.index.name:get{name_or_id}
    elseif type(name_or_id) ~= 'nil' then
        tuple = _user:get{name_or_id}
    end
    if tuple == nil or tuple[4] ~= 'role' then
        return nil
    else
        return tuple[1]
    end
end

local function user_resolve(name_or_id)
    local _user = box.space[box.schema.USER_ID]
    local tuple
    if type(name_or_id) == 'string' then
        tuple = _user.index.name:get{name_or_id}
    elseif type(name_or_id) ~= 'nil' then
        tuple = _user:get{name_or_id}
    end
    if tuple == nil or tuple[4] ~= 'user' then
        return nil
    else
        return tuple[1]
    end
end

--[[
 @brief Common function to check table with parameters (like options)
 @param table - table with parameters
 @param template - table with expected types of expected parameters
  type could be comma separated string with lua types (number, string etc),
  or 'any' if any type is allowed
 The function checks following:
 1)that parameters table is a table (or nil)
 2)all keys in parameters are present in template
 3)type of every parameter fits (one of) types described in template
 Check (2) and (3) could be disabled by adding {, dont_check = <smth is true>, }
  into parameters table
 The functions calls box.error(box.error.ILLEGAL_PARAMS, ..) on error
 @example
 check_param_table(options, { user = 'string',
                              port = 'string, number',
                              data = 'any' } )
--]]
local function check_param_table(table, template)
    if table == nil then
        return
    end
    if type(table) ~= 'table' then
        box.error(box.error.ILLEGAL_PARAMS,
                  "options should be a table")
    end
    -- just pass {.. dont_check = true, ..} to disable checks below
    if table.dont_check then
        return
    end
    for k,v in pairs(table) do
        if template[k] == nil then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options parameter '" .. k .. "' is unexpected")
        elseif template[k] == 'any' then
            -- any type is ok
        elseif (string.find(template[k], ',') == nil) then
            -- one type
            if type(v) ~= template[k] then
                box.error(box.error.ILLEGAL_PARAMS,
                          "options parameter '" .. k ..
                          "' should be of type " .. template[k])
            end
        else
            local good_types = string.gsub(template[k], ' ', '')
            local haystack = ',' .. good_types .. ','
            local needle = ',' .. type(v) .. ','
            if (string.find(haystack, needle) == nil) then
                good_types = string.gsub(good_types, ',', ', ')
                box.error(box.error.ILLEGAL_PARAMS,
                          "options parameter '" .. k ..
                          "' should be one of types: " .. template[k])
            end
        end
    end
end

--[[
 @brief Common function to check type parameter (of function)
 Calls box.error(box.error.ILLEGAL_PARAMS, ) on error
 @example: check_param(user, 'user', 'string')
--]]
local function check_param(param, name, should_be_type)
    if type(param) ~= should_be_type then
        box.error(box.error.ILLEGAL_PARAMS,
                  name .. " should be a " .. should_be_type)
    end
end

--[[
 Adds to a table key-value pairs from defaults table
  that is not present in original table.
 Returns updated table.
 If nil is passed instead of table, it's treated as empty table {}
 For example update_param_table({ type = 'hash', temporary = true },
                                { type = 'tree', unique = true })
  will return table { type = 'hash', temporary = true, unique = true }
--]]
local function update_param_table(table, defaults)
    if table == nil then
        return defaults
    end
    if defaults == nil then
        return table
    end
    for k,v in pairs(defaults) do
        if table[k] == nil then
            table[k] = v
        end
    end
    return table
end

box.begin = function()
    if builtin.box_txn_begin() == -1 then
        box.error()
    end
end
-- box.commit yields, so it's defined as Lua/C binding

box.rollback = builtin.box_txn_rollback;

box.schema.space = {}
box.schema.space.create = function(name, options)
    check_param(name, 'name', 'string')
    local options_template = {
        if_not_exists = 'boolean',
        temporary = 'boolean',
        engine = 'string',
        id = 'number',
        field_count = 'number',
        user = 'string, number',
        format = 'table'
    }
    local options_defaults = {
        engine = 'memtx',
        field_count = 0,
    }
    check_param_table(options, options_template)
    options = update_param_table(options, options_defaults)

    local _space = box.space[box.schema.SPACE_ID]
    if box.space[name] then
        if options.if_not_exists then
            return box.space[name], "not created"
        else
            box.error(box.error.SPACE_EXISTS, name)
        end
    end
    local id = options.id
    if not id then
        local _schema = box.space._schema
        local max_id = _schema:update({'max_id'}, {{'+', 2, 1}})
        if max_id == nil then
            id = _space.index.primary:max()[1]
            if id < box.schema.SYSTEM_ID_MAX then
                id = box.schema.SYSTEM_ID_MAX
            end
            max_id = _schema:insert{'max_id', id + 1}
        end
        id = max_id[2]
    end
    local uid = nil
    if options.user then
        uid = user_or_role_resolve(options.user)
    end
    if uid == nil then
        uid = session.uid()
    end
    local temporary = options.temporary and "temporary" or ""
    local format = options.format and options.format or {}
    _space:insert{id, uid, name, options.engine, options.field_count, temporary, format}
    return box.space[id], "created"
end

-- space format - the metadata about space fields
function box.schema.space.format(id, format)
    local _space = box.space._space
    check_param(id, 'id', 'number')
    check_param(format, 'format', 'table')
    if format == nil then
        return _space:get(id)[7]
    else
        _space:update(id, {{'=', 7, format}})
    end
end

box.schema.create_space = box.schema.space.create

box.schema.space.drop = function(space_id, space_name)
    check_param(space_id, 'space_id', 'number')

    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local keys = _index:select(space_id)
    for i = #keys, 1, -1 do
        local v = keys[i]
        _index:delete{v[1], v[2]}
    end
    local privs = _priv.index.object:select{'space', space_id}
    for k, tuple in pairs(privs) do
        box.schema.user.revoke(tuple[2], tuple[5], tuple[3], tuple[4])
    end
    if _space:delete{space_id} == nil then
        if space_name == nil then
            space_name = '#'..tostring(space_id)
        end
        box.error(box.error.NO_SUCH_SPACE, space_name)
    end
end

box.schema.space.rename = function(space_id, space_name)
    check_param(space_id, 'space_id', 'number')
    check_param(space_name, 'space_name', 'string')

    local _space = box.space[box.schema.SPACE_ID]
    _space:update(space_id, {{"=", 3, space_name}})
end

box.schema.index = {}

local function check_index_parts(parts)
    if type(parts) ~= "table" then
        box.error(box.error.ILLEGAL_PARAMS,
                  "options.parts parameter should be a table")
    end
    if #parts % 2 ~= 0 then
        box.error(box.error.ILLEGAL_PARAMS,
                  "options.parts: expected field_no (number), type (string) pairs")
    end
    for i=1,#parts,2 do
        if type(parts[i]) ~= "number" then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts: expected field_no (number), type (string) pairs")
        elseif parts[i] == 0 then
            -- Lua uses one-based field numbers but _space is zero-based
            box.error(box.error.ILLEGAL_PARAMS,
                      "invalid index parts: field_no must be one-based")
        end
    end
    for i=2,#parts,2 do
        if type(parts[i]) ~= "string" then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts: expected field_no (number), type (string) pairs")
        end
    end
end

local function update_index_parts(parts)
    for i=1,#parts,2 do
        -- Lua uses one-based field numbers but _space is zero-based
        parts[i] = parts[i] - 1
    end
    return parts
end

box.schema.index.create = function(space_id, name, options)
    check_param(space_id, 'space_id', 'number')
    check_param(name, 'name', 'string')
    local options_template = {
        type = 'string',
        parts = 'table',
        unique = 'boolean',
        id = 'number',
        if_not_exists = 'boolean',
        dimension = 'number',
        distance = 'string',
    }
    check_param_table(options, options_template)
    local options_defaults = {
        type = 'tree',
    }
    options = update_param_table(options, options_defaults)
    local type_dependent_defaults = {
        rtree = {parts = { 2, 'array' }, unique = false},
        bitset = {parts = { 2, 'num' }, unique = false},
        other = {parts = { 1, 'num' }, unique = true},
    }
    options_defaults = type_dependent_defaults[options.type]
            and type_dependent_defaults[options.type]
            or type_dependent_defaults.other
    options = update_param_table(options, options_defaults)

    check_index_parts(options.parts)
    options.parts = update_index_parts(options.parts)

    local _index = box.space[box.schema.INDEX_ID]
    if _index.index.name:get{space_id, name} then
        if options.if_not_exists then
            return box.space[space_id].index[name], "not created"
        else
            box.error(box.error.INDEX_EXISTS, name)
        end
    end

    local iid = 0
    if options.id then
        iid = options.id
    else
        -- max
        local tuple = _index.index[0]
            :select(space_id, { limit = 1, iterator = 'LE' })[1]
        if tuple then
            local id = tuple[1]
            if id == space_id then
                iid = tuple[2] + 1
            end
        end
    end
    local parts = {}
    for i = 1, #options.parts, 2 do
        table.insert(parts, {options.parts[i], options.parts[i + 1]})
    end
    local key_opts = { dimension = options.dimension,
        unique = options.unique, distance = options.distance }
    _index:insert{space_id, iid, name, options.type, key_opts, parts}
    return box.space[space_id].index[name]
end

box.schema.index.drop = function(space_id, index_id)
    check_param(space_id, 'space_id', 'number')
    check_param(index_id, 'index_id', 'number')

    local _index = box.space[box.schema.INDEX_ID]
    _index:delete{space_id, index_id}
end

box.schema.index.rename = function(space_id, index_id, name)
    check_param(space_id, 'space_id', 'number')
    check_param(index_id, 'index_id', 'number')
    check_param(name, 'name', 'string')

    local _index = box.space[box.schema.INDEX_ID]
    _index:update({space_id, index_id}, {{"=", 3, name}})
end

box.schema.index.alter = function(space_id, index_id, options)
    if box.space[space_id] == nil then
        box.error(box.error.NO_SUCH_SPACE, '#'..tostring(space_id))
    end
	if box.space[space_id].engine == 'sophia' then
		box.error(box.error.SOPHIA, 'alter is not supported for a Sophia index')
	end
    if box.space[space_id].index[index_id] == nil then
        box.error(box.error.NO_SUCH_INDEX, index_id, box.space[space_id].name)
    end
    if options == nil then
        return
    end

    local options_template = {
        id = 'number',
        name = 'string',
        type = 'string',
        parts = 'table',
        unique = 'boolean',
        dimension = 'number',
        distance = 'string',
    }
    check_param_table(options, options_template)

    if type(space_id) ~= "number" then
        space_id = box.space[space_id].id
    end
    if type(index_id) ~= "number" then
        index_id = box.space[space_id].index[index_id].id
    end
    local _index = box.space[box.schema.INDEX_ID]
    if options.id ~= nil then
        local can_update_field = {id = true, name = true, type = true }
        local can_update = true
        local cant_update_fields = ''
        for k,v in pairs(options) do
            if not can_update_field[k] then
                can_update = false
                cant_update_fields = cant_update_fields .. ' ' .. k
            end
        end
        if not can_update then
            box.error(box.error.PROC_LUA,
                      "Don't know how to update both id and" ..
                       cant_update_fields)
        end
        ops = {}
        local function add_op(value, field_no)
            if value then
                table.insert(ops, {'=', field_no, value})
            end
        end
        add_op(options.id, 2)
        add_op(options.name, 3)
        add_op(options.type, 4)
        _index:update({space_id, index_id}, ops)
        return
    end
    local tuple = _index:get{space_id, index_id }
    local parts = {}
    local key_opts = {}
    local OPTS = 5
    local PARTS = 6
    if type(tuple[OPTS]) == 'number' then
        -- old format
        key_opts.unique = tuple[OPTS] == 1
        local part_count = tuple[PARTS]
        for i = 1, part_count do
            table.insert(parts, {tuple[2 * i + 4], tuple[2 * i + 5]});
        end
    else
        -- new format
        key_opts = tuple[OPTS]
        parts = tuple[PARTS]
    end
    if options.name == nil then
        options.name = tuple[3]
    end
    if options.type == nil then
        options.type = tuple[4]
    end
    if options.unique ~= nil then
        key_opts.unique = options.unique and true or false
    end
    if options.dimension ~= nil then
        key_opts.dimension = options.dimension
    end
    if options.distance ~= nil then
        key_opts.distance = options.distance
    end
    if options.parts ~= nil then
        check_index_parts(options.parts)
        options.parts = update_index_parts(options.parts)
        parts = {}
        for i = 1, #options.parts, 2 do
            table.insert(parts, {options.parts[i], options.parts[i + 1]})
        end
    end
    _index:replace{space_id, index_id, options.name, options.type,
                   key_opts, parts}
end

-- a static box_tuple_t ** instance for calling box_index_* API
local ptuple = ffi.new('box_tuple_t *[1]')

local function keify(key)
    if key == nil then
        return {}
    end
    if type(key) == "table" then
        return key
    end
    return {key}
end

local iterator_t = ffi.typeof('struct iterator')
ffi.metatype(iterator_t, {
    __tostring = function(iterator)
        return "<iterator state>"
    end;
})

local iterator_gen = function(param, state)
    --[[
        index:pairs() mostly confirms to the Lua for-in loop conventions and
        tries to follow the best practices of Lua community.

        - this generating function is stateless.

        - *param* should contain **immutable** data needed to fully define
          an iterator. *param* is opaque for users. Currently it contains keybuf
          string just to prevent GC from collecting it. In future some other
          variables like space_id, index_id, sc_version will be stored here.

        - *state* should contain **immutable** transient state of an iterator.
          *state* is opaque for users. Currently it contains `struct iterator`
          cdata that is modified during iteration. This is a sad limitation of
          underlying C API. Moreover, the separation of *param* and *state* is
          not properly implemented here. These drawbacks can be fixed in
          future without changing this API.

        Please checkout http://www.lua.org/pil/7.3.html for the further
        information.
    --]]
    if not ffi.istype(iterator_t, state) then
        error('usage: next(param, state)')
    end
    -- next() modifies state in-place
    if builtin.box_iterator_next(state, ptuple) ~= 0 then
        return box.error() -- error
    elseif ptuple[0] ~= nil then
        return state, box.tuple.bless(ptuple[0]) -- new state, value
    else
        return nil
    end
end

local iterator_gen_luac = function(param, state)
    local tuple = internal.iterator_next(state)
    if tuple ~= nil then
        return state, tuple -- new state, value
    else
        return nil
    end
end

-- global struct port instance to use by select()/get()
local port_buf = ffi.new('struct port_buf')
local port_buf_entry_t = ffi.typeof('struct port_buf_entry')

-- Helper function for nicer error messages
-- in some cases when space object is misused
-- Takes time so should not be used for DML.
local function space_object_check(space)
        if type(space) ~= 'table' then
            space = { name = space }
        end
        local s = box.space[space.id]
        if s == nil then
            box.error(box.error.NO_SUCH_SPACE, space.name)
        end
end

local function check_iterator_type(opts, key_is_nil)
    local itype
    if opts and opts.iterator then
        if type(opts.iterator) == "number" then
            itype = opts.iterator
        elseif type(opts.iterator) == "string" then
            itype = box.index[string.upper(opts.iterator)]
            if itype == nil then
                box.error(box.error.ITERATOR_TYPE, opts.iterator)
            end
        else
            box.error(box.error.ITERATOR_TYPE, tostring(opts.iterator))
        end
    else
        -- Use ALL for {} and nil keys and EQ for other keys
        itype = key_is_nil and box.index.ALL or box.index.EQ
    end
    return itype
end

internal.check_iterator_type = check_iterator_type -- export for net.box

function box.schema.space.bless(space)
    local index_mt = {}
    -- __len and __index
    index_mt.len = function(index)
        local ret = builtin.box_index_len(index.space_id, index.id)
        if ret == -1 then
            box.error()
        end
        return tonumber(ret)
    end
    -- index.bsize
    index_mt.bsize = function(index)
        local ret = builtin.box_index_bsize(index.space_id, index.id)
        if ret == -1 then
            box.error()
        end
        return tonumber(ret)
    end
    index_mt.__len = index_mt.len -- Lua 5.2 compatibility
    index_mt.__newindex = function(table, index)
        return error('Attempt to modify a read-only table') end
    index_mt.__index = index_mt
    -- min and max
    index_mt.min_ffi = function(index, key)
        local pkey, pkey_end = msgpackffi.encode_tuple(key)
        if builtin.box_index_min(index.space_id, index.id,
                                 pkey, pkey_end, ptuple) ~= 0 then
            box.error() -- error
        elseif ptuple[0] ~= nil then
            return box.tuple.bless(ptuple[0])
        else
            return
        end
    end
    index_mt.min_luac = function(index, key)
        key = keify(key)
        return internal.min(index.space_id, index.id, key);
    end
    index_mt.max_ffi = function(index, key)
        local pkey, pkey_end = msgpackffi.encode_tuple(key)
        if builtin.box_index_max(index.space_id, index.id,
                                 pkey, pkey_end, ptuple) ~= 0 then
            box.error() -- error
        elseif ptuple[0] ~= nil then
            return box.tuple.bless(ptuple[0])
        else
            return
        end
    end
    index_mt.max_luac = function(index, key)
        key = keify(key)
        return internal.max(index.space_id, index.id, key);
    end
    index_mt.random_ffi = function(index, rnd)
        rnd = rnd or math.random()
        if builtin.box_index_random(index.space_id, index.id, rnd,
                                    ptuple) ~= 0 then
            box.error() -- error
        elseif ptuple[0] ~= nil then
            return box.tuple.bless(ptuple[0])
        else
            return
        end
    end
    index_mt.random_luac = function(index, rnd)
        rnd = rnd or math.random()
        return internal.random(index.space_id, index.id, rnd);
    end
    -- iteration
    index_mt.pairs_ffi = function(index, key, opts)
        local pkey, pkey_end = msgpackffi.encode_tuple(key)
        local itype = check_iterator_type(opts, pkey + 1 >= pkey_end);

        local keybuf = ffi.string(pkey, pkey_end - pkey)
        local pkeybuf = ffi.cast('const char *', keybuf)
        local cdata = builtin.box_index_iterator(index.space_id, index.id,
            itype, pkeybuf, pkeybuf + #keybuf);
        if cdata == nil then
            box.error()
        end
        return fun.wrap(iterator_gen, keybuf,
            ffi.gc(cdata, builtin.box_iterator_free))
    end
    index_mt.pairs_luac = function(index, key, opts)
        key = keify(key)
        local itype = check_iterator_type(opts, #key == 0);
        local keymp = msgpack.encode(key)
        local keybuf = ffi.string(keymp, #keymp)
        local cdata = internal.iterator(index.space_id, index.id, itype, keymp);
        return fun.wrap(iterator_gen_luac, keybuf,
            ffi.gc(cdata, builtin.box_iterator_free))
    end

    -- index subtree size
    index_mt.count_ffi = function(index, key, opts)
        local pkey, pkey_end = msgpackffi.encode_tuple(key)
        local itype = check_iterator_type(opts, pkey + 1 >= pkey_end);
        local count = builtin.box_index_count(index.space_id, index.id,
            itype, pkey, pkey_end);
        if count == -1 then
            box.error()
        end
        return tonumber(count)
    end
    index_mt.count_luac = function(index, key, opts)
        key = keify(key)
        local itype = check_iterator_type(opts, #key == 0);
        return internal.count(index.space_id, index.id, itype, key);
    end

    local function check_index(space, index_id)
        if space.index[index_id] == nil then
            box.error(box.error.NO_SUCH_INDEX, index_id, space.name)
        end
    end

    index_mt.get_ffi = function(index, key)
        local key, key_end = msgpackffi.encode_tuple(key)
        if builtin.box_index_get(index.space_id, index.id,
                                 key, key_end, ptuple) ~= 0 then
            return box.error() -- error
        elseif ptuple[0] ~= nil then
            return box.tuple.bless(ptuple[0])
        else
            return
        end
    end
    index_mt.get_luac = function(index, key)
        key = keify(key)
        return internal.get(index.space_id, index.id, key)
    end

    local function check_select_opts(opts, key_is_nil)
        local offset = 0
        local limit = 4294967295
        local iterator = check_iterator_type(opts, key_is_nil)
        if opts ~= nil then
            if opts.offset ~= nil then
                offset = opts.offset
            end
            if opts.limit ~= nil then
                limit = opts.limit
            end
        end
        return iterator, offset, limit
    end

    index_mt.select_ffi = function(index, key, opts)
        local key, key_end = msgpackffi.encode_tuple(key)
        local iterator, offset, limit = check_select_opts(opts, key + 1 >= key_end)

        builtin.port_buf_create(port_buf)
        if builtin.box_select(port_buf, index.space_id,
            index.id, iterator, offset, limit, key, key_end) ~=0 then
            builtin.port_buf_destroy(port_buf);
            return box.error()
        end

        local ret = {}
        local entry = port_buf.first
        for i=1,tonumber(port_buf.size),1 do
            ret[i] = box.tuple.bless(entry.tuple)
            entry = entry.next
        end
        builtin.port_buf_destroy(port_buf);
        return ret
    end

    index_mt.select_luac = function(index, key, opts)
        local key = keify(key)
        local iterator, offset, limit = check_select_opts(opts, #key == 0)
        return internal.select(index.space_id, index.id, iterator,
            offset, limit, key)
    end

    index_mt.update = function(index, key, ops)
        return internal.update(index.space_id, index.id, keify(key), ops);
    end
    index_mt.upsert = function(index, tuple_key, ops, deprecated)
        if deprecated ~= nil then
            local msg = "Error: extra argument in upsert call: "
            msg = msg .. tostring(deprecated)
            msg = msg .. ". Usage :upsert(tuple, operations)"
            box.error(box.error.PROC_LUA, msg)
        end
        return internal.upsert(index.space_id, index.id, tuple_key, ops);
    end
    index_mt.delete = function(index, key)
        return internal.delete(index.space_id, index.id, keify(key));
    end
    index_mt.drop = function(index)
        return box.schema.index.drop(index.space_id, index.id)
    end
    index_mt.rename = function(index, name)
        return box.schema.index.rename(index.space_id, index.id, name)
    end
    index_mt.alter= function(index, options)
        if index.id == nil or index.space_id == nil then
            box.error(box.error.PROC_LUA, "Usage: index:alter{opts}")
        end
        return box.schema.index.alter(index.space_id, index.id, options)
    end

    -- true if reading operations may yield
    local read_yields = space.engine == 'sophia'
    local read_ops = {'select', 'get', 'min', 'max', 'count', 'random', 'pairs'}
    for _, op in ipairs(read_ops) do
        if read_yields then
            -- use Lua/C implmenetation
            index_mt[op] = index_mt[op .. "_luac"]
        else
            -- use FFI implementation
            index_mt[op] = index_mt[op .. "_ffi"]
        end
    end
    index_mt.__pairs = index_mt.pairs -- Lua 5.2 compatibility
    index_mt.__ipairs = index_mt.pairs -- Lua 5.2 compatibility
    --
    local space_mt = {}
    space_mt.len = function(space)
        if space.index[0] == nil then
            return 0 -- empty space without indexes, return 0
        end
        return space.index[0]:len()
    end
    space_mt.__newindex = index_mt.__newindex

    space_mt.get = function(space, key)
        check_index(space, 0)
        return space.index[0]:get(key)
    end
    space_mt.select = function(space, key, opts)
        check_index(space, 0)
        return space.index[0]:select(key, opts)
    end
    space_mt.insert = function(space, tuple)
        return internal.insert(space.id, tuple);
    end
    space_mt.replace = function(space, tuple)
        return internal.replace(space.id, tuple);
    end
    space_mt.put = space_mt.replace; -- put is an alias for replace
    space_mt.update = function(space, key, ops)
        check_index(space, 0)
        return space.index[0]:update(key, ops)
    end
    space_mt.upsert = function(space, tuple_key, ops, deprecated)
        check_index(space, 0)
        return space.index[0]:upsert(tuple_key, ops, deprecated)
    end
    space_mt.delete = function(space, key)
        check_index(space, 0)
        return space.index[0]:delete(key)
    end
-- Assumes that spaceno has a TREE (NUM) primary key
-- inserts a tuple after getting the next value of the
-- primary key and returns it back to the user
    space_mt.auto_increment = function(space, tuple)
        local max_tuple = space.index[0]:max()
        local max = 0
        if max_tuple ~= nil then
            max = max_tuple[1]
        end
        table.insert(tuple, 1, max + 1)
        return space:insert(tuple)
    end

    --
    -- Increment counter identified by primary key.
    -- Create counter if not exists.
    -- Returns updated value of the counter.
    --
    space_mt.inc = function(space, key)
        local key = keify(key)
        local cnt_index = #key + 1
        local tuple
        while true do
            tuple = space:update(key, {{'+', cnt_index, 1}})
            if tuple ~= nil then break end
            local data = key
            table.insert(data, 1)
            tuple = space:insert(data)
            if tuple ~= nil then break end
        end
        return tuple[cnt_index]
    end

    --
    -- Decrement counter identified by primary key.
    -- Delete counter if it decreased to zero.
    -- Returns updated value of the counter.
    --
    space_mt.dec = function(space, key)
        local key = keify(key)
        local cnt_index = #key + 1
        local tuple = space:get(key)
        if tuple == nil then return 0 end
        if tuple[cnt_index] == 1 then
            space:delete(key)
            return 0
        else
            tuple = space:update(key, {{'-', cnt_index, 1}})
            return tuple[cnt_index]
        end
    end

    space_mt.pairs = function(space, key)
        if space.index[0] == nil then
            -- empty space without indexes, return empty iterator
            return fun.iter({})
        end
        check_index(space, 0)
        return space.index[0]:pairs(key)
    end
    space_mt.__pairs = space_mt.pairs -- Lua 5.2 compatibility
    space_mt.__ipairs = space_mt.pairs -- Lua 5.2 compatibility
    space_mt.truncate = function(space)
        if space.index[0] == nil then
            return -- empty space without indexes, nothing to truncate
        end
        local _index = box.space[box.schema.INDEX_ID]
        -- drop and create all indexes
        local keys = _index:select(space.id)
        for i = #keys, 1, -1 do
            local v = keys[i]
            _index:delete{v[1], v[2]}
        end
        for i = 1, #keys, 1 do
            _index:insert(keys[i])
        end
    end
    space_mt.format = function(space, format)
        return box.schema.space.format(space.id, format)
    end
    space_mt.drop = function(space)
        return box.schema.space.drop(space.id, space.name)
    end
    space_mt.rename = function(space, name)
        space_object_check(space)
        return box.schema.space.rename(space.id, name)
    end
    space_mt.create_index = function(space, name, options)
        space_object_check(space)
        return box.schema.index.create(space.id, name, options)
    end
    space_mt.run_triggers = function(space, yesno)
        local s = builtin.space_by_id(space.id)
        if s == nil then
            box.error(box.error.NO_SUCH_SPACE, space.name)
        end
        builtin.space_run_triggers(s, yesno)
    end
    space_mt.__index = space_mt

    setmetatable(space, space_mt)
    if type(space.index) == 'table' and space.enabled then
        for j, index in pairs(space.index) do
            if type(j) == 'number' then
                setmetatable(index, index_mt)
            end
        end
    end
end

local function privilege_resolve(privilege)
    local numeric = 0
    if type(privilege) == 'string' then
        privilege = string.lower(privilege)
        if string.find(privilege, 'read') then
            numeric = numeric + 1
        end
        if string.find(privilege, 'write') then
            numeric = numeric + 2
        end
        if string.find(privilege, 'execute') then
            numeric = numeric + 4
        end
    else
        numeric = privilege
    end
    return numeric
end

local function checked_privilege(privilege, object_type)
    local priv_hex = privilege_resolve(privilege)
    if object_type == 'role' and priv_hex ~= 4 then
        box.error(box.error.UNSUPPORTED_ROLE_PRIV, privilege)
    end
    return priv_hex
end

local function privilege_name(privilege)
    local names = {}
    if bit.band(privilege, 1) ~= 0 then
        table.insert(names, "read")
    end
    if bit.band(privilege, 2) ~= 0 then
        table.insert(names, "write")
    end
    if bit.band(privilege, 4) ~= 0 then
        table.insert(names, "execute")
    end
    return table.concat(names, ",")
end

local function object_resolve(object_type, object_name)
    if object_type == 'universe' then
        return 0
    end
    if object_type == 'space' then
        local space = box.space[object_name]
        if  space == nil then
            box.error(box.error.NO_SUCH_SPACE, object_name)
        end
        return space.id
    end
    if object_type == 'function' then
        local _func = box.space[box.schema.FUNC_ID]
        local func
        if type(object_name) == 'string' then
            func = _func.index.name:get{object_name}
        else
            func = _func:get{object_name}
        end
        if func then
            return func[1]
        else
            box.error(box.error.NO_SUCH_FUNCTION, object_name)
        end
    end
    if object_type == 'role' then
        local _user = box.space[box.schema.USER_ID]
        local role
        if type(object_name) == 'string' then
            role = _user.index.name:get{object_name}
        else
            role = _user:get{object_name}
        end
        if role and role[4] == 'role' then
            return role[1]
        else
            box.error(box.error.NO_SUCH_ROLE, object_name)
        end
    end

    box.error(box.error.UNKNOWN_SCHEMA_OBJECT, object_type)
end

local function object_name(object_type, object_id)
    if object_type == 'universe' then
        return ""
    end
    local space
    if object_type == 'space' then
        space = box.space._space
    elseif object_type == 'function' then
        space = box.space._func
    elseif object_type == 'role' or object_type == 'user' then
        space = box.space._user
    else
        box.error(box.error.UNKNOWN_SCHEMA_OBJECT, object_type)
    end
    return space:get{object_id}[3]
end

box.schema.func = {}
box.schema.func.create = function(name, opts)
    opts = opts or {}
    check_param_table(opts, { setuid = 'boolean',
                              if_not_exists = 'boolean',
                              language = 'string'})
    local _func = box.space[box.schema.FUNC_ID]
    local func = _func.index.name:get{name}
    if func then
        if not opts.if_not_exists then
            box.error(box.error.FUNCTION_EXISTS, name)
        end
        return
    end
    opts = update_param_table(opts, { setuid = false, type = 'lua'})
    opts.setuid = opts.setuid and 1 or 0
    _func:auto_increment{session.uid(), name, opts.setuid, opts.language}
end

box.schema.func.drop = function(name, opts)
    opts = opts or {}
    check_param_table(opts, { if_exists = 'boolean' })
    local _func = box.space[box.schema.FUNC_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local fid
    local tuple
    if type(name) == 'string' then
        tuple = _func.index.name:get{name}
    else
        tuple = _func:get{name}
    end
    if tuple then
        fid = tuple[1]
    end
    if fid == nil then
        if not opts.if_exists then
            box.error(box.error.NO_SUCH_FUNCTION, name)
        end
        return
    end
    local privs = _priv.index.object:select{'function', fid}
    for k, tuple in pairs(privs) do
        box.schema.user.revoke(tuple[2], tuple[5], tuple[3], tuple[4])
    end
    _func:delete{fid}
end

function box.schema.func.exists(name_or_id)
    local _func = box.space[box.schema.FUNC_ID]
    local tuple = nil
    if type(name_or_id) == 'string' then
        tuple = _func.index.name:get{name_or_id}
    elseif type(name_or_id) == 'number' then
        tuple = _func:get{name_or_id}
    end
    return tuple ~= nil
end

box.schema.user = {}

box.schema.user.password = function(password)
    local BUF_SIZE = 128
    local buf = ffi.new("char[?]", BUF_SIZE)
    builtin.password_prepare(password, #password, buf, BUF_SIZE)
    return ffi.string(buf)
end

local function chpasswd(uid, new_password)
    local _user = box.space[box.schema.USER_ID]
    local auth_mech_list = {}
    auth_mech_list["chap-sha1"] = box.schema.user.password(new_password)
    _user:update({uid}, {{"=", 5, auth_mech_list}})
end

box.schema.user.passwd = function(name, new_password)
    if name == nil then
        box.error(box.error.PROC_LUA, "Usage: box.schema.user.passwd([user,] password)")
    end
    if new_password == nil then
        -- change password for current user
        new_password = name
        box.session.su('admin', chpasswd, session.uid(), new_password)
    else
        -- change password for other user
        local uid = user_resolve(name)
        if uid == nil then
            box.error(box.error.NO_SUCH_USER, name)
        end
        return chpasswd(uid, new_password)
    end
end

box.schema.user.create = function(name, opts)
    local uid = user_or_role_resolve(name)
    opts = opts or {}
    check_param_table(opts, { password = 'string', if_not_exists = 'boolean' })
    if uid then
        if not opts.if_not_exists then
            box.error(box.error.USER_EXISTS, name)
        end
        return
    end
    local auth_mech_list = {}
    if opts.password then
        auth_mech_list["chap-sha1"] = box.schema.user.password(opts.password)
    end
    local _user = box.space[box.schema.USER_ID]
    uid = _user:auto_increment{session.uid(), name, 'user', auth_mech_list}[1]
    -- grant role 'public' to the user
    box.schema.user.grant(uid, 'public')
end

box.schema.user.exists = function(name)
    if user_resolve(name) then
        return true
    else
        return false
    end
end

local function grant(uid, name, privilege, object_type,
                     object_name, options)
    -- From user point of view, role is the same thing
    -- as a privilege. Allow syntax grant(user, role).
    if object_name == nil and object_type == nil then
        -- sic: avoid recursion, to not bother with roles
        -- named 'execute'
        object_type = 'role'
        object_name = privilege
        privilege = 'execute'
    end
    local privilege_hex = checked_privilege(privilege, object_type)

    local oid = object_resolve(object_type, object_name)
    if options == nil then
        options = {}
    end
    if options.grantor == nil then
        options.grantor = session.uid()
    else
        options.grantor = user_or_role_resolve(options.grantor)
    end
    if options.if_not_exists == nil then
        options.if_not_exists = false
    end
    local _priv = box.space[box.schema.PRIV_ID]
    -- add the granted privilege to the current set
    local tuple = _priv:get{uid, object_type, oid}
    local old_privilege
    if tuple ~= nil then
        old_privilege = tuple[5]
    else
        old_privilege = 0
    end
    privilege_hex = bit.bor(privilege_hex, old_privilege)
    -- do not execute a replace if it does not change anything
    -- XXX bug if we decide to add a grant option: new grantor
    -- replaces the old one, old grantor is lost
    if privilege_hex ~= old_privilege then
        _priv:replace{options.grantor, uid, object_type, oid, privilege_hex}
    elseif options.if_not_exists == false then
            if object_type == 'role' then
                box.error(box.error.ROLE_GRANTED, name, object_name)
            else
                box.error(box.error.PRIV_GRANTED, name, privilege,
                          object_type, object_name)
            end
    end
end

local function revoke(uid, name, privilege, object_type, object_name, options)
    -- From user point of view, role is the same thing
    -- as a privilege. Allow syntax revoke(user, role).
    if object_name == nil and object_type == nil then
        object_type = 'role'
        object_name = privilege
        privilege = 'execute'
    end
    local privilege_hex = checked_privilege(privilege, object_type)

    if options == nil then
        options = {}
    end
    if options.if_exists == nil then
        options.if_exists = false
    end
    local oid = object_resolve(object_type, object_name)
    local _priv = box.space[box.schema.PRIV_ID]
    local tuple = _priv:get{uid, object_type, oid}
    if tuple == nil then
        if options.if_exists then
            return
        end
        if object_type == 'role' then
            box.error(box.error.ROLE_NOT_GRANTED, name, object_name)
        else
            box.error(box.error.PRIV_NOT_GRANTED, name, privilege,
                      object_type, object_name)
        end
    end
    local old_privilege = tuple[5]
    local grantor = tuple[1]
    -- sic:
    -- a user may revoke more than he/she granted
    -- (erroneous user input)
    --
    privilege_hex = bit.band(old_privilege, bit.bnot(privilege_hex))
    if privilege_hex ~= 0 then
        _priv:replace{grantor, uid, object_type, oid, privilege_hex}
    else
        _priv:delete{uid, object_type, oid}
    end
end

local function drop(uid, opts)
    -- recursive delete of user data
    local _priv = box.space[box.schema.PRIV_ID]
    local privs = _priv.index.primary:select{uid}
    for k, tuple in pairs(privs) do
        revoke(uid, uid, tuple[5], tuple[3], tuple[4])
    end
    local spaces = box.space[box.schema.SPACE_ID].index.owner:select{uid}
    for k, tuple in pairs(spaces) do
        box.space[tuple[1]]:drop()
    end
    local funcs = box.space[box.schema.FUNC_ID].index.owner:select{uid}
    for k, tuple in pairs(funcs) do
        box.schema.func.drop(tuple[1])
    end
    -- if this is a role, revoke this role from whoever it was granted to
    local grants = _priv.index.object:select{'role', uid}
    for k, tuple in pairs(grants) do
        revoke(tuple[2], tuple[2], uid)
    end
    box.space[box.schema.USER_ID]:delete{uid}
end

box.schema.user.grant = function(user_name, ...)
    local uid = user_resolve(user_name)
    if uid == nil then
        box.error(box.error.NO_SUCH_USER, user_name)
    end
    return grant(uid, user_name, ...)
end

box.schema.user.revoke = function(user_name, ...)
    local uid = user_resolve(user_name)
    if uid == nil then
        box.error(box.error.NO_SUCH_USER, user_name)
    end
    return revoke(uid, user_name, ...)
end

box.schema.user.drop = function(name, opts)
    opts = opts or {}
    check_param_table(opts, { if_exists = 'boolean' })
    local uid = user_resolve(name)
    if uid == nil then
        box.error(box.error.NO_SUCH_USER, name)
    end
    return drop(uid, opts)
end

local function info(id)
    local _priv = box.space._priv
    local _user = box.space._priv
    local privs = {}
    for _, v in pairs(_priv:select{id}) do
        table.insert(
            privs,
            {privilege_name(v[5]), v[3], object_name(v[3], v[4])}
        )
    end
    return privs
end

box.schema.user.info = function(user_name)
    local uid
    if user_name == nil then
        uid = box.session.uid()
    else
        uid = user_resolve(user_name)
        if uid == nil then
            box.error(box.error.NO_SUCH_USER, user_name)
        end
    end
    return info(uid)
end

box.schema.role = {}

box.schema.role.exists = function(name)
    if role_resolve(name) then
        return true
    else
        return false
    end
end

box.schema.role.create = function(name, opts)
    opts = opts or {}
    check_param_table(opts, { if_not_exists = 'boolean' })
    local uid = user_or_role_resolve(name)
    if uid then
        if not opts.if_not_exists then
            box.error(box.error.ROLE_EXISTS, name)
        end
        return
    end
    local _user = box.space[box.schema.USER_ID]
    _user:auto_increment{session.uid(), name, 'role'}
end

box.schema.role.drop = function(name, opts)
    opts = opts or {}
    check_param_table(opts, { if_exists = 'boolean' })
    local uid = role_resolve(name)
    if uid == nil then
        if not opts.if_exists then
            box.error(box.error.NO_SUCH_ROLE, name)
        end
        return
    end
    return drop(uid)
end
box.schema.role.grant = function(user_name, ...)
    local uid = role_resolve(user_name)
    if uid == nil then
        box.error(box.error.NO_SUCH_ROLE, user_name)
    end
    return grant(uid, user_name, ...)
end
box.schema.role.revoke = function(user_name, ...)
    local uid = role_resolve(user_name)
    if uid == nil then
        box.error(box.error.NO_SUCH_ROLE, user_name)
    end
    return revoke(uid, user_name, ...)
end
box.schema.role.info = function(role_name)
    local rid = role_resolve(role_name)
    if rid == nil then
        box.error(box.error.NO_SUCH_ROLE, role_name)
    end
    return info(rid)
end

--
-- once
--
box.once = function(key, func, ...)
    if type(key) ~= 'string' or type(func) ~= 'function' then
        box.error(box.error.ILLEGAL_PARAMS, "Usage: box.once(key, func, ...)")
    end

    local key = "once"..key
    if box.space._schema:get{key} ~= nil then
        return
    end
    box.space._schema:put{key}
    return func(...)
end

--
-- nice output when typing box.space in admin console
--
box.space = {}

local function box_space_mt(tab)
    local t = {}
    for k,v in pairs(tab) do
        -- skip system spaces and views
        if type(k) == 'string' and #k > 0 and k:sub(1,1) ~= '_' then
            t[k] = { engine = v.engine, temporary = v.temporary }
        end
    end
    return t
end

setmetatable(box.space, { __serialize = box_space_mt })
