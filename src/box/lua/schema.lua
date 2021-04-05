-- schema.lua (internal file)
--
local ffi = require('ffi')
local msgpack = require('msgpack')
local fun = require('fun')
local log = require('log')
local buffer = require('buffer')
local session = box.session
local internal = require('box.internal')
local function setmap(table)
    return setmetatable(table, { __serialize = 'map' })
end

local builtin = ffi.C

-- performance fixup for hot functions
local tuple_encode = box.internal.tuple.encode
local tuple_bless = box.internal.tuple.bless
local is_tuple = box.tuple.is
assert(tuple_encode ~= nil and tuple_bless ~= nil and is_tuple ~= nil)
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

local INT64_MIN = tonumber64('-9223372036854775808')
local INT64_MAX = tonumber64('9223372036854775807')

ffi.cdef[[
    struct space *space_by_id(uint32_t id);
    extern uint32_t box_schema_version();
    void space_run_triggers(struct space *space, bool yesno);
    size_t space_bsize(struct space *space);

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
    ssize_t
    box_index_len(uint32_t space_id, uint32_t index_id);
    ssize_t
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
    bool
    box_txn();
    int64_t
    box_txn_id();
    int
    box_txn_begin();
    /** \endcond public */
    /** \cond public */
    int
    box_sequence_current(uint32_t seq_id, int64_t *result);
    /** \endcond public */
    typedef struct txn_savepoint box_txn_savepoint_t;

    box_txn_savepoint_t *
    box_txn_savepoint();

    struct port_c_entry {
        struct port_c_entry *next;
        union {
            struct tuple *tuple;
            char *mp;
        };
        uint32_t mp_size;
    };

    struct port_c {
        const struct port_vtab *vtab;
        struct port_c_entry *first;
        struct port_c_entry *last;
        struct port_c_entry first_entry;
        int size;
    };

    void
    port_destroy(struct port *port);

    int
    box_select(uint32_t space_id, uint32_t index_id,
               int iterator, uint32_t offset, uint32_t limit,
               const char *key, const char *key_end,
               struct port *port);

    void password_prepare(const char *password, int len,
                          char *out, int out_len);

    enum priv_type {
        PRIV_R = 1,
        PRIV_W = 2,
        PRIV_X = 4,
        PRIV_S = 8,
        PRIV_U = 16,
        PRIV_C = 32,
        PRIV_D = 64,
        PRIV_A = 128,
        PRIV_REFERENCE = 256,
        PRIV_TRIGGER = 512,
        PRIV_INSERT = 1024,
        PRIV_UPDATE = 2048,
        PRIV_DELETE = 4096,
        PRIV_GRANT = 8192,
        PRIV_REVOKE = 16384,
        PRIV_ALL  = 4294967295
    };

]]

box.priv = {
    ["R"] = builtin.PRIV_R,
    ["W"] = builtin.PRIV_W,
    ["X"] = builtin.PRIV_X,
    ["S"] = builtin.PRIV_S,
    ["U"] = builtin.PRIV_U,
    ["C"] = builtin.PRIV_C,
    ["D"] = builtin.PRIV_D,
    ["A"] = builtin.PRIV_A,
    ["REFERENCE"] = builtin.PRIV_REFERENCE,
    ["TRIGGER"] = builtin.PRIV_TRIGGER,
    ["INSERT"] = builtin.PRIV_INSERT,
    ["UPDATE"] = builtin.PRIV_UPDATE,
    ["DELETE"] = builtin.PRIV_DELETE,
    ["GRANT"]= builtin.PRIV_GRANT,
    ["REVOKE"] = builtin.PRIV_REVOKE,
    ["ALL"] = builtin.PRIV_ALL
}

local function user_or_role_resolve(user)
    local _vuser = box.space[box.schema.VUSER_ID]
    local tuple
    if type(user) == 'string' then
        tuple = _vuser.index.name:get{user}
    else
        tuple = _vuser:get{user}
    end
    if tuple == nil then
        return nil
    end
    return tuple.id
end

local function role_resolve(name_or_id)
    local _vuser = box.space[box.schema.VUSER_ID]
    local tuple
    if type(name_or_id) == 'string' then
        tuple = _vuser.index.name:get{name_or_id}
    elseif type(name_or_id) ~= 'nil' then
        tuple = _vuser:get{name_or_id}
    end
    if tuple == nil or tuple.type ~= 'role' then
        return nil
    else
        return tuple.id
    end
end

local function user_resolve(name_or_id)
    local _vuser = box.space[box.schema.VUSER_ID]
    local tuple
    if type(name_or_id) == 'string' then
        tuple = _vuser.index.name:get{name_or_id}
    elseif type(name_or_id) ~= 'nil' then
        tuple = _vuser:get{name_or_id}
    end
    if tuple == nil or tuple.type ~= 'user' then
        return nil
    else
        return tuple.id
    end
end

local function sequence_resolve(name_or_id)
    local _vsequence = box.space[box.schema.VSEQUENCE_ID]
    local tuple
    if type(name_or_id) == 'string' then
        tuple = _vsequence.index.name:get{name_or_id}
    elseif type(name_or_id) ~= 'nil' then
        tuple = _vsequence:get{name_or_id}
    end
    if tuple ~= nil then
        return tuple.id, tuple
    else
        return nil
    end
end

-- Revoke all privileges associated with the given object.
local function revoke_object_privs(object_type, object_id)
    local _vpriv = box.space[box.schema.VPRIV_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local privs = _vpriv.index.object:select{object_type, object_id}
    for _, tuple in pairs(privs) do
        local uid = tuple.grantee
        _priv:delete{uid, object_type, object_id}
    end
end

-- Same as type(), but returns 'number' if 'param' is
-- of type 'cdata' and represents a 64-bit integer.
local function param_type(param)
    local t = type(param)
    if t == 'cdata' and tonumber64(param) ~= nil then
        t = 'number'
    end
    return t
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
                              data = 'any'} )
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
                      "unexpected option '" .. k .. "'")
        elseif template[k] == 'any' then -- luacheck: ignore
            -- any type is ok
        elseif (string.find(template[k], ',') == nil) then
            -- one type
            if param_type(v) ~= template[k] then
                box.error(box.error.ILLEGAL_PARAMS,
                          "options parameter '" .. k ..
                          "' should be of type " .. template[k])
            end
        else
            local good_types = string.gsub(template[k], ' ', '')
            local haystack = ',' .. good_types .. ','
            local needle = ',' .. param_type(v) .. ','
            if (string.find(haystack, needle) == nil) then
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
    if param_type(param) ~= should_be_type then
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
    local new_table = {}
    if defaults ~= nil then
        for k,v in pairs(defaults) do
            new_table[k] = v
        end
    end
    if table ~= nil then
        for k,v in pairs(table) do
            new_table[k] = v
        end
    end
    return new_table
end

local function feedback_save_event(event)
    if internal.feedback_daemon ~= nil then
        internal.feedback_daemon.save_event(event)
    end
end

box.begin = function()
    if builtin.box_txn_begin() == -1 then
        box.error()
    end
end

box.is_in_txn = builtin.box_txn

box.savepoint = function()
    local csavepoint = builtin.box_txn_savepoint()
    if csavepoint == nil then
        box.error()
    end
    return { csavepoint=csavepoint, txn_id=builtin.box_txn_id() }
end

local function atomic_tail(status, ...)
    if not status then
        box.rollback()
        error((...), 2)
     end
     box.commit()
     return ...
end

box.atomic = function(fun, ...)
    box.begin()
    return atomic_tail(pcall(fun, ...))
end

-- box.commit yields, so it's defined as Lua/C binding
-- box.rollback and box.rollback_to_savepoint yields as well

local function update_format(format)
    local result = {}
    for i, given in ipairs(format) do
        local field = {}
        if type(given) ~= "table" then
            field.name = given
        else
            for k, v in pairs(given) do
                if k == 1 then
                    if given.name then
                        if not given.type then
                            field.type = v
                        else
                            field[1] = v
                        end
                    else
                        field.name = v
                    end
                elseif k == 2 and not given.type and not given.name then
                    field.type = v
                elseif k == 'collation' then
                    local coll = box.space._collation.index.name:get{v}
                    if not coll then
                        box.error(box.error.ILLEGAL_PARAMS,
                            "format[" .. i .. "]: collation was not found by name '" .. v .. "'")
                    end
                    field[k] = coll.id
                else
                    field[k] = v
                end
            end
        end
        if type(field.name) ~= 'string' then
            box.error(box.error.ILLEGAL_PARAMS,
                "format[" .. i .. "]: name (string) is expected")
        end
        if field.type == nil then
            field.type = 'any'
        elseif type(field.type) ~= 'string' then
            box.error(box.error.ILLEGAL_PARAMS,
                "format[" .. i .. "]: type must be a string")
        end
        table.insert(result, field)
    end
    return result
end

