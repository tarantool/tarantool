-- schema.lua (internal file)
--
local ffi = require('ffi')
local msgpackffi = require('msgpackffi')
local fun = require('fun')
local session = box.session
local internal = require('box.internal')

local builtin = ffi.C

ffi.cdef[[
    struct space *space_by_id(uint32_t id);
    void space_run_triggers(struct space *space, bool yesno);

    struct iterator {
        struct tuple *(*next)(struct iterator *);
        void (*free)(struct iterator *);
        void (*close)(struct iterator *);
        int sc_version;
        uint32_t space_id;
        uint32_t index_id;
    };
    size_t
    boxffi_index_len(uint32_t space_id, uint32_t index_id);
    struct tuple *
    boxffi_index_random(uint32_t space_id, uint32_t index_id, uint32_t rnd);
    struct iterator *
    boxffi_index_iterator(uint32_t space_id, uint32_t index_id, int type,
                  const char *key);
    struct tuple *
    boxffi_iterator_next(struct iterator *itr);

    struct port;
    struct port_ffi
    {
        struct port_vtab *vtab;
        uint32_t size;
        uint32_t capacity;
        struct tuple **ret;
    };

    void
    port_ffi_create(struct port_ffi *port);
    void
    port_ffi_destroy(struct port_ffi *port);

    int
    boxffi_select(struct port *port, uint32_t space_id, uint32_t index_id,
              int iterator, uint32_t offset, uint32_t limit,
              const char *key, const char *key_end);
    void password_prepare(const char *password, int len,
		                  char *out, int out_len);
    int
    boxffi_txn_begin();

    int
    boxffi_txn_commit();

    void
    boxffi_txn_rollback();
]]

local function user_resolve(user)
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
        table = {}
    end
    if (defaults == nil) then
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
    if ffi.C.boxffi_txn_begin() == -1 then
        box.error()
    end
end
box.commit = function()
    if ffi.C.boxffi_txn_commit() == -1 then
        box.error()
    end
end
box.rollback = ffi.C.boxffi_txn_rollback;

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
        id = _space.index[0]:max()[1]
        if id < box.schema.SYSTEM_ID_MAX then
            id = box.schema.SYSTEM_ID_MAX + 1
        else
            id = id + 1
        end
    end
    local uid = nil
    if options.user then
        uid = user_resolve(options.user)
    end
    if uid == nil then
        uid = session.uid()
    end
    local temporary = options.temporary and "temporary" or ""
    _space:insert{id, uid, name, options.engine, options.field_count, temporary}
    return box.space[id], "created"
end

box.schema.create_space = box.schema.space.create