box.schema.space = {}
box.schema.space.create = function(name, options)
    check_param(name, 'name', 'string')
    local options_template = {
        if_not_exists = 'boolean',
        engine = 'string',
        id = 'number',
        field_count = 'number',
        user = 'string, number',
        format = 'table',
        is_local = 'boolean',
        temporary = 'boolean',
        is_sync = 'boolean',
    }
    local options_defaults = {
        engine = 'memtx',
        field_count = 0,
        temporary = false,
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
            id = _space.index.primary:max().id
            if id < box.schema.SYSTEM_ID_MAX then
                id = box.schema.SYSTEM_ID_MAX
            end
            max_id = _schema:insert{'max_id', id + 1}
        end
        id = max_id.value
    end
    local uid = session.euid()
    if options.user then
        uid = user_or_role_resolve(options.user)
        if uid == nil then
            box.error(box.error.NO_SUCH_USER, options.user)
        end
    end
    local format = options.format and options.format or {}
    check_param(format, 'format', 'table')
    format = update_format(format)
    -- filter out global parameters from the options array
    local space_options = setmap({
        group_id = options.is_local and 1 or nil,
        temporary = options.temporary and true or nil,
        is_sync = options.is_sync
    })
    _space:insert{id, uid, name, options.engine, options.field_count,
        space_options, format}

    feedback_save_event('create_space')
    return box.space[id], "created"
end

-- space format - the metadata about space fields
function box.schema.space.format(id, format)
    local _space = box.space._space
    local _vspace = box.space._vspace
    check_param(id, 'id', 'number')

    if format == nil then
        local tuple = _vspace:get(id)
        if tuple == nil then
            box.error(box.error.NO_SUCH_SPACE, '#' .. tostring(id))
        end
        return tuple.format
    else
        check_param(format, 'format', 'table')
        format = update_format(format)
        _space:update(id, {{'=', 7, format}})
    end
end

box.schema.create_space = box.schema.space.create

box.schema.space.drop = function(space_id, space_name, opts)
    check_param(space_id, 'space_id', 'number')
    opts = opts or {}
    check_param_table(opts, { if_exists = 'boolean' })
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _trigger = box.space[box.schema.TRIGGER_ID]
    local _vindex = box.space[box.schema.VINDEX_ID]
    local _truncate = box.space[box.schema.TRUNCATE_ID]
    local _space_sequence = box.space[box.schema.SPACE_SEQUENCE_ID]
    local _fk_constraint = box.space[box.schema.FK_CONSTRAINT_ID]
    local _ck_constraint = box.space[box.schema.CK_CONSTRAINT_ID]
    local _func_index = box.space[box.schema.FUNC_INDEX_ID]
    local sequence_tuple = _space_sequence:delete{space_id}
    if sequence_tuple ~= nil and sequence_tuple.is_generated == true then
        -- Delete automatically generated sequence.
        box.schema.sequence.drop(sequence_tuple.sequence_id)
    end
    for _, t in _trigger.index.space_id:pairs({space_id}) do
        _trigger:delete({t.name})
    end
    for _, t in _fk_constraint.index.child_id:pairs({space_id}) do
        _fk_constraint:delete({t.name, space_id})
    end
    for _, t in _ck_constraint.index.primary:pairs({space_id}) do
        _ck_constraint:delete({space_id, t.name})
    end
    for _, t in _func_index.index.primary:pairs({space_id}) do
        _func_index:delete({space_id, t.index_id})
    end
    local keys = _vindex:select(space_id)
    for i = #keys, 1, -1 do
        local v = keys[i]
        _index:delete{v.id, v.iid}
    end
    revoke_object_privs('space', space_id)
    _truncate:delete{space_id}
    if _space:delete{space_id} == nil then
        if space_name == nil then
            space_name = '#'..tostring(space_id)
        end
        if not opts.if_exists then
            box.error(box.error.NO_SUCH_SPACE, space_name)
        end
    end

    feedback_save_event('drop_space')
end

box.schema.space.rename = function(space_id, space_name)
    check_param(space_id, 'space_id', 'number')
    check_param(space_name, 'space_name', 'string')

    local _space = box.space[box.schema.SPACE_ID]
    _space:update(space_id, {{"=", 3, space_name}})
end

local alter_space_template = {
    field_count = 'number',
    user = 'string, number',
    format = 'table',
    temporary = 'boolean',
    is_sync = 'boolean',
    name = 'string',
}

box.schema.space.alter = function(space_id, options)
    local space = box.space[space_id]
    if not space then
        box.error(box.error.NO_SUCH_SPACE, '#'..tostring(space_id))
    end
    check_param_table(options, alter_space_template)

    local _space = box.space._space
    local tuple = _space:get({space.id})
    assert(tuple ~= nil)

    local owner
    if options.user then
        owner = user_or_role_resolve(options.user)
        if not owner then
            box.error(box.error.NO_SUCH_USER, options.user)
        end
    else
        owner = tuple.owner
    end

    local name = options.name or tuple.name
    local field_count = options.field_count or tuple.field_count
    local flags = tuple.flags

    if options.temporary ~= nil then
        flags.temporary = options.temporary
    end

    if options.is_sync ~= nil then
        flags.is_sync = options.is_sync
    end

    local format
    if options.format ~= nil then
        format = update_format(options.format)
    else
        format = tuple.format
    end

    tuple = tuple:totable()
    tuple[2] = owner
    tuple[3] = name
    tuple[5] = field_count
    tuple[6] = flags
    tuple[7] = format
    _space:replace(tuple)
end

box.schema.index = {}

local function update_index_parts_1_6_0(parts)
    local result = {}
    if #parts % 2 ~= 0 then
        box.error(box.error.ILLEGAL_PARAMS,
                  "options.parts: expected field_no (number), type (string) pairs")
    end
    local i = 0
    for _ in pairs(parts) do
        i = i + 1
        if parts[i] == nil then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts: expected field_no (number), type (string) pairs")
        end
        if i % 2 == 0 then
            goto continue
        end
        if type(parts[i]) ~= "number" then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts: expected field_no (number), type (string) pairs")
        elseif parts[i] == 0 then
            -- Lua uses one-based field numbers but _space is zero-based
            box.error(box.error.ILLEGAL_PARAMS,
                      "invalid index parts: field_no must be one-based")
        end
        if type(parts[i + 1]) ~= "string" then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts: expected field_no (number), type (string) pairs")
        end
        table.insert(result, {field = parts[i] - 1, type = parts[i + 1]})
        ::continue::
    end
    return result
end

--
-- Get field index by format field name.
--
local function format_field_index_by_name(format, name)
    for k, v in pairs(format) do
        if v.name == name then
            return k
        end
    end
    return nil
end

--
-- Get field 0-based index and relative JSON path to data by
-- field 1-based index or full JSON path. A particular case of a
-- full JSON path is the format field name.
--
local function format_field_resolve(format, path, what)
    assert(type(path) == 'number' or type(path) == 'string')
    local idx
    local relative_path = nil
    local field_name
    -- Path doesn't require resolve.
    if type(path) == 'number' then
        idx = path
        goto done
    end
    -- An attempt to interpret a path as the full field name.
    idx = format_field_index_by_name(format, path)
    if idx ~= nil then
        relative_path = nil
        goto done
    end
    -- Check if the initial part of the JSON path is a token of
    -- the form [%d].
    field_name = string.match(path, "^%[(%d+)%]")
    idx = tonumber(field_name)
    if idx ~= nil then
        relative_path = string.sub(path, string.len(field_name) + 3)
        goto done
    end
    -- Check if the initial part of the JSON path is a token of
    -- the form ["%s"] or ['%s'].
    field_name = string.match(path, '^%["([^%]]+)"%]') or
                 string.match(path, "^%['([^%]]+)'%]")
    idx = format_field_index_by_name(format, field_name)
    if idx ~= nil then
        relative_path = string.sub(path, string.len(field_name) + 5)
        goto done
    end
    -- Check if the initial part of the JSON path is a string
    -- token: assume that it ends with .*[ or .*.
    field_name = string.match(path, "^([^.[]+)")
    idx = format_field_index_by_name(format, field_name)
    if idx ~= nil then
        relative_path = string.sub(path, string.len(field_name) + 1)
        goto done
    end
    -- Can't resolve field index by path.
    assert(idx == nil)
    box.error(box.error.ILLEGAL_PARAMS, what .. ": " ..
              "field was not found by name '" .. path .. "'")

::done::
    if idx <= 0 then
        box.error(box.error.ILLEGAL_PARAMS, what .. ": " ..
                  "field (number) must be one-based")
    end
    return idx - 1, relative_path
end

local function update_index_parts(format, parts)
    if type(parts) ~= "table" then
        box.error(box.error.ILLEGAL_PARAMS,
        "options.parts parameter should be a table")
    end
    if #parts == 0 then
        box.error(box.error.ILLEGAL_PARAMS,
        "options.parts must have at least one part")
    end
    if type(parts[1]) == 'number' and
            (parts[2] == nil or type(parts[2]) == 'string') then
        if parts[3] == nil then
            parts = {parts} -- one part only
        else
            return update_index_parts_1_6_0(parts), true
        end
    end

    local parts_can_be_simplified = true
    local result = {}
    local i = 0
    for _ in pairs(parts) do
        i = i + 1
        if parts[i] == nil then
            box.error(box.error.ILLEGAL_PARAMS,
                    "options.parts: unexpected option(s)")
        end
        local part = {}
        if type(parts[i]) ~= "table" then
            part.field = parts[i]
        else
            for k, v in pairs(parts[i]) do
                -- Support {1, 'unsigned', collation='xx'} shortcut
                if k == 1 or k == 'field' then
                    part.field = v;
                elseif k == 2 or k == 'type' then
                    part.type = v;
                elseif k == 'collation' then
                    -- find ID by name
                    local coll = box.space._collation.index.name:get{v}
                    if not coll then
                        coll = box.space._collation.index.name:get{v:lower()}
                    end
                    if not coll then
                        box.error(box.error.ILLEGAL_PARAMS,
                            "options.parts[" .. i .. "]: collation was not found by name '" .. v .. "'")
                    end
                    part[k] = coll[1]
                    parts_can_be_simplified = false
                elseif k == 'is_nullable' then
                    part[k] = v
                    parts_can_be_simplified = false
                elseif k == 'exclude_null' then
                    if type(v) ~= 'boolean' then
                        box.error(box.error.ILLEGAL_PARAMS,
                                "options.parts[" .. i .. "]: " ..
                                "type (boolean) is expected")
                    end
                    part[k] = v
                    parts_can_be_simplified = false
                else
                    part[k] = v
                    parts_can_be_simplified = false
                end
            end
        end
        if type(part.field) == 'number' or type(part.field) == 'string' then
            local idx, path = format_field_resolve(format, part.field,
                                                   "options.parts[" .. i .. "]")
            part.field = idx
            part.path = path or part.path
            parts_can_be_simplified = parts_can_be_simplified and part.path == nil
        else
            box.error(box.error.ILLEGAL_PARAMS, "options.parts[" .. i .. "]: " ..
                      "field (name or number) is expected")
        end
        local fmt = format[part.field + 1]
        if part.type == nil then
            if fmt and fmt.type then
                part.type = fmt.type
            else
                part.type = 'scalar'
            end
        elseif type(part.type) ~= 'string' then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts[" .. i .. "]: type (string) is expected")
        end
        if part.is_nullable == nil then
            if fmt and fmt.is_nullable then
                part.is_nullable = true
                parts_can_be_simplified = false
            end
        elseif type(part.is_nullable) ~= 'boolean' then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts[" .. i .. "]: type (boolean) is expected")
        end
        if (not part.is_nullable) and part.exclude_null then
            if part.is_nullable ~= nil then
                box.error(box.error.ILLEGAL_PARAMS,
                       "options.parts[" .. i .. "]: exclude_null=true and " ..
                       "is_nullable=false are incompatible")
            end
            part.is_nullable = true
        end
        if part.action == nil then
            if fmt and fmt.action ~= nil then
                part.action = fmt.action
                parts_can_be_simplified = false
            end
        end
        if type(parts[i]) == "table" then
            local first_illegal_index = 3
            if parts[i].field ~= nil then
                first_illegal_index = first_illegal_index - 1
            end
            if parts[i].type ~= nil then
                first_illegal_index = first_illegal_index - 1
            end
            if parts[i][first_illegal_index] ~= nil then
                box.error(box.error.ILLEGAL_PARAMS,
                        "options.parts[" .. i .. "]: unexpected option " .. parts[i][first_illegal_index])
            end
        end
        table.insert(result, part)
    end
    return result, parts_can_be_simplified
end

--
-- Convert index parts into 1.6.6 format if they
-- don't use collation, is_nullable and exclude_null options
--
local function simplify_index_parts(parts)
    local new_parts = {}
    for i, part in pairs(parts) do
        assert(part.collation == nil and part.is_nullable == nil
                and part.exclude_null == nil,
               "part is simple")
        new_parts[i] = {part.field, part.type}
    end
    return new_parts
end

--
-- Raise an error if a sequence isn't compatible with a given
-- index definition.
--
local function space_sequence_check(sequence, parts, space_name, index_name)
    local sequence_part = nil
    if sequence.field ~= nil then
        sequence.path = sequence.path or ''
        -- Look up the index part corresponding to the given field.
        for _, part in ipairs(parts) do
            local field = part.field or part[1]
            local path = part.path or ''
            if sequence.field == field and sequence.path == path then
                sequence_part = part
                break
            end
        end
        if sequence_part == nil then
            box.error(box.error.MODIFY_INDEX, index_name, space_name,
                      "sequence field must be a part of the index")
        end
    else
        -- If the sequence field is omitted, use the first
        -- indexed field.
        sequence_part = parts[1]
        sequence.field = sequence_part.field or sequence_part[1]
        sequence.path = sequence_part.path or ''
    end
    -- Check the type of the auto-increment field.
    local t = sequence_part.type or sequence_part[2]
    if t ~= 'integer' and t ~= 'unsigned' then
        box.error(box.error.MODIFY_INDEX, index_name, space_name,
                  "sequence cannot be used with a non-integer key")
    end
end

--
-- The first stage of a space sequence modification operation.
-- Called before altering the space definition. Checks sequence
-- options and detaches the old sequence from the space.
-- Returns a proxy object that is supposed to be passed to
-- space_sequence_alter_commit() to complete the operation.
--
local function space_sequence_alter_prepare(format, parts, options,
                                            space_id, index_id,
                                            space_name, index_name)
    local _space_sequence = box.space[box.schema.SPACE_SEQUENCE_ID]

    -- A sequence can only be attached to a primary index.
    if index_id ~= 0 then
        -- Ignore 'sequence = false' for secondary indexes.
        if not options.sequence then
            return nil
        end
        box.error(box.error.MODIFY_INDEX, index_name, space_name,
                  "sequence cannot be used with a secondary key")
    end

    -- Look up the currently attached sequence, if any.
    local old_sequence
    local tuple = _space_sequence:get(space_id)
    if tuple ~= nil then
        old_sequence = {
            id = tuple.sequence_id,
            is_generated = tuple.is_generated,
            field = tuple.field,
            path = tuple.path,
        }
    else
        old_sequence = nil
    end

    if options.sequence == nil then
        -- No sequence option, just check that the old sequence
        -- is compatible with the new index definition.
        if old_sequence ~= nil and old_sequence.field ~= nil then
            space_sequence_check(old_sequence, parts, space_name, index_name)
        end
        return nil
    end

    -- Convert the provided option to the table format.
    local new_sequence
    if type(options.sequence) == 'table' then
        -- Sequence is given as a table, just copy it.
        -- Silently ignore unknown fields.
        new_sequence = {
            id = options.sequence.id,
            field = options.sequence.field,
        }
    elseif options.sequence == true then
        -- Create an auto-generated sequence.
        new_sequence = {}
    elseif options.sequence == false then
        -- Drop the currently attached sequence.
        new_sequence = nil
    else
        -- Attach a sequence with the given id.
        new_sequence = {id = options.sequence}
    end

    if new_sequence ~= nil then
        -- Resolve the sequence name.
        if new_sequence.id ~= nil then
            local id = sequence_resolve(new_sequence.id)
            if id == nil then
                box.error(box.error.NO_SUCH_SEQUENCE, new_sequence.id)
            end
            local tuple = _space_sequence.index.sequence:select(id)[1]
            if tuple ~= nil and tuple.is_generated then
                box.error(box.error.ALTER_SPACE, space_name,
                          "can not attach generated sequence")
            end
            new_sequence.id = id
        end
        -- Resolve the sequence field.
        if new_sequence.field ~= nil then
            local field, path = format_field_resolve(format, new_sequence.field,
                                                     "sequence field")
            new_sequence.field = field
            new_sequence.path = path
        end
        -- Inherit omitted options from the attached sequence.
        if old_sequence ~= nil then
            if new_sequence.id == nil and old_sequence.is_generated then
                new_sequence.id = old_sequence.id
                new_sequence.is_generated = true
            end
            if new_sequence.field == nil then
                new_sequence.field = old_sequence.field
                new_sequence.path = old_sequence.path
            end
        end
        -- Check that the sequence is compatible with
        -- the index definition.
        space_sequence_check(new_sequence, parts, space_name, index_name)
        -- If sequence id is omitted, we are supposed to create
        -- a new auto-generated sequence for the given space.
        if new_sequence.id == nil then
            local seq = box.schema.sequence.create(space_name .. '_seq')
            new_sequence.id = seq.id
            new_sequence.is_generated = true
        end
        new_sequence.is_generated = new_sequence.is_generated or false
    end

    if old_sequence ~= nil then
        -- Detach the old sequence before altering the space.
        _space_sequence:delete(space_id)
    end

    return {
        space_id = space_id,
        new_sequence = new_sequence,
        old_sequence = old_sequence,
    }
end

--
-- The second stage of a space sequence modification operation.
-- Called after altering the space definition. Attaches the sequence
-- to the space and drops the old sequence if required. 'proxy' is
-- an object returned by space_sequence_alter_prepare().
--
local function space_sequence_alter_commit(proxy)
    local _space_sequence = box.space[box.schema.SPACE_SEQUENCE_ID]

    if proxy == nil then
        -- No sequence option, nothing to do.
        return
    end

    local space_id = proxy.space_id
    local old_sequence = proxy.old_sequence
    local new_sequence = proxy.new_sequence

    if new_sequence ~= nil then
        -- Attach the new sequence.
        _space_sequence:insert{space_id, new_sequence.id,
                               new_sequence.is_generated,
                               new_sequence.field, new_sequence.path}
    end

    if old_sequence ~= nil and old_sequence.is_generated and
       (new_sequence == nil or old_sequence.id ~= new_sequence.id) then
        -- Drop automatically generated sequence.
        box.schema.sequence.drop(old_sequence.id)
    end
end

-- Historically, some properties of an index
-- are stored as tuple fields, others in a
-- single field containing msgpack map.
-- This is the map.
local index_options = {
    unique = 'boolean',
    dimension = 'number',
    distance = 'string',
    run_count_per_level = 'number',
    run_size_ratio = 'number',
    range_size = 'number',
    page_size = 'number',
    bloom_fpr = 'number',
    func = 'number, string',
    hint = 'boolean',
}

local function jsonpaths_from_idx_parts(parts)
    local paths = {}

    for _, part in pairs(parts) do
        if type(part.path) == 'string' then
            table.insert(paths, part.path)
        end
    end

    return paths
end

local function is_multikey_index(parts)
    for _, path in pairs(jsonpaths_from_idx_parts(parts)) do
        if path:find('[*]', 1, true) then
            return true
        end
    end

    return false
end

--
-- check_param_table() template for alter index,
-- includes all index options.
--
local alter_index_template = {
    id = 'number',
    name = 'string',
    type = 'string',
    parts = 'table',
    sequence = 'boolean, number, string, table',
}
for k, v in pairs(index_options) do
    alter_index_template[k] = v
end

--
-- check_param_table() template for create_index(), includes
-- all index options and if_not_exists specifier
--
local create_index_template = table.deepcopy(alter_index_template)
create_index_template.if_not_exists = "boolean"

-- Find a function id by given function name
local function func_id_by_name(func_name)
    local func = box.space._func.index.name:get(func_name)
    if func == nil then
        box.error(box.error.NO_SUCH_FUNCTION, func_name)
    end
    return func.id
end

box.schema.index.create = function(space_id, name, options)
    check_param(space_id, 'space_id', 'number')
    check_param(name, 'name', 'string')
    check_param_table(options, create_index_template)
    local space = box.space[space_id]
    if not space then
        box.error(box.error.NO_SUCH_SPACE, '#'..tostring(space_id))
    end
    local format = space:format()

    local options_defaults = {
        type = 'tree',
    }
    options = update_param_table(options, options_defaults)
    local type_dependent_defaults = {
        rtree = {parts = { 2, 'array' }, unique = false},
        bitset = {parts = { 2, 'unsigned' }, unique = false},
        other = {parts = { 1, 'unsigned' }, unique = true},
    }
    options_defaults = type_dependent_defaults[options.type]
            or type_dependent_defaults.other
    if not options.parts then
        local fieldno = options_defaults.parts[1]
        if #format >= fieldno then
            local t = format[fieldno].type
            if t ~= 'any' then
                options.parts = {{fieldno, format[fieldno].type}}
            end
        end
    end
    options = update_param_table(options, options_defaults)
    if space.engine == 'vinyl' then
        options_defaults = {
            page_size = box.cfg.vinyl_page_size,
            range_size = box.cfg.vinyl_range_size,
            run_count_per_level = box.cfg.vinyl_run_count_per_level,
            run_size_ratio = box.cfg.vinyl_run_size_ratio,
            bloom_fpr = box.cfg.vinyl_bloom_fpr
        }
    else
        options_defaults = {}
    end
    options = update_param_table(options, options_defaults)
    if options.hint and
            (options.type ~= 'tree' or box.space[space_id].engine ~= 'memtx') then
        box.error(box.error.MODIFY_INDEX, name, space.name,
                "hint is only reasonable with memtx tree index")
    end
    if options.hint and options.func then
        box.error(box.error.MODIFY_INDEX, name, space.name,
                "functional index can't use hints")
    end

    local _index = box.space[box.schema.INDEX_ID]
    local _vindex = box.space[box.schema.VINDEX_ID]
    if _vindex.index.name:get{space_id, name} then
        if options.if_not_exists then
            return space.index[name], "not created"
        else
            box.error(box.error.INDEX_EXISTS, name)
        end
    end

    local iid = 0
    if options.id then
        iid = options.id
    else
        -- max
        local tuple = _vindex.index[0]
            :select(space_id, { limit = 1, iterator = 'LE' })[1]
        if tuple then
            local id = tuple.id
            if id == space_id then
                iid = tuple.iid + 1
            end
        end
    end
    local parts, parts_can_be_simplified =
        update_index_parts(format, options.parts)
    -- create_index() options contains type, parts, etc,
    -- stored separately. Remove these members from index_opts
    local index_opts = {
            dimension = options.dimension,
            unique = options.unique,
            distance = options.distance,
            page_size = options.page_size,
            range_size = options.range_size,
            run_count_per_level = options.run_count_per_level,
            run_size_ratio = options.run_size_ratio,
            bloom_fpr = options.bloom_fpr,
            func = options.func,
            hint = options.hint,
    }
    local field_type_aliases = {
        num = 'unsigned'; -- Deprecated since 1.7.2
        uint = 'unsigned';
        str = 'string';
        int = 'integer';
        ['*'] = 'any';
    };
    for _, part in pairs(parts) do
        local field_type = part.type:lower()
        part.type = field_type_aliases[field_type] or field_type
        if field_type == 'num' then
            log.warn("field type '%s' is deprecated since Tarantool 1.7, "..
                     "please use '%s' instead", field_type, part.type)
        end
    end
    -- save parts in old format if possible
    if parts_can_be_simplified then
        parts = simplify_index_parts(parts)
    end
    if options.hint and is_multikey_index(parts) then
        box.error(box.error.MODIFY_INDEX, name, space.name,
                "multikey index can't use hints")
    end
    if index_opts.func ~= nil and type(index_opts.func) == 'string' then
        index_opts.func = func_id_by_name(index_opts.func)
    end
    local sequence_proxy = space_sequence_alter_prepare(format, parts, options,
                                                        space_id, iid,
                                                        space.name, name)
    _index:insert{space_id, iid, name, options.type, index_opts, parts}
    space_sequence_alter_commit(sequence_proxy)
    if index_opts.func ~= nil then
        local _func_index = box.space[box.schema.FUNC_INDEX_ID]
        _func_index:insert{space_id, iid, index_opts.func}
    end

    feedback_save_event('create_index')
    return space.index[name]
end

box.schema.index.drop = function(space_id, index_id)
    check_param(space_id, 'space_id', 'number')
    check_param(index_id, 'index_id', 'number')
    if index_id == 0 then
        local _space_sequence = box.space[box.schema.SPACE_SEQUENCE_ID]
        local sequence_tuple = _space_sequence:delete{space_id}
        if sequence_tuple ~= nil and sequence_tuple.is_generated == true then
            -- Delete automatically generated sequence.
            box.schema.sequence.drop(sequence_tuple.sequence_id)
        end
    end
    local _index = box.space[box.schema.INDEX_ID]
    local _func_index = box.space[box.schema.FUNC_INDEX_ID]
    for _, v in box.space._func_index:pairs{space_id, index_id} do
        _func_index:delete({v.space_id, v.index_id})
    end
    _index:delete{space_id, index_id}

    feedback_save_event('drop_index')
end

box.schema.index.rename = function(space_id, index_id, name)
    check_param(space_id, 'space_id', 'number')
    check_param(index_id, 'index_id', 'number')
    check_param(name, 'name', 'string')

    local _index = box.space[box.schema.INDEX_ID]
    _index:update({space_id, index_id}, {{"=", 3, name}})
end

box.schema.index.alter = function(space_id, index_id, options)
    local space = box.space[space_id]
    if space == nil then
        box.error(box.error.NO_SUCH_SPACE, '#'..tostring(space_id))
    end
    if space.index[index_id] == nil then
        box.error(box.error.NO_SUCH_INDEX_ID, index_id, space.name)
    end
    if options == nil then
        return
    end

    check_param_table(options, alter_index_template)

    if type(space_id) ~= "number" then
        space_id = space.id
    end
    if type(index_id) ~= "number" then
        index_id = space.index[index_id].id
    end
    local format = space:format()
    local _index = box.space[box.schema.INDEX_ID]
    if options.id ~= nil then
        local can_update_field = {id = true, name = true, type = true }
        local can_update = true
        local cant_update_fields = ''
        for k, _ in pairs(options) do
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
        local ops = {}
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
    local index_opts = {}
    if type(tuple.opts) == 'number' then
        -- old format
        index_opts.unique = tuple[5] == 1
        local part_count = tuple[6]
        for i = 1, part_count do
            table.insert(parts, {tuple[2 * i + 4], tuple[2 * i + 5]});
        end
    else
        -- new format
        index_opts = tuple.opts
        parts = tuple.parts
    end
    if options.name == nil then
        options.name = tuple.name
    end
    if options.type == nil then
        options.type = tuple.type
    end
    for k, _ in pairs(index_options) do
        if options[k] ~= nil then
            index_opts[k] = options[k]
        end
    end
    if options.hint and
       (options.type ~= 'tree' or box.space[space_id].engine ~= 'memtx') then
        box.error(box.error.MODIFY_INDEX, space.index[index_id].name,
                                          space.name,
            "hint is only reasonable with memtx tree index")
    end
    if options.hint and options.func then
        box.error(box.error.MODIFY_INDEX, space.index[index_id].name,
                                          space.name,
                "functional index can't use hints")
    end
    if options.parts then
        local parts_can_be_simplified
        parts, parts_can_be_simplified =
            update_index_parts(format, options.parts)
        -- save parts in old format if possible
        if parts_can_be_simplified then
            parts = simplify_index_parts(parts)
        end
    end
    if options.hint and is_multikey_index(parts) then
        box.error(box.error.MODIFY_INDEX, space.index[index_id].name,
                                          space.name,
                "multikey index can't use hints")
    end
    if index_opts.func ~= nil and type(index_opts.func) == 'string' then
        index_opts.func = func_id_by_name(index_opts.func)
    end
    local sequence_proxy = space_sequence_alter_prepare(format, parts, options,
                                                        space_id, index_id,
                                                        space.name, options.name)
    _index:replace{space_id, index_id, options.name, options.type,
                   index_opts, parts}
    if index_opts.func ~= nil then
        local _func_index = box.space[box.schema.FUNC_INDEX_ID]
        _func_index:insert{space_id, index_id, index_opts.func}
    end
    space_sequence_alter_commit(sequence_proxy)
end

-- a static box_tuple_t ** instance for calling box_index_* API
local ptuple = ffi.new('box_tuple_t *[1]')

local function keify(key)
    if key == nil then
        return {}
    elseif type(key) == "table" or is_tuple(key) then
        return key
    end
    return {key}
end

local iterator_t = ffi.typeof('struct iterator')
ffi.metatype(iterator_t, {
    __tostring = function(self)
        return "<iterator state>"
    end;
})

local iterator_gen = function(param, state) -- luacheck: no unused args
    --[[
        index:pairs() mostly conforms to the Lua for-in loop conventions and
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

        Please check out http://www.lua.org/pil/7.3.html for details.
    --]]
    if not ffi.istype(iterator_t, state) then
        error('usage: next(param, state)')
    end
    -- next() modifies state in-place
    if builtin.box_iterator_next(state, ptuple) ~= 0 then
        return box.error() -- error
    elseif ptuple[0] ~= nil then
        return state, tuple_bless(ptuple[0]) -- new state, value
    else
        return nil
    end
end

local iterator_gen_luac = function(param, state) -- luacheck: no unused args
    local tuple = internal.iterator_next(state)
    if tuple ~= nil then
        return state, tuple -- new state, value
    else
        return nil
    end
end

-- global struct port instance to use by select()/get()
local port_c = ffi.new('struct port_c')

-- Helper function to check space:method() usage
local function check_space_arg(space, method)
    if type(space) ~= 'table' or space.id == nil then
        local fmt = 'Use space:%s(...) instead of space.%s(...)'
        error(string.format(fmt, method, method))
    end
end
box.internal.check_space_arg = check_space_arg -- for net.box

-- Helper function for nicer error messages
-- in some cases when space object is misused
-- Takes time so should not be used for DML.
local function check_space_exists(space)
    local s = box.space[space.id]
    if s == nil then
        box.error(box.error.NO_SUCH_SPACE, space.name)
    end
end

-- Helper function to check index:method() usage
local function check_index_arg(index, method)
    if type(index) ~= 'table' or index.id == nil then
        local fmt = 'Use index:%s(...) instead of index.%s(...)'
        error(string.format(fmt, method, method))
    end
end
box.internal.check_index_arg = check_index_arg -- for net.box

-- Helper function to check that space have primary key and return it
local function check_primary_index(space)
    local pk = space.index[0]
    if pk == nil then
        box.error(box.error.NO_SUCH_INDEX_ID, 0, space.name)
    end
    return pk
end
box.internal.check_primary_index = check_primary_index -- for net.box

-- Helper function to check ck_constraint:method() usage
local function check_ck_constraint_arg(ck_constraint, method)
    if type(ck_constraint) ~= 'table' or ck_constraint.name == nil then
        local fmt = 'Use ck_constraint:%s(...) instead of ck_constraint.%s(...)'
        error(string.format(fmt, method, method))
    end
end
box.internal.check_ck_constraint_arg = check_ck_constraint_arg

box.internal.schema_version = builtin.box_schema_version

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
    elseif opts and type(opts) == "string" then
        itype = box.index[string.upper(opts)]
        if itype == nil then
            box.error(box.error.ITERATOR_TYPE, opts)
        end
    else
        -- Use ALL for {} and nil keys and EQ for other keys
        itype = key_is_nil and box.index.ALL or box.index.EQ
    end
    return itype
end

internal.check_iterator_type = check_iterator_type -- export for net.box

local base_index_mt = {}
base_index_mt.__index = base_index_mt
--
-- Inherit engine specific index metatables from a base one.
--
local vinyl_index_mt = {}
vinyl_index_mt.__index = vinyl_index_mt
local memtx_index_mt = {}
memtx_index_mt.__index = memtx_index_mt
--
-- When a new method is added below to base index mt, the same
-- method is added both to vinyl and memtx index mt.
--
setmetatable(base_index_mt, {
    __newindex = function(t, k, v)
        vinyl_index_mt[k] = v
        memtx_index_mt[k] = v
        rawset(t, k, v)
    end
})
-- __len and __index
base_index_mt.len = function(index)
    check_index_arg(index, 'len')
    local ret = builtin.box_index_len(index.space_id, index.id)
    if ret == -1 then
        box.error()
    end
    return tonumber(ret)
end
-- index.bsize
base_index_mt.bsize = function(index)
    check_index_arg(index, 'bsize')
    local ret = builtin.box_index_bsize(index.space_id, index.id)
    if ret == -1 then
        box.error()
    end
    return tonumber(ret)
end
-- index.fselect - formatted select.
-- Options can be passed through opts, fselect_opts and global variables.
-- If an option is in opts table or set in global variable - it must have
-- prefix 'fselect_'. If an option is on fselect_opts table - it may or
-- may not have the prefix.
-- Options:
-- type:
--   'sql' - like mysql result (default)
--   'gh' (or 'github' or 'markdown') - markdown syntax, for pasting to github.
--   'jira' syntax (for pasting to jira)
-- widths: array with desired widths of columns.
-- max_width: limit entire length of a row string, longest fields will be cut.
--  Set to 0 (default) to detect and use screen width. Set to -1 for no limit.
-- print: (default - false) - print each line instead of adding to result.
-- use_nbsp: (default - true) - add invisible spaces to improve readability
--  in YAML output. Not applicabble when print=true.
base_index_mt.fselect = function(index, key, opts, fselect_opts)
    -- Options.
    if type(fselect_opts) ~= 'table' then fselect_opts = {} end

    -- Get global value, like _G[name] but wrapped with pcall for strict mode.
    local function get_global(name)
        local function internal() return _G[name] end
        local success,result = pcall(internal)
        return success and result or nil
    end
    -- Get a value from `opts` table and remove it from the table.
    local function grab_from_opts(name)
        if type(opts) ~= 'table' then return nil end
        local res = opts[name]
        if res ~= nil then opts[name] = nil end
        return res
    end
    -- Find an option in opts, fselect_opts or _G by given name.
    -- In opts and _G the value is searched with 'fselect_' prefix;
    -- In fselect_opts - with or without prefix.
    local function get_opt(name, default, expected_type)
        local prefix_name = 'fselect_' .. name
        local variants = {fselect_opts[prefix_name], fselect_opts[name],
            grab_from_opts(prefix_name), get_global(prefix_name), default }
        local min_i = 0
        local min_v = nil
        for i,v in pairs(variants) do
            if (type(v) == expected_type and i < min_i) or min_v == nil then
                min_i = i
                min_v = v
            end
        end
        return min_v
    end

    local fselect_type = get_opt('type', 'sql', 'string')
    if fselect_type == 'gh' or fselect_type == 'github' then
        fselect_type = 'markdown'
    end
    if fselect_type ~= 'sql' and fselect_type ~= 'markdown' and fselect_type ~= 'jira' then
        fselect_type = 'sql'
    end
    local widths = get_opt('widths', {}, 'table')
    local default_max_width = 0
    if #widths > 0 then default_max_width = -1 end
    local max_width = get_opt('max_width', default_max_width, 'number')
    local use_print = get_opt('print', false, 'boolean')
    local use_nbsp = get_opt('use_nbsp', true, 'boolean')
    local min_col_width = 5
    local max_col_width = 1000
    if use_print then use_nbsp = false end

    -- Screen size autodetection.
    local function detect_width()
        local ffi = require('ffi')
        ffi.cdef('void rl_get_screen_size(int *rows, int *cols);')
        local colsp = ffi.new('int[1]')
        ffi.C.rl_get_screen_size(nil, colsp)
        return colsp[0]
    end
    if max_width == 0 then
        max_width = detect_width()
        -- YAML uses several additinal symbols in output, we should shink line.
        local waste_size = use_print and 0 or 5
        if max_width > waste_size then
            max_width = max_width - waste_size
        else
            max_width = fselect_type == 'sql' and 140 or 260
        end
    end

    -- select and stringify.
    local tab = { }
    local json = require('json')
    for _,t in index:pairs(key, opts) do
        local row = { }
        for _,f in t:pairs() do
            table.insert(row, json.encode(f))
        end
        table.insert(tab, row)
    end

    local num_rows = #tab
    local space = box.space[index.space_id]
    local fmt = space:format()
    local num_cols = math.max(#fmt, 1)
    for i = 1,num_rows do
        num_cols = math.max(num_cols, #tab[i])
    end

    local names = {}
    for j = 1,num_cols do
        table.insert(names, fmt[j] and fmt[j].name or 'col' .. tostring(j))
    end
    local real_width = num_cols + 1 -- including '|' symbols
    for j = 1,num_cols do
        if type(widths[j]) ~= 'number' then
            local width = names[j]:len()
            if fselect_type == 'jira' then
                width = width + 1
            end
            for i = 1,num_rows do
                if tab[i][j] then
                    width = math.max(width, tab[i][j]:len())
                end
            end
            widths[j] = width
        end
        widths[j] = math.max(widths[j], min_col_width)
        widths[j] = math.min(widths[j], max_col_width)
        real_width = real_width + widths[j]
    end

    -- cut some columns if its width is too big
    while max_width > 0 and real_width > max_width do
        local max_j = 1
        for j = 2,num_cols do
            if widths[j] >= widths[max_j] then max_j = j end
        end
        widths[max_j] = widths[max_j] - 1
        real_width = real_width - 1
    end

    -- Yaml wraps all strings that contain spaces with single quotes, and
    -- does not wrap otherwise. Let's add some invisible spaces to every line
    -- in order to make them similar in output.
    local prefix = string.char(0xE2) .. string.char(0x80) .. string.char(0x8B)
    if not use_nbsp then prefix = '' end

    local header_row_delim = fselect_type == 'jira' and '||' or '|'
    local result_row_delim = '|'
    local delim_row_delim = fselect_type == 'sql' and '+' or '|'

    local delim_row = prefix .. delim_row_delim
    for j = 1,num_cols do
        delim_row = delim_row .. string.rep('-', widths[j]) .. delim_row_delim
    end

    -- format string - cut or fill with spaces to make is exactly n symbols.
    -- also replace spaces with non-break spaces.
    local fmt_str = function(x, n)
        if not x then x = '' end
        local str
        if x:len() <= n then
            local add = n - x:len()
            local addl = math.floor(add/2)
            local addr = math.ceil(add/2)
            str = string.rep(' ', addl) .. x .. string.rep(' ', addr)
        else
            str = x:sub(1, n)
        end
        if use_nbsp then
            -- replace spaces with &nbsp
            return str:gsub("%s", string.char(0xC2) .. string.char(0xA0))
        else
            return str
        end
    end

    local res = {}

    -- insert into res a string with formatted row.
    local res_insert = function(row, is_header)
        local delim = is_header and header_row_delim or result_row_delim
        local str_row = prefix .. delim
        local shrink = fselect_type == 'jira' and is_header and 1 or 0
        for j = 1,num_cols do
            str_row = str_row .. fmt_str(row[j], widths[j] - shrink) .. delim
        end
        table.insert(res, str_row)
    end

    -- format result
    if fselect_type == 'sql' then
        table.insert(res, delim_row)
    end
    res_insert(names, true)
    if fselect_type ~= 'jira' then
        table.insert(res, delim_row)
    end
    for i = 1,num_rows do
        res_insert(tab[i], false)
    end
    if fselect_type == 'sql' then
        table.insert(res, delim_row)
    end
    if use_print then
        for _,line in ipairs(res) do
            print(line)
        end
        return {}
    end
    return res
end
base_index_mt.gselect = function(index, key, opts, fselect_opts)
    if type(fselect_opts) ~= 'table' then fselect_opts = {} end
    fselect_opts['type'] = 'gh'
    return base_index_mt.fselect(index, key, opts, fselect_opts)
end
base_index_mt.jselect = function(index, key, opts, fselect_opts)
    if type(fselect_opts) ~= 'table' then fselect_opts = {} end
    fselect_opts['type'] = 'jira'
    return base_index_mt.fselect(index, key, opts, fselect_opts)
end
-- Lua 5.2 compatibility
base_index_mt.__len = base_index_mt.len
-- min and max
base_index_mt.min_ffi = function(index, key)
    check_index_arg(index, 'min')
    local ibuf = cord_ibuf_take()
    local pkey, pkey_end = tuple_encode(ibuf, key)
    local nok = builtin.box_index_min(index.space_id, index.id, pkey, pkey_end,
                                      ptuple) ~= 0
    cord_ibuf_put(ibuf)
    if nok then
        box.error() -- error
    elseif ptuple[0] ~= nil then
        return tuple_bless(ptuple[0])
    else
        return
    end
end
base_index_mt.min_luac = function(index, key)
    check_index_arg(index, 'min')
    key = keify(key)
    return internal.min(index.space_id, index.id, key);
end
base_index_mt.max_ffi = function(index, key)
    check_index_arg(index, 'max')
    local ibuf = cord_ibuf_take()
    local pkey, pkey_end = tuple_encode(ibuf, key)
    local nok = builtin.box_index_max(index.space_id, index.id, pkey, pkey_end,
                                      ptuple) ~= 0
    cord_ibuf_put(ibuf)
    if nok then
        box.error() -- error
    elseif ptuple[0] ~= nil then
        return tuple_bless(ptuple[0])
    else
        return
    end
end
base_index_mt.max_luac = function(index, key)
    check_index_arg(index, 'max')
    key = keify(key)
    return internal.max(index.space_id, index.id, key);
end
base_index_mt.random_ffi = function(index, rnd)
    check_index_arg(index, 'random')
    rnd = rnd or math.random()
    if builtin.box_index_random(index.space_id, index.id, rnd,
                                ptuple) ~= 0 then
        box.error() -- error
    elseif ptuple[0] ~= nil then
        return tuple_bless(ptuple[0])
    else
        return
    end
end
base_index_mt.random_luac = function(index, rnd)
    check_index_arg(index, 'random')
    rnd = rnd or math.random()
    return internal.random(index.space_id, index.id, rnd);
end
-- iteration
base_index_mt.pairs_ffi = function(index, key, opts)
    check_index_arg(index, 'pairs')
    local ibuf = cord_ibuf_take()
    local pkey, pkey_end = tuple_encode(ibuf, key)
    local itype = check_iterator_type(opts, pkey + 1 >= pkey_end);

    local keybuf = ffi.string(pkey, pkey_end - pkey)
    cord_ibuf_put(ibuf)
    local pkeybuf = ffi.cast('const char *', keybuf)
    local cdata = builtin.box_index_iterator(index.space_id, index.id,
        itype, pkeybuf, pkeybuf + #keybuf);
    if cdata == nil then
        box.error()
    end
    return fun.wrap(iterator_gen, keybuf,
        ffi.gc(cdata, builtin.box_iterator_free))
end
base_index_mt.pairs_luac = function(index, key, opts)
    check_index_arg(index, 'pairs')
    key = keify(key)
    local itype = check_iterator_type(opts, #key == 0);
    local keymp = msgpack.encode(key)
    local keybuf = ffi.string(keymp, #keymp)
    local cdata = internal.iterator(index.space_id, index.id, itype, keymp);
    return fun.wrap(iterator_gen_luac, keybuf,
        ffi.gc(cdata, builtin.box_iterator_free))
end

-- index subtree size
base_index_mt.count_ffi = function(index, key, opts)
    check_index_arg(index, 'count')
    local ibuf = cord_ibuf_take()
    local pkey, pkey_end = tuple_encode(ibuf, key)
    local itype = check_iterator_type(opts, pkey + 1 >= pkey_end);
    local count = builtin.box_index_count(index.space_id, index.id,
        itype, pkey, pkey_end);
    cord_ibuf_put(ibuf)
    if count == -1 then
        box.error()
    end
    return tonumber(count)
end
base_index_mt.count_luac = function(index, key, opts)
    check_index_arg(index, 'count')
    key = keify(key)
    local itype = check_iterator_type(opts, #key == 0);
    return internal.count(index.space_id, index.id, itype, key);
end

base_index_mt.get_ffi = function(index, key)
    check_index_arg(index, 'get')
    local ibuf = cord_ibuf_take()
    local key, key_end = tuple_encode(ibuf, key)
    local nok = builtin.box_index_get(index.space_id, index.id, key, key_end,
                                      ptuple) ~= 0
    cord_ibuf_put(ibuf)
    if nok then
        return box.error() -- error
    elseif ptuple[0] ~= nil then
        return tuple_bless(ptuple[0])
    else
        return
    end
end
base_index_mt.get_luac = function(index, key)
    check_index_arg(index, 'get')
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

base_index_mt.select_ffi = function(index, key, opts)
    check_index_arg(index, 'select')
    local ibuf = cord_ibuf_take()
    local key, key_end = tuple_encode(ibuf, key)
    local iterator, offset, limit = check_select_opts(opts, key + 1 >= key_end)

    local port = ffi.cast('struct port *', port_c)
    local nok = builtin.box_select(index.space_id, index.id, iterator, offset,
                                   limit, key, key_end, port) ~= 0
    cord_ibuf_put(ibuf)
    if nok then
        return box.error()
    end

    local ret = {}
    local entry = port_c.first
    for i=1,tonumber(port_c.size),1 do
        ret[i] = tuple_bless(entry.tuple)
        entry = entry.next
    end
    builtin.port_destroy(port);
    return ret
end

base_index_mt.select_luac = function(index, key, opts)
    check_index_arg(index, 'select')
    local key = keify(key)
    local iterator, offset, limit = check_select_opts(opts, #key == 0)
    return internal.select(index.space_id, index.id, iterator,
        offset, limit, key)
end

base_index_mt.update = function(index, key, ops)
    check_index_arg(index, 'update')
    return internal.update(index.space_id, index.id, keify(key), ops);
end
base_index_mt.delete = function(index, key)
    check_index_arg(index, 'delete')
    return internal.delete(index.space_id, index.id, keify(key));
end

base_index_mt.stat = function(index)
    return internal.stat(index.space_id, index.id);
end

base_index_mt.compact = function(index)
    return internal.compact(index.space_id, index.id)
end

base_index_mt.drop = function(index)
    check_index_arg(index, 'drop')
    return box.schema.index.drop(index.space_id, index.id)
end
base_index_mt.rename = function(index, name)
    check_index_arg(index, 'rename')
    return box.schema.index.rename(index.space_id, index.id, name)
end
base_index_mt.alter = function(index, options)
    check_index_arg(index, 'alter')
    if index.id == nil or index.space_id == nil then
        box.error(box.error.PROC_LUA, "Usage: index:alter{opts}")
    end
    return box.schema.index.alter(index.space_id, index.id, options)
end

local read_ops = {'select', 'get', 'min', 'max', 'count', 'random', 'pairs'}
for _, op in ipairs(read_ops) do
    vinyl_index_mt[op] = base_index_mt[op..'_luac']
    memtx_index_mt[op] = base_index_mt[op..'_ffi']
end
-- Lua 5.2 compatibility
vinyl_index_mt.__pairs = vinyl_index_mt.pairs
vinyl_index_mt.__ipairs = vinyl_index_mt.pairs
memtx_index_mt.__pairs = memtx_index_mt.pairs
memtx_index_mt.__ipairs = memtx_index_mt.pairs

local space_mt = {}
space_mt.len = function(space)
    check_space_arg(space, 'len')
    local pk = space.index[0]
    if pk == nil then
        return 0 -- empty space without indexes, return 0
    end
    return space.index[0]:len()
end
space_mt.count = function(space, key, opts)
    check_space_arg(space, 'count')
    local pk = space.index[0]
    if pk == nil then
        return 0 -- empty space without indexes, return 0
    end
    return pk:count(key, opts)
end
space_mt.bsize = function(space)
    check_space_arg(space, 'bsize')
    local s = builtin.space_by_id(space.id)
    if s == nil then
        box.error(box.error.NO_SUCH_SPACE, space.name)
    end
    return builtin.space_bsize(s)
end

space_mt.get = function(space, key)
    check_space_arg(space, 'get')
    return check_primary_index(space):get(key)
end
space_mt.select = function(space, key, opts)
    check_space_arg(space, 'select')
    return check_primary_index(space):select(key, opts)
end
space_mt.fselect = function(space, key, opts, fselect_opts)
    check_space_arg(space, 'select')
    return check_primary_index(space):fselect(key, opts, fselect_opts)
end
space_mt.gselect = function(space, key, opts, fselect_opts)
    check_space_arg(space, 'select')
    return check_primary_index(space):gselect(key, opts, fselect_opts)
end
space_mt.jselect = function(space, key, opts, fselect_opts)
    check_space_arg(space, 'select')
    return check_primary_index(space):jselect(key, opts, fselect_opts)
end
space_mt.insert = function(space, tuple)
    check_space_arg(space, 'insert')
    return internal.insert(space.id, tuple);
end
space_mt.replace = function(space, tuple)
    check_space_arg(space, 'replace')
    return internal.replace(space.id, tuple);
end
space_mt.put = space_mt.replace; -- put is an alias for replace
space_mt.update = function(space, key, ops)
    check_space_arg(space, 'update')
    return check_primary_index(space):update(key, ops)
end
space_mt.upsert = function(space, tuple_key, ops, deprecated)
    check_space_arg(space, 'upsert')
    if deprecated ~= nil then
        local msg = "Error: extra argument in upsert call: "
        msg = msg .. tostring(deprecated)
        msg = msg .. ". Usage :upsert(tuple, operations)"
        box.error(box.error.PROC_LUA, msg)
    end
    return internal.upsert(space.id, tuple_key, ops);
end
space_mt.delete = function(space, key)
    check_space_arg(space, 'delete')
    return check_primary_index(space):delete(key)
end
-- Assumes that spaceno has a TREE (NUM) primary key
-- inserts a tuple after getting the next value of the
-- primary key and returns it back to the user
space_mt.auto_increment = function(space, tuple)
    check_space_arg(space, 'auto_increment')
    local max_tuple = check_primary_index(space):max()
    local max = 0
    if max_tuple ~= nil then
        max = max_tuple[1]
    end
    table.insert(tuple, 1, max + 1)
    return space:insert(tuple)
end
-- Manage space ck constraints
space_mt.create_check_constraint = function(space, name, code)
    check_space_arg(space, 'create_constraint')
    if name == nil or code == nil then
        box.error(box.error.PROC_LUA,
                  "Usage: space:create_constraint(name, code)")
    end
    box.space._ck_constraint:insert({space.id, name, false, 'SQL', code, true})
    return space.ck_constraint[name]
end
space_mt.pairs = function(space, key, opts)
    check_space_arg(space, 'pairs')
    local pk = space.index[0]
    if pk == nil then
        -- empty space without indexes, return empty iterator
        return fun.iter({})
    end
    return pk:pairs(key, opts)
end
space_mt.__pairs = space_mt.pairs -- Lua 5.2 compatibility
space_mt.__ipairs = space_mt.pairs -- Lua 5.2 compatibility
space_mt.truncate = function(space)
    check_space_arg(space, 'truncate')
    return internal.truncate(space.id)
end
space_mt.format = function(space, format)
    check_space_arg(space, 'format')
    return box.schema.space.format(space.id, format)
end
space_mt.drop = function(space)
    check_space_arg(space, 'drop')
    check_space_exists(space)
    return box.schema.space.drop(space.id, space.name)
end
space_mt.rename = function(space, name)
    check_space_arg(space, 'rename')
    check_space_exists(space)
    return box.schema.space.rename(space.id, name)
end
space_mt.alter = function(space, options)
    check_space_arg(space, 'alter')
    check_space_exists(space)
    return box.schema.space.alter(space.id, options)
end
space_mt.create_index = function(space, name, options)
    check_space_arg(space, 'create_index')
    check_space_exists(space)
    return box.schema.index.create(space.id, name, options)
end
space_mt.run_triggers = function(space, yesno)
    check_space_arg(space, 'run_triggers')
    local s = builtin.space_by_id(space.id)
    if s == nil then
        box.error(box.error.NO_SUCH_SPACE, space.name)
    end
    builtin.space_run_triggers(s, yesno)
end
space_mt.frommap = box.internal.space.frommap
space_mt.__index = space_mt

local ck_constraint_mt = {}
ck_constraint_mt.drop = function(ck_constraint)
    check_ck_constraint_arg(ck_constraint, 'drop')
    box.space._ck_constraint:delete({ck_constraint.space_id, ck_constraint.name})
end
ck_constraint_mt.enable = function(ck_constraint, yesno)
    check_ck_constraint_arg(ck_constraint, 'enable')
    local s = builtin.space_by_id(ck_constraint.space_id)
    if s == nil then
        box.error(box.error.NO_SUCH_SPACE, tostring(ck_constraint.space_id))
    end
    local t = box.space._ck_constraint:get({ck_constraint.space_id,
                                            ck_constraint.name})
    if t == nil then
        box.error(box.error.NO_SUCH_CONSTRAINT, tostring(ck_constraint.name))
    end
    box.space._ck_constraint:update({ck_constraint.space_id, ck_constraint.name},
                                    {{'=', 6, yesno}})
end

box.schema.index_mt = base_index_mt
box.schema.memtx_index_mt = memtx_index_mt
box.schema.vinyl_index_mt = vinyl_index_mt
box.schema.space_mt = space_mt

--
-- Wrap a global space/index metatable into a space/index local
-- one. Routinely this metatable just indexes the global one. When
-- a user attempts to extend a space or index methods via local
-- space/index metatable instead of from box.schema mt, the local
-- metatable is transformed. Its __index metamethod starts looking
-- up at first in self, and only then into the global mt.
--
local function wrap_schema_object_mt(name)
    local global_mt = box.schema[name]
    local mt = {
        __index = global_mt,
        __ipairs = global_mt.__ipairs,
        __pairs = global_mt.__pairs
    }
    local mt_mt = {}
    mt_mt.__newindex = function(self, k, v)
        mt_mt.__newindex = nil
        mt.__index = function(self, k)
            return mt[k] or box.schema[name][k]
        end
        rawset(mt, k, v)
    end
    setmetatable(mt, mt_mt)
    return mt
end

function box.schema.space.bless(space)
    local index_mt_name
    if space.engine == 'vinyl' then
        index_mt_name = 'vinyl_index_mt'
    else
        index_mt_name = 'memtx_index_mt'
    end
    local space_mt = wrap_schema_object_mt('space_mt')

    setmetatable(space, space_mt)
    if type(space.index) == 'table' and space.enabled then
        for j, index in pairs(space.index) do
            if type(j) == 'number' then
                setmetatable(index, wrap_schema_object_mt(index_mt_name))
            end
        end
        for j, ck_constraint in pairs(space.ck_constraint) do
            if type(j) == 'string' then
                setmetatable(ck_constraint, {__index = ck_constraint_mt})
            end
        end
    end
end

local sequence_mt = {}
sequence_mt.__index = sequence_mt

sequence_mt.next = function(self)
    return internal.sequence.next(self.id)
end

sequence_mt.current = function(self)
    local ai64 = ffi.new('int64_t[1]')
    local rc = builtin.box_sequence_current(self.id, ai64)
    if rc < 0 then
        box.error(box.error.last())
    end
    return ai64[0]
end

sequence_mt.set = function(self, value)
    return internal.sequence.set(self.id, value)
end

sequence_mt.reset = function(self)
    return internal.sequence.reset(self.id)
end

sequence_mt.alter = function(self, opts)
    box.schema.sequence.alter(self.id, opts)
end

sequence_mt.drop = function(self)
    box.schema.sequence.drop(self.id)
end

box.sequence = {}
box.schema.sequence = {}

function box.schema.sequence.bless(seq)
    setmetatable(seq, {__index = sequence_mt})
end

local sequence_options = {
    step = 'number',
    min = 'number',
    max = 'number',
    start = 'number',
    cache = 'number',
    cycle = 'boolean',
}

local create_sequence_options = table.deepcopy(sequence_options)
create_sequence_options.if_not_exists = 'boolean'

local alter_sequence_options = table.deepcopy(sequence_options)
alter_sequence_options.name = 'string'

box.schema.sequence.create = function(name, opts)
    opts = opts or {}
    check_param(name, 'name', 'string')
    check_param_table(opts, create_sequence_options)
    local ascending = not opts.step or opts.step > 0
    local options_defaults = {
        step = 1,
        min = ascending and 1 or INT64_MIN,
        max = ascending and INT64_MAX or -1,
        start = ascending and (opts.min or 1) or (opts.max or -1),
        cache = 0,
        cycle = false,
    }
    opts = update_param_table(opts, options_defaults)
    local id = sequence_resolve(name)
    if id ~= nil then
        if not opts.if_not_exists then
            box.error(box.error.SEQUENCE_EXISTS, name)
        end
        return box.sequence[name], 'not created'
    end
    local _sequence = box.space[box.schema.SEQUENCE_ID]
    _sequence:auto_increment{session.euid(), name, opts.step, opts.min,
                             opts.max, opts.start, opts.cache, opts.cycle}
    return box.sequence[name]
end

box.schema.sequence.alter = function(name, opts)
    check_param_table(opts, alter_sequence_options)
    local id, tuple = sequence_resolve(name)
    if id == nil then
        box.error(box.error.NO_SUCH_SEQUENCE, name)
    end
    if opts == nil then
        return
    end
    local seq = {}
    seq.id, seq.uid, seq.name, seq.step, seq.min, seq.max,
        seq.start, seq.cache, seq.cycle = tuple:unpack()
    opts = update_param_table(opts, seq)
    local _sequence = box.space[box.schema.SEQUENCE_ID]
    _sequence:replace{seq.id, seq.uid, opts.name, opts.step, opts.min,
                      opts.max, opts.start, opts.cache, opts.cycle}
end

box.schema.sequence.drop = function(name, opts)
    opts = opts or {}
    check_param_table(opts, {if_exists = 'boolean'})
    local id = sequence_resolve(name)
    if id == nil then
        if not opts.if_exists then
            box.error(box.error.NO_SUCH_SEQUENCE, name)
        end
        return
    end
    revoke_object_privs('sequence', id)
    local _sequence = box.space[box.schema.SEQUENCE_ID]
    local _sequence_data = box.space[box.schema.SEQUENCE_DATA_ID]
    _sequence_data:delete{id}
    _sequence:delete{id}
end

local function privilege_resolve(privilege)
    local numeric = 0
    if type(privilege) == 'string' then
        privilege = string.lower(privilege)
        if string.find(privilege, 'read') then
            numeric = numeric + box.priv.R
        end
        if string.find(privilege, 'write') then
            numeric = numeric + box.priv.W
        end
        if string.find(privilege, 'execute') then
            numeric = numeric + box.priv.X
        end
        if string.find(privilege, 'session') then
            numeric = numeric + box.priv.S
        end
        if string.find(privilege, 'usage') then
            numeric = numeric + box.priv.U
        end
        if string.find(privilege, 'create') then
            numeric = numeric + box.priv.C
        end
        if string.find(privilege, 'drop') then
            numeric = numeric + box.priv.D
        end
        if string.find(privilege, 'alter') then
            numeric = numeric + box.priv.A
        end
        if string.find(privilege, 'reference') then
            numeric = numeric + box.priv.REFERENCE
        end
        if string.find(privilege, 'trigger') then
            numeric = numeric + box.priv.TRIGGER
        end
        if string.find(privilege, 'insert') then
            numeric = numeric + box.priv.INSERT
        end
        if string.find(privilege, 'update') then
            numeric = numeric + box.priv.UPDATE
        end
        if string.find(privilege, 'delete') then
            numeric = numeric + box.priv.DELETE
        end
    else
        numeric = privilege
    end
    return numeric
end

-- allowed combination of privilege bits for object
local priv_object_combo = {
    ["universe"] = box.priv.ALL,
-- sic: we allow to grant 'execute' on space. This is a legacy
-- bug, please fix it in 2.0
    ["space"]    = bit.bxor(box.priv.ALL, box.priv.S,
                            box.priv.REVOKE, box.priv.GRANT),
    ["sequence"] = bit.bor(box.priv.R, box.priv.W, box.priv.U,
                           box.priv.C, box.priv.A, box.priv.D),
    ["function"] = bit.bor(box.priv.X, box.priv.U,
                           box.priv.C, box.priv.D),
    ["role"]     = bit.bor(box.priv.X, box.priv.U,
                           box.priv.C, box.priv.D),
    ["user"]     = bit.bor(box.priv.C, box.priv.A,
                           box.priv.D),
}

--
-- Resolve privilege hex by name and check
-- that bits are allowed for this object type
--
local function privilege_check(privilege, object_type)
    local priv_hex = privilege_resolve(privilege)
    if priv_object_combo[object_type] == nil then
        box.error(box.error.UNKNOWN_SCHEMA_OBJECT, object_type)
    elseif type(priv_hex) ~= 'number' or priv_hex == 0 or
           bit.band(priv_hex, priv_object_combo[object_type] or 0) ~= priv_hex then
        box.error(box.error.UNSUPPORTED_PRIV, object_type, privilege)
    end
    return priv_hex
end

local function privilege_name(privilege)
    local names = {}
    if bit.band(privilege, box.priv.R) ~= 0 then
        table.insert(names, "read")
    end
    if bit.band(privilege, box.priv.W) ~= 0 then
        table.insert(names, "write")
    end
    if bit.band(privilege, box.priv.X) ~= 0 then
        table.insert(names, "execute")
    end
    if bit.band(privilege, box.priv.S) ~= 0 then
        table.insert(names, "session")
    end
    if bit.band(privilege, box.priv.U) ~= 0 then
        table.insert(names, "usage")
    end
    if bit.band(privilege, box.priv.C) ~= 0 then
        table.insert(names, "create")
    end
    if bit.band(privilege, box.priv.D) ~= 0 then
        table.insert(names, "drop")
    end
    if bit.band(privilege, box.priv.A) ~= 0 then
        table.insert(names, "alter")
    end
    if bit.band(privilege, box.priv.REFERENCE) ~= 0 then
        table.insert(names, "reference")
    end
    if bit.band(privilege, box.priv.TRIGGER) ~= 0 then
        table.insert(names, "trigger")
    end
    if bit.band(privilege, box.priv.INSERT) ~= 0 then
        table.insert(names, "insert")
    end
    if bit.band(privilege, box.priv.UPDATE) ~= 0 then
        table.insert(names, "update")
    end
    if bit.band(privilege, box.priv.DELETE) ~= 0 then
        table.insert(names, "delete")
    end
    return table.concat(names, ",")
end

local function object_resolve(object_type, object_name)
    if object_name ~= nil and type(object_name) ~= 'string'
            and type(object_name) ~= 'number' then
        box.error(box.error.ILLEGAL_PARAMS, "wrong object name type")
    end
    if object_type == 'universe' then
        return 0
    end
    if object_type == 'space' then
        if object_name == '' then
            return ''
        end
        local space = box.space[object_name]
        if  space == nil then
            box.error(box.error.NO_SUCH_SPACE, object_name)
        end
        return space.id
    end
    if object_type == 'function' then
        if object_name == '' then
            return ''
        end
        local _vfunc = box.space[box.schema.VFUNC_ID]
        local func
        if type(object_name) == 'string' then
            func = _vfunc.index.name:get{object_name}
        else
            func = _vfunc:get{object_name}
        end
        if func then
            return func.id
        else
            box.error(box.error.NO_SUCH_FUNCTION, object_name)
        end
    end
    if object_type == 'sequence' then
        if object_name == '' then
            return ''
        end
        local seq = sequence_resolve(object_name)
        if seq == nil then
            box.error(box.error.NO_SUCH_SEQUENCE, object_name)
        end
        return seq
    end
    if object_type == 'role' or object_type == 'user' then
        if object_name == '' then
            return ''
        end
        local _vuser = box.space[box.schema.VUSER_ID]
        local role_or_user
        if type(object_name) == 'string' then
            role_or_user = _vuser.index.name:get{object_name}
        else
            role_or_user = _vuser:get{object_name}
        end
        if role_or_user and role_or_user.type == object_type then
            return role_or_user.id
        elseif object_type == 'role' then
            box.error(box.error.NO_SUCH_ROLE, object_name)
        else
            box.error(box.error.NO_SUCH_USER, object_name)
        end
    end

    box.error(box.error.UNKNOWN_SCHEMA_OBJECT, object_type)
end

local function object_name(object_type, object_id)
    if object_type == 'universe' or object_id == '' then
        return ""
    end
    local space
    if object_type == 'space' then
        space = box.space._vspace
    elseif object_type == 'sequence' then
        space = box.space._sequence
    elseif object_type == 'function' then
        space = box.space._vfunc
    elseif object_type == 'role' or object_type == 'user' then
        space = box.space._vuser
    else
        box.error(box.error.UNKNOWN_SCHEMA_OBJECT, object_type)
    end
    return space:get{object_id}.name
end

box.schema.func = {}
box.schema.func.create = function(name, opts)
    opts = opts or {}
    check_param_table(opts, { setuid = 'boolean',
                              if_not_exists = 'boolean',
                              language = 'string', body = 'string',
                              is_deterministic = 'boolean',
                              is_sandboxed = 'boolean', comment = 'string',
                              param_list = 'table', returns = 'string',
                              exports = 'table', opts = 'table' })
    local _func = box.space[box.schema.FUNC_ID]
    local _vfunc = box.space[box.schema.VFUNC_ID]
    local func = _vfunc.index.name:get{name}
    if func then
        if not opts.if_not_exists then
            box.error(box.error.FUNCTION_EXISTS, name)
        end
        return
    end
    local datetime = os.date("%Y-%m-%d %H:%M:%S")
    opts = update_param_table(opts, { setuid = false, language = 'lua',
                    body = '', routine_type = 'function', returns = 'any',
                    param_list = {}, aggregate = 'none', sql_data_access = 'none',
                    is_deterministic = false, is_sandboxed = false,
                    is_null_call = true, exports = {'LUA'}, opts = setmap{},
                    comment = '', created = datetime, last_altered = datetime})
    opts.language = string.upper(opts.language)
    opts.setuid = opts.setuid and 1 or 0
    _func:auto_increment{session.euid(), name, opts.setuid, opts.language,
                         opts.body, opts.routine_type, opts.param_list,
                         opts.returns, opts.aggregate, opts.sql_data_access,
                         opts.is_deterministic, opts.is_sandboxed,
                         opts.is_null_call, opts.exports, opts.opts,
                         opts.comment, opts.created, opts.last_altered}
end

box.schema.func.drop = function(name, opts)
    opts = opts or {}
    check_param_table(opts, { if_exists = 'boolean' })
    local _func = box.space[box.schema.FUNC_ID]
    local _vfunc = box.space[box.schema.VFUNC_ID]
    local fid
    local tuple
    if type(name) == 'string' then
        tuple = _vfunc.index.name:get{name}
    else
        tuple = _vfunc:get{name}
    end
    if tuple then
        fid = tuple.id
    end
    if fid == nil then
        if not opts.if_exists then
            box.error(box.error.NO_SUCH_FUNCTION, name)
        end
        return
    end
    revoke_object_privs('function', fid)
    _func:delete{fid}
end

function box.schema.func.exists(name_or_id)
    local _vfunc = box.space[box.schema.VFUNC_ID]
    local tuple = nil
    if type(name_or_id) == 'string' then
        tuple = _vfunc.index.name:get{name_or_id}
    elseif type(name_or_id) == 'number' then
        tuple = _vfunc:get{name_or_id}
    end
    return tuple ~= nil
end

-- Helper function to check func:method() usage
local function check_func_arg(func, method)
    if type(func) ~= 'table' or func.name == nil then
        local fmt = 'Use func:%s(...) instead of func.%s(...)'
        error(string.format(fmt, method, method))
    end
end

local func_mt = {}

func_mt.drop = function(func, opts)
    check_func_arg(func, 'drop')
    box.schema.func.drop(func.name, opts)
end

func_mt.call = function(func, args)
    check_func_arg(func, 'call')
    args = args or {}
    if type(args) ~= 'table' then
        error('Use func:call(table)')
    end
    return box.schema.func.call(func.name, unpack(args))
end

function box.schema.func.bless(func)
    setmetatable(func, {__index = func_mt})
end

box.schema.func.reload = internal.module_reload
box.schema.func.call = internal.func_call

box.internal.collation = {}
box.internal.collation.create = function(name, coll_type, locale, opts)
    opts = opts or setmap{}
    if type(name) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS,
        "name (first arg) must be a string")
    end
    if type(coll_type) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS,
        "type (second arg) must be a string")
    end
    if type(locale) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS,
        "locale (third arg) must be a string")
    end
    if type(opts) ~= 'table' then
        box.error(box.error.ILLEGAL_PARAMS,
        "options (fourth arg) must be a table or nil")
    end
    local lua_opts = {if_not_exists = opts.if_not_exists }
    check_param_table(lua_opts, {if_not_exists = 'boolean'})
    opts.if_not_exists = nil
    local collation_defaults = {
        strength = "tertiary",
    }
    opts = update_param_table(opts, collation_defaults)
    opts = setmap(opts)

    local _coll = box.space[box.schema.COLLATION_ID]
    if lua_opts.if_not_exists then
        local coll = _coll.index.name:get{name}
        if coll then
            return
        end
    end
    _coll:auto_increment{name, session.euid(), coll_type, locale, opts}
end

box.internal.collation.drop = function(name, opts)
    opts = opts or {}
    check_param_table(opts, { if_exists = 'boolean' })

    local _coll = box.space[box.schema.COLLATION_ID]
    if opts.if_exists then
        local coll = _coll.index.name:get{name}
        if not coll then
            return
        end
    end
    _coll.index.name:delete{name}
end

box.internal.collation.exists = function(name)
    local _coll = box.space[box.schema.COLLATION_ID]
    local coll = _coll.index.name:get{name}
    return not not coll
end

box.internal.collation.id_by_name = function(name)
    local _coll = box.space[box.schema.COLLATION_ID]
    local coll = _coll.index.name:get{name}
    return coll.id
end

box.schema.user = {}

box.schema.user.password = function(password)
    local BUF_SIZE = 128
    local ibuf = cord_ibuf_take()
    local buf = ibuf:alloc(BUF_SIZE)
    builtin.password_prepare(password, #password, buf, BUF_SIZE)
    buf = ffi.string(buf)
    cord_ibuf_put(ibuf)
    return buf
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
    local auth_mech_list = setmap({})
    if opts.password then
        auth_mech_list["chap-sha1"] = box.schema.user.password(opts.password)
    end
    local _user = box.space[box.schema.USER_ID]
    uid = _user:auto_increment{session.euid(), name, 'user', auth_mech_list}.id
    -- grant role 'public' to the user
    box.schema.user.grant(uid, 'public')
    -- Grant privilege 'alter' on itself, so that it can
    -- change its password or username.
    box.schema.user.grant(uid, 'alter', 'user', uid)
    -- we have to grant global privileges from setuid function, since
    -- only admin has the ownership over universe and we don't have
    -- grant option
    box.session.su('admin', box.schema.user.grant, uid, 'session,usage', 'universe',
                   nil, {if_not_exists=true})
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
    if object_name == nil then
        if object_type == nil then
            -- sic: avoid recursion, to not bother with roles
            -- named 'execute'
            object_type = 'role'
            object_name = privilege
            privilege = 'execute'
        else
            -- Allow syntax grant(user, priv, entity)
            -- for entity grants.
            object_name = ''
        end
    end
    local privilege_hex = privilege_check(privilege, object_type)

    local oid = object_resolve(object_type, object_name)
    options = options or {}
    if options.grantor == nil then
        options.grantor = session.euid()
    else
        options.grantor = user_or_role_resolve(options.grantor)
    end
    local _priv = box.space[box.schema.PRIV_ID]
    local _vpriv = box.space[box.schema.VPRIV_ID]
    -- add the granted privilege to the current set
    local tuple = _vpriv:get{uid, object_type, oid}
    local old_privilege
    if tuple ~= nil then
        old_privilege = tuple.privilege
    else
        old_privilege = 0
    end
    privilege_hex = bit.bor(privilege_hex, old_privilege)
    -- do not execute a replace if it does not change anything
    -- XXX bug if we decide to add a grant option: new grantor
    -- replaces the old one, old grantor is lost
    if privilege_hex ~= old_privilege then
        _priv:replace{options.grantor, uid, object_type, oid, privilege_hex}
    elseif not options.if_not_exists then
            if object_type == 'role' and object_name ~= '' and
               privilege == 'execute' then
                box.error(box.error.ROLE_GRANTED, name, object_name)
            else
                if object_type ~= 'universe' then
                    object_name = string.format(" '%s'", object_name)
                end
                box.error(box.error.PRIV_GRANTED, name, privilege,
                          object_type, object_name)
            end
    end
end

local function revoke(uid, name, privilege, object_type, object_name, options)
    -- From user point of view, role is the same thing
    -- as a privilege. Allow syntax revoke(user, role).
    if object_name == nil then
        if object_type == nil then
            object_type = 'role'
            object_name = privilege
            privilege = 'execute'
        else
            -- Allow syntax revoke(user, privilege, entity)
            -- to revoke entity privileges.
            object_name = ''
        end
    end
    local privilege_hex = privilege_check(privilege, object_type)
    options = options or {}
    local oid = object_resolve(object_type, object_name)
    local _priv = box.space[box.schema.PRIV_ID]
    local _vpriv = box.space[box.schema.VPRIV_ID]
    local tuple = _vpriv:get{uid, object_type, oid}
    -- system privileges of admin and guest can't be revoked
    if tuple == nil then
        if options.if_exists then
            return
        end
        if object_type == 'role' and object_name ~= '' and
           privilege == 'execute' then
            box.error(box.error.ROLE_NOT_GRANTED, name, object_name)
        else
            box.error(box.error.PRIV_NOT_GRANTED, name, privilege,
                      object_type, object_name)
        end
    end
    local old_privilege = tuple.privilege
    local grantor = tuple.grantor
    -- sic:
    -- a user may revoke more than he/she granted
    -- (erroneous user input)
    --
    privilege_hex = bit.band(old_privilege, bit.bnot(privilege_hex))
    -- give an error if we're not revoking anything
    if privilege_hex == old_privilege then
        if options.if_exists then
            return
        end
        box.error(box.error.PRIV_NOT_GRANTED, name, privilege,
                  object_type, object_name)
    end
    if privilege_hex ~= 0 then
        _priv:replace{grantor, uid, object_type, oid, privilege_hex}
    else
        _priv:delete{uid, object_type, oid}
    end
end

local function drop(uid)
    -- recursive delete of user data
    local _vpriv = box.space[box.schema.VPRIV_ID]
    local spaces = box.space[box.schema.VSPACE_ID].index.owner:select{uid}
    for _, tuple in pairs(spaces) do
        box.space[tuple.id]:drop()
    end
    local funcs = box.space[box.schema.VFUNC_ID].index.owner:select{uid}
    for _, tuple in pairs(funcs) do
        box.schema.func.drop(tuple.id)
    end
    -- if this is a role, revoke this role from whoever it was granted to
    local grants = _vpriv.index.object:select{'role', uid}
    for _, tuple in pairs(grants) do
        revoke(tuple.grantee, tuple.grantee, uid)
    end
    local sequences = box.space[box.schema.VSEQUENCE_ID].index.owner:select{uid}
    for _, tuple in pairs(sequences) do
        box.schema.sequence.drop(tuple.id)
    end
    -- xxx: hack, we have to revoke session and usage privileges
    -- of a user using a setuid function in absence of create/drop
    -- privileges and grant option
    if box.space._vuser:get{uid}.type == 'user' then
        box.session.su('admin', box.schema.user.revoke, uid,
                       'session,usage', 'universe', nil, {if_exists = true})
    end
    local privs = _vpriv.index.primary:select{uid}

    for _, tuple in pairs(privs) do
        -- we need an additional box.session.su() here, because of
        -- unnecessary check for privilege PRIV_REVOKE in priv_def_check()
        box.session.su("admin", revoke, uid, uid, tuple.privilege,
                       tuple.object_type, tuple.object_id)
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

box.schema.user.enable = function(user)
    box.schema.user.grant(user, "session,usage", "universe", nil,
                            {if_not_exists = true})
end

box.schema.user.disable = function(user)
    box.schema.user.revoke(user, "session,usage", "universe", nil,
                            {if_exists = true})
end

box.schema.user.drop = function(name, opts)
    opts = opts or {}
    check_param_table(opts, { if_exists = 'boolean' })
    local uid = user_resolve(name)
    if uid ~= nil then
        if uid >= box.schema.SYSTEM_USER_ID_MIN and
           uid <= box.schema.SYSTEM_USER_ID_MAX then
            -- gh-1205: box.schema.user.info fails
            box.error(box.error.DROP_USER, name,
                      "the user or the role is a system")
        end
        if uid == box.session.uid() or uid == box.session.euid() then
            box.error(box.error.DROP_USER, name,
                      "the user is active in the current session")
        end
        return drop(uid)
    end
    if not opts.if_exists then
        box.error(box.error.NO_SUCH_USER, name)
    end
    return
end

local function info(id)
    local _priv = box.space._vpriv
    local privs = {}
    for _, v in pairs(_priv:select{id}) do
        table.insert(
            privs,
            {privilege_name(v.privilege), v.object_type,
             object_name(v.object_type, v.object_id)}
        )
    end
    return privs
end

box.schema.user.info = function(user_name)
    local uid
    if user_name == nil then
        uid = box.session.euid()
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
    _user:auto_increment{session.euid(), name, 'role', setmap({})}
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
    if uid >= box.schema.SYSTEM_USER_ID_MIN and
       uid <= box.schema.SYSTEM_USER_ID_MAX or uid == box.schema.SUPER_ROLE_ID then
        -- gh-1205: box.schema.user.info fails
        box.error(box.error.DROP_USER, name, "the user or the role is a system")
    end
    return drop(uid)
end

local function role_check_grant_revoke_of_sys_priv(priv)
    priv = string.lower(priv)
    if (type(priv) == 'string' and (priv:match("session") or priv:match("usage"))) or
        (type(priv) == "number" and (bit.band(priv, 8) ~= 0 or bit.band(priv, 16) ~= 0)) then
        box.error(box.error.GRANT, "system privilege can not be granted to role")
    end
end

box.schema.role.grant = function(user_name, ...)
    local uid = role_resolve(user_name)
    if uid == nil then
        box.error(box.error.NO_SUCH_ROLE, user_name)
    end
    role_check_grant_revoke_of_sys_priv(...)
    return grant(uid, user_name, ...)
end
box.schema.role.revoke = function(user_name, ...)
    local uid = role_resolve(user_name)
    if uid == nil then
        box.error(box.error.NO_SUCH_ROLE, user_name)
    end
    role_check_grant_revoke_of_sys_priv(...)
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
    box.ctl.wait_rw()
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
            t[k] = {
                engine = v.engine,
                is_local = v.is_local,
                temporary = v.temporary,
                is_sync = v.is_sync,
            }
        end
    end
    return t
end

setmetatable(box.space, { __serialize = box_space_mt })

box.NULL = msgpack.NULL