box.schema.space.drop = function(space_id)
    check_param(space_id, 'space_id', 'number')

    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local keys = _index:select(space_id)
    for i = #keys, 1, -1 do
        local v = keys[i]
        _index:delete{v[1], v[2]}
    end
    local privs = _priv:select{}
    for k, tuple in pairs(privs) do
        if tuple[3] == 'space' and tuple[4] == space_id then
            box.schema.user.revoke(tuple[2], tuple[5], tuple[3], tuple[4])
        end
    end
    if _space:delete{space_id} == nil then
        box.error(box.error.NO_SUCH_SPACE, '#'..tostring(space_id))
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
                  "options.parts: expected filed_no (number), type (string) pairs")
    end
    for i=1,#parts,2 do
        if type(parts[i]) ~= "number" then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts: expected filed_no (number), type (string) pairs")
        elseif parts[i] == 0 then
            -- Lua uses one-based field numbers but _space is zero-based
            box.error(box.error.ILLEGAL_PARAMS,
                      "invalid index parts: field_no must be one-based")
        end
    end
    for i=2,#parts,2 do
        if type(parts[i]) ~= "string" then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts: expected filed_no (number), type (string) pairs")
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
    }
    local options_defaults = {
        type = 'tree',
        parts = { 1, "num" },
        unique = true,
    }
    check_param_table(options, options_template)
    options = update_param_table(options, options_defaults)
    check_index_parts(options.parts)
    options.parts = update_index_parts(options.parts)

    local _index = box.space[box.schema.INDEX_ID]

    local unique = options.unique and 1 or 0
    local part_count = bit.rshift(#options.parts, 1)
    local parts = options.parts
    local iid = 0
    -- max
    local tuple = _index.index[0]
        :select(space_id, { limit = 1, iterator = 'LE' })[1]
    if tuple then
        local id = tuple[1]
        if id == space_id then
            iid = tuple[2] + 1
        end
    end
    if options.id then
        iid = options.id
    end
    _index:insert{space_id, iid, name, options.type,
                  unique, part_count, unpack(options.parts)}
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
    if box.space[space_id].index[index_id] == nil then
        box.error(box.error.NO_SUCH_INDEX, index_id, box.space[space_id].name)
    end
    if options == nil then
        return
    end

    local options_template = {
        type = 'string',
        name = 'string',
        parts = 'table',
        unique = 'boolean',
        id = 'number',
    }
    check_param_table(options, options_template)

    if type(space_id) ~= "number" then
        space_id = box.space[space_id].id
    end
    if type(index_id) ~= "number" then
        index_id = box.space[space_id].index[index_id].id
    end
    local _index = box.space[box.schema.INDEX_ID]
    if options.unique ~= nil then
        options.unique = options.unique and 1 or 0
    end
    if options.id ~= nil then
        if options.parts ~= nil then
            box.error(box.error.PROC_LUA,
                      "Don't know how to update both id and parts")
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
        add_op(options.unique, 5)
        _index:update({space_id, index_id}, ops)
        return
    end
    local tuple = _index:get{space_id, index_id}
    if options.name == nil then
        options.name = tuple[3]
    end
    if options.type == nil then
        options.type = tuple[4]
    end
    if options.unique == nil then
        options.unique = tuple[5]
    end
    if options.parts == nil then
        options.parts = {tuple:slice(6)} -- not part count
    else
        check_index_parts(options.parts)
        options.parts = update_index_parts(options.parts)
    end
    _index:replace{space_id, index_id, options.name, options.type,
                   options.unique, #options.parts/2, unpack(options.parts)}
end

local function keify(key)
    if key == nil then
        return {}
    end
    if type(key) == "table" then
        return key
    end
    return {key}
end

--
-- Change one-based indexing in update commands to zero-based.
--
local function normalize_update_ops(ops)
    for _, op in ipairs(ops) do
        if op[1] == ':' then
            -- fix offset for splice
            if op[3] > 0 then
                op[3] = op[3] - 1
            elseif op[3] == 0 then
                box.error(box.error.SPLICE, op[2], "offset is out of bound")
            end
        end
        if op[2] > 0 then
           op[2] = op[2] - 1
        elseif op[2] == 0 then
           box.error(box.error.NO_SUCH_FIELD, op[2])
        end
    end
    return ops
end
internal.normalize_update_ops = normalize_update_ops -- export for net.box

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
        error('usage gen(param, state)')
    end
    -- next() modifies state in-place
    local tuple = builtin.boxffi_iterator_next(state)
    if tuple ~= nil then
        return state, box.tuple.bless(tuple) -- new state, value
    else
        return nil
    end
end

local iterator_cdata_gc = function(iterator)
    return iterator.free(iterator)
end

-- global struct port instance to use by select()/get()
local port = ffi.new('struct port_ffi')
builtin.port_ffi_create(port)
ffi.gc(port, builtin.port_ffi_destroy)
local port_t = ffi.typeof('struct port *')

function box.schema.space.bless(space)
    local index_mt = {}
    -- __len and __index
    index_mt.len = function(index)
        local ret = builtin.boxffi_index_len(index.space.id, index.id)
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
    index_mt.min = function(index, key)
        if index.type == 'HASH' then
            box.error(box.error.UNSUPPORTED, 'HASH', 'min()')
        end
        local lst = index:select(key, { iterator = 'GE', limit = 1 })[1]
        if lst ~= nil then
            return lst
        else
            return
        end
    end
    index_mt.max = function(index, key)
        if index.type == 'HASH' then
            box.error(box.error.UNSUPPORTED, 'HASH', 'max()')
        end
        local lst = index:select(key, { iterator = 'LE', limit = 1 })[1]
        if lst ~= nil then
            return lst
        else
            return
        end
    end
    index_mt.random = function(index, rnd)
        rnd = rnd or math.random()
        local tuple = builtin.boxffi_index_random(index.space.id, index.id, rnd)
        if tuple == ffi.cast('void *', -1) then
            box.error() -- error
        elseif tuple ~= nil then
            return box.tuple.bless(tuple)
        else
            return nil
        end
    end
    -- iteration
    index_mt.pairs = function(index, key, opts)
        check_param_table(opts, {iterator = 'number,string'});
        local pkey, pkey_end = msgpackffi.encode_tuple(key)
        -- Use ALL for {} and nil keys and EQ for other keys
        local itype = pkey + 1 < pkey_end and box.index.EQ or box.index.ALL

        if opts and opts.iterator then
            if type(opts.iterator) == "number" then
                itype = opts.iterator
            elseif type(opts.iterator) == "string" then
                itype = box.index[string.upper(opts.iterator)]
                if itype == nil then
                    box.error(box.error.ITERATOR_TYPE, opts.iterator)
                end
            end
        end

        local keybuf = ffi.string(pkey, pkey_end - pkey)
        local cdata = builtin.boxffi_index_iterator(index.space.id, index.id,
            itype, keybuf);
        if cdata == nil then
            box.error()
        end

        return fun.wrap(iterator_gen, keybuf, ffi.gc(cdata, iterator_cdata_gc))
    end
    index_mt.__pairs = index_mt.pairs -- Lua 5.2 compatibility
    index_mt.__ipairs = index_mt.pairs -- Lua 5.2 compatibility
    -- index subtree size
    index_mt.count = function(index, key, opts)
        local count = 0
        local iterator

        if opts and opts.iterator ~= nil then
            iterator = opts.iterator
        else
            iterator = 'EQ'
        end

        if key == nil or type(key) == "table" and #key == 0 then
            return index:len()
        end

        local state, tuple
        for state, tuple in index:pairs(key, { iterator = iterator }) do
            count = count + 1
        end
        return count
    end

    local function check_index(space, index_id)
        if space.index[index_id] == nil then
            box.error(box.error.NO_SUCH_INDEX, index_id, space.name)
        end
    end

    index_mt.get = function(index, key)
        local key, key_end = msgpackffi.encode_tuple(key)
        port.size = 0;
        if builtin.boxffi_select(ffi.cast(port_t, port), index.space.id,
           index.id, box.index.EQ, 0, 2, key, key_end) ~=0 then
            return box.error()
        end
        if port.size == 0 then
            return
        elseif port.size == 1 then
            return box.tuple.bless(port.ret[0])
        else
            box.error(box.error.MORE_THAN_ONE_TUPLE)
        end
    end

    index_mt.select = function(index, key, opts)
        local options_template = {
            offset = 'number',
            limit = 'number',
            iterator = 'number,string',
        }
        check_param_table(opts, options_template);

        local options_defaults = {
            offset = 0,
            limit = 4294967295,
            iterator = box.index.EQ,
        }
        local key, key_end = msgpackffi.encode_tuple(key)
        if key_end == key + 1 then -- empty array
            options_defaults.iterator = box.index.ALL
        end
        opts = update_param_table(opts, options_defaults)

        if type(opts.iterator) == "string" then
            local resolved_iter = box.index[string.upper(opts.iterator)]
            if resolved_iter == nil then
                box.error(box.error.ITERATOR_TYPE, opts.iterator);
            end
            opts.iterator = resolved_iter
        end

        port.size = 0;
        local space_id = index.space.id
        if builtin.boxffi_select(ffi.cast(port_t, port), space_id, index.id,
            opts.iterator, opts.offset, opts.limit, key, key_end) ~= 0 then
            return box.error()
        end

        local ret = {}
        for i=0,port.size - 1,1 do
            table.insert(ret, box.tuple.bless(port.ret[i]))
        end
        return ret
    end
    index_mt.update = function(index, key, ops)
        ops = normalize_update_ops(ops)
        return internal.update(index.space.id, index.id, keify(key), ops);
    end
    index_mt.delete = function(index, key)
        return internal.delete(index.space.id, index.id, keify(key));
    end
    index_mt.drop = function(index)
        return box.schema.index.drop(index.space.id, index.id)
    end
    index_mt.rename = function(index, name)
        return box.schema.index.rename(index.space.id, index.id, name)
    end
    index_mt.alter= function(index, options)
        if index.id == nil or index.space == nil then
            box.error(box.error.PROC_LUA, "Usage: index:alter{opts}")
        end
        return box.schema.index.alter(index.space.id, index.id, options)
    end
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
    space_mt.drop = function(space)
        return box.schema.space.drop(space.id)
    end
    space_mt.rename = function(space, name)
        return box.schema.space.rename(space.id, name)
    end
    space_mt.create_index = function(space, name, options)
        return box.schema.index.create(space.id, name, options)
    end
    space_mt.run_triggers = function(space, yesno)
        local space = ffi.C.space_by_id(space.id)
        if space == nil then
            box.error(box.error.NO_SUCH_SPACE, space.name)
        end
        ffi.C.space_run_triggers(space, yesno)
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
    local _func = box.space[box.schema.FUNC_ID]
    local func = _func.index.name:get{name}
    if func then
            box.error(box.error.FUNCTION_EXISTS, name)
    end
    check_param_table(opts, { setuid = 'boolean' })
    opts = update_param_table(opts, { setuid = false })
    opts.setuid = opts.setuid and 1 or 0
    _func:auto_increment{session.uid(), name, opts.setuid}
end

box.schema.func.drop = function(name)
    local _func = box.space[box.schema.FUNC_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local fid = object_resolve('function', name)
    local privs = _priv:select{}
    for k, tuple in pairs(privs) do
        if tuple[3] == 'function' and tuple[4] == fid then
            box.schema.user.revoke(tuple[2], tuple[5], tuple[3], tuple[4])
        end
    end
    _func:delete{fid}
end

box.schema.user = {}

box.schema.user.password = function(password)
    local BUF_SIZE = 128
    local buf = ffi.new("char[?]", BUF_SIZE)
    ffi.C.password_prepare(password, #password, buf, BUF_SIZE)
    return ffi.string(buf)
end

box.schema.user.passwd = function(new_password)
    local uid = session.uid()
    local _user = box.space[box.schema.USER_ID]
    auth_mech_list = {}
    auth_mech_list["chap-sha1"] = box.schema.user.password(new_password)
    box.session.su('admin')
    _user:update({uid}, {{"=", 5, auth_mech_list}})
    box.session.su(uid)
end

box.schema.user.create = function(name, opts)
    local uid = user_resolve(name)
    if uid then
        box.error(box.error.USER_EXISTS, name)
    end
    if opts == nil then
        opts = {}
    end
    auth_mech_list = {}
    if opts.password then
        auth_mech_list["chap-sha1"] = box.schema.user.password(opts.password)
    end
    local _user = box.space[box.schema.USER_ID]
    _user:auto_increment{session.uid(), name, 'user', auth_mech_list}
end

box.schema.user.drop = function(name)
    local uid = user_resolve(name)
    if uid == nil then
        box.error(box.error.NO_SUCH_USER, name)
    end
    -- recursive delete of user data
    local _priv = box.space[box.schema.PRIV_ID]
    local privs = _priv.index.primary:select{uid}
    for k, tuple in pairs(privs) do
        box.schema.user.revoke(uid, tuple[5], tuple[3], tuple[4])
    end
    local spaces = box.space[box.schema.SPACE_ID].index.owner:select{uid}
    for k, tuple in pairs(spaces) do
        box.space[tuple[1]]:drop()
    end
    local funcs = box.space[box.schema.FUNC_ID].index.owner:select{uid}
    for k, tuple in pairs(funcs) do
        box.schema.func.drop(tuple[1])
    end
    box.space[box.schema.USER_ID]:delete{uid}
end

box.schema.user.grant = function(user_name, privilege, object_type,
                                 object_name, grantor)
    local uid = user_resolve(user_name)
    if uid == nil then
        box.error(box.error.NO_SUCH_USER, user_name)
    end
    privilege = privilege_resolve(privilege)
    local oid = object_resolve(object_type, object_name)
    if grantor == nil then
        grantor = session.uid()
    else
        grantor = user_resolve(grantor)
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
    privilege = bit.bor(privilege, old_privilege)
    -- do not execute a replace if it does not change anything
    -- XXX bug: new grantor replaces the old one, old grantor is lost
    if privilege ~= old_privilege then
        _priv:replace{grantor, uid, object_type, oid, privilege}
    end
end

box.schema.user.revoke = function(user_name, privilege, object_type, object_name)
    local uid = user_resolve(user_name)
    if uid == nil then
        box.error(box.error.NO_SUCH_USER, name)
    end
    privilege = privilege_resolve(privilege)
    local oid = object_resolve(object_type, object_name)
    local _priv = box.space[box.schema.PRIV_ID]
    local tuple = _priv:get{uid, object_type, oid}
    if tuple == nil then
        return
    end
    local old_privilege = tuple[5]
    local grantor = tuple[1]
    -- XXX gh-449: the privilege may be removed by someone who did
    -- not grant it
    if privilege ~= old_privilege then
        privilege = bit.band(old_privilege, bit.bnot(privilege))
        _priv:replace{grantor, uid, object_type, oid, privilege}
    else
        _priv:delete{uid, object_type, oid}
    end
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
    local _priv = box.space._priv
    local _user = box.space._priv
    local privs = {}
    for _, v in pairs(_priv:select{uid}) do
        table.insert(privs,
                     {privilege_name(v[5]), v[3], object_name(v[3], v[4])})
    end
    return privs
end

box.schema.role = {}

box.schema.role.create = function(name)
    local uid = user_resolve(name)
    if uid then
        box.error(box.error.ROLE_EXISTS, name)
    end
    local _user = box.space[box.schema.USER_ID]
    _user:auto_increment{session.uid(), name, 'role'}
end

box.schema.role.drop = function(name)
    local uid = user_resolve(name)
    if uid == nil then
        box.error(box.error.NO_SUCH_ROLE, name)
    end
    return box.schema.user.drop(name)
end
box.schema.role.grant = function(user_name, role_name, grantor)
    return box.schema.user.grant(user_name, 'execute', 'role', role_name, grantor)
end
box.schema.role.revoke = function(user_name, role_name)
    return box.schema.user.revoke(user_name, 'execute', 'role', role_name)
end
box.schema.role.info = box.schema.user.info
