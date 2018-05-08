-- schema.lua (internal file)
--
local ffi = require('ffi')
local msgpack = require('msgpack')
local msgpackffi = require('msgpackffi')
local fun = require('fun')
local log = require('log')
local fio = require('fio')
local json = require('json')
local session = box.session
local internal = require('box.internal')
local function setmap(table)
    return setmetatable(table, { __serialize = 'map' })
end

local builtin = ffi.C

-- performance fixup for hot functions
local tuple_encode = box.tuple.encode
local tuple_bless = box.tuple.bless
local is_tuple = box.tuple.is
assert(tuple_encode ~= nil and tuple_bless ~= nil and is_tuple ~= nil)

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
    int64_t
    box_txn_id();
    int
    box_txn_begin();
    /** \endcond public */
    typedef struct txn_savepoint box_txn_savepoint_t;

    box_txn_savepoint_t *
    box_txn_savepoint();

    int
    box_txn_rollback_to_savepoint(box_txn_savepoint_t *savepoint);

    struct port_tuple_entry {
        struct port_tuple_entry *next;
        struct tuple *tuple;
    };

    struct port_tuple {
        const struct port_vtab *vtab;
        size_t size;
        struct port_tuple_entry *first;
        struct port_tuple_entry *last;
        struct port_tuple_entry first_entry;
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

local function sequence_resolve(name_or_id)
    local _sequence = box.space[box.schema.SEQUENCE_ID]
    local tuple
    if type(name_or_id) == 'string' then
        tuple = _sequence.index.name:get{name_or_id}
    elseif type(name_or_id) ~= 'nil' then
        tuple = _sequence:get{name_or_id}
    end
    if tuple ~= nil then
        return tuple[1], tuple
    else
        return nil
    end
end

-- Revoke all privileges associated with the given object.
local function revoke_object_privs(object_type, object_id)
    local _priv = box.space[box.schema.PRIV_ID]
    local privs = _priv.index.object:select{object_type, object_id}
    for k, tuple in pairs(privs) do
        local uid = tuple[2]
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
        elseif template[k] == 'any' then
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

box.begin = function()
    if builtin.box_txn_begin() == -1 then
        box.error()
    end
end

box.savepoint = function()
    local csavepoint = builtin.box_txn_savepoint()
    if csavepoint == nil then
        box.error()
    end
    return { csavepoint=csavepoint, txn_id=builtin.box_txn_id() }
end

local savepoint_type = ffi.typeof('box_txn_savepoint_t')

local function check_savepoint(savepoint)
    if savepoint == nil or savepoint.txn_id == nil or
       savepoint.csavepoint == nil or
       type(tonumber(savepoint.txn_id)) ~= 'number' or
       type(savepoint.csavepoint) ~= 'cdata' or
       not ffi.istype(savepoint_type, savepoint.csavepoint) then
        error("Usage: box.rollback_to_savepoint(savepoint)")
    end
end

box.rollback_to_savepoint = function(savepoint)
    check_savepoint(savepoint)
    if savepoint.txn_id ~= builtin.box_txn_id() then
        box.error(box.error.NO_SUCH_SAVEPOINT)
    end
    if builtin.box_txn_rollback_to_savepoint(savepoint.csavepoint) == -1 then
        box.error()
    end
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
-- box.rollback yields as well

function update_format(format)
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
        temporary = 'boolean',
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
            id = _space.index.primary:max()[1]
            if id < box.schema.SYSTEM_ID_MAX then
                id = box.schema.SYSTEM_ID_MAX
            end
            max_id = _schema:insert{'max_id', id + 1}
        end
        id = max_id[2]
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
        temporary = options.temporary and true or nil,
    })
    _space:insert{id, uid, name, options.engine, options.field_count,
        space_options, format}
    return box.space[id], "created"
end

-- space format - the metadata about space fields
function box.schema.space.format(id, format)
    local _space = box.space._space
    check_param(id, 'id', 'number')

    if format == nil then
        return _space:get(id)[7]
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
    local _truncate = box.space[box.schema.TRUNCATE_ID]
    local _space_sequence = box.space[box.schema.SPACE_SEQUENCE_ID]
    local sequence_tuple = _space_sequence:delete{space_id}
    if sequence_tuple ~= nil and sequence_tuple[3] == true then
        -- Delete automatically generated sequence.
        box.schema.sequence.drop(sequence_tuple[2])
    end
    local keys = _index:select(space_id)
    for i = #keys, 1, -1 do
        local v = keys[i]
        _index:delete{v[1], v[2]}
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
end

box.schema.space.rename = function(space_id, space_name)
    check_param(space_id, 'space_id', 'number')
    check_param(space_name, 'space_name', 'string')

    local _space = box.space[box.schema.SPACE_ID]
    _space:update(space_id, {{"=", 3, space_name}})
end

box.schema.index = {}

local function update_index_parts_1_6_0(parts)
    local result = {}
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
        if type(parts[i + 1]) ~= "string" then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts: expected field_no (number), type (string) pairs")
        end
        table.insert(result, {field = parts[i] - 1, type = parts[i + 1]})
    end
    return result
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
    if type(parts[1]) == 'number' and type(parts[2]) == 'string' then
        return update_index_parts_1_6_0(parts), true
    end

    local parts_can_be_simplified = true
    local result = {}
    for i=1,#parts do
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
                else
                    part[k] = v
                    parts_can_be_simplified = false
                end
            end
        end
        if type(part.field) ~= 'number' and type(part.field) ~= 'string' then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts[" .. i .. "]: field (name or number) is expected")
        elseif type(part.field) == 'string' then
            for k,v in pairs(format) do
                if v.name == part.field then
                    part.field = k
                    break
                end
            end
            if type(part.field) == 'string' then
                box.error(box.error.ILLEGAL_PARAMS,
                          "options.parts[" .. i .. "]: field was not found by name '" .. part.field .. "'")
            end
        elseif part.field == 0 then
            box.error(box.error.ILLEGAL_PARAMS,
                      "options.parts[" .. i .. "]: field (number) must be one-based")
        end
        local fmt = format[part.field]
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
        if part.action == nil then
            if fmt and fmt.action ~= nil then
                part.action = fmt.action
                parts_can_be_simplified = false
            end
        end
        part.field = part.field - 1
        table.insert(result, part)
    end
    return result, parts_can_be_simplified
end

--
-- Convert index parts into 1.6.6 format if they
-- doesn't use collation and is_nullable options
--
local function simplify_index_parts(parts)
    local new_parts = {}
    for i, part in pairs(parts) do
        assert(part.collation == nil and part.is_nullable == nil,
               "part is simple")
        new_parts[i] = {part.field, part.type}
    end
    return new_parts
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
}

--
-- check_param_table() template for alter index,
-- includes all index options.
--
local alter_index_template = {
    id = 'number',
    name = 'string',
    type = 'string',
    parts = 'table',
    sequence = 'boolean, number, string',
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

    local _index = box.space[box.schema.INDEX_ID]
    if _index.index.name:get{space_id, name} then
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
        local tuple = _index.index[0]
            :select(space_id, { limit = 1, iterator = 'LE' })[1]
        if tuple then
            local id = tuple[1]
            if id == space_id then
                iid = tuple[2] + 1
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
    local _space_sequence = box.space[box.schema.SPACE_SEQUENCE_ID]
    local sequence_is_generated = false
    local sequence = options.sequence or nil -- ignore sequence = false
    if sequence ~= nil then
        if iid ~= 0 then
            box.error(box.error.MODIFY_INDEX, name, space.name,
                      "sequence cannot be used with a secondary key")
        end
        if #parts >= 1 and parts[1].type ~= 'integer' and
                           parts[1].type ~= 'unsigned' then
            box.error(box.error.MODIFY_INDEX, name, space.name,
                      "sequence cannot be used with a non-integer key")
        end
        if sequence == true then
            sequence = box.schema.sequence.create(space.name .. '_seq')
            sequence = sequence.id
            sequence_is_generated = true
        else
            sequence = sequence_resolve(sequence)
            if sequence == nil then
                box.error(box.error.NO_SUCH_SEQUENCE, options.sequence)
            end
        end
    end
    -- save parts in old format if possible
    if parts_can_be_simplified then
        parts = simplify_index_parts(parts)
    end
    _index:insert{space_id, iid, name, options.type, index_opts, parts}
    if sequence ~= nil then
        _space_sequence:insert{space_id, sequence, sequence_is_generated}
    end
    return space.index[name]
end

box.schema.index.drop = function(space_id, index_id)
    check_param(space_id, 'space_id', 'number')
    check_param(index_id, 'index_id', 'number')
    if index_id == 0 then
        local _space_sequence = box.space[box.schema.SPACE_SEQUENCE_ID]
        local sequence_tuple = _space_sequence:delete{space_id}
        if sequence_tuple ~= nil and sequence_tuple[3] == true then
            -- Delete automatically generated sequence.
            box.schema.sequence.drop(sequence_tuple[2])
        end
    end
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
    local space = box.space[space_id]
    if space == nil then
        box.error(box.error.NO_SUCH_SPACE, '#'..tostring(space_id))
    end
    if space.index[index_id] == nil then
        box.error(box.error.NO_SUCH_INDEX, index_id, space.name)
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
    local OPTS = 5
    local PARTS = 6
    if type(tuple[OPTS]) == 'number' then
        -- old format
        index_opts.unique = tuple[OPTS] == 1
        local part_count = tuple[PARTS]
        for i = 1, part_count do
            table.insert(parts, {tuple[2 * i + 4], tuple[2 * i + 5]});
        end
    else
        -- new format
        index_opts = tuple[OPTS]
        parts = tuple[PARTS]
    end
    if options.name == nil then
        options.name = tuple[3]
    end
    if options.type == nil then
        options.type = tuple[4]
    end
    for k, t in pairs(index_options) do
        if options[k] ~= nil then
            index_opts[k] = options[k]
        end
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
    local _space_sequence = box.space[box.schema.SPACE_SEQUENCE_ID]
    local sequence_is_generated = false
    local sequence = options.sequence
    local sequence_tuple
    if index_id ~= 0 then
        if sequence then
            box.error(box.error.MODIFY_INDEX, options.name, space.name,
                      "sequence cannot be used with a secondary key")
        end
        -- ignore 'sequence = false' for secondary indexes
        sequence = nil
    else
        sequence_tuple = _space_sequence:get(space_id)
        if (sequence or (sequence ~= false and sequence_tuple ~= nil)) and
           #parts >= 1 and (parts[1].type or parts[1][2]) ~= 'integer' and
                           (parts[1].type or parts[1][2]) ~= 'unsigned' then
            box.error(box.error.MODIFY_INDEX, options.name, space.name,
                      "sequence cannot be used with a non-integer key")
        end
    end
    if sequence == true then
        if sequence_tuple == nil or sequence_tuple[3] == false then
            sequence = box.schema.sequence.create(space.name .. '_seq')
            sequence = sequence.id
            sequence_is_generated = true
        else
            -- Space already has an automatically generated sequence.
            sequence = nil
        end
    elseif sequence then
        sequence = sequence_resolve(sequence)
        if sequence == nil then
            box.error(box.error.NO_SUCH_SEQUENCE, options.sequence)
        end
    end
    if sequence == false then
        _space_sequence:delete(space_id)
    end
    _index:replace{space_id, index_id, options.name, options.type,
                   index_opts, parts}
    if sequence then
        _space_sequence:replace{space_id, sequence, sequence_is_generated}
    end
    if sequence_tuple ~= nil and sequence_tuple[3] == true and
       sequence_tuple[2] ~= sequence then
        -- Delete automatically generated sequence.
        box.schema.sequence.drop(sequence_tuple[2])
    end
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
    __tostring = function(iterator)
        return "<iterator state>"
    end;
})

local iterator_gen = function(param, state)
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

local iterator_gen_luac = function(param, state)
    local tuple = internal.iterator_next(state)
    if tuple ~= nil then
        return state, tuple -- new state, value
    else
        return nil
    end
end

-- global struct port instance to use by select()/get()
local port_tuple = ffi.new('struct port_tuple')
local port_tuple_entry_t = ffi.typeof('struct port_tuple_entry')

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
        box.error(box.error.NO_SUCH_INDEX, 0, space.name)
    end
    return pk
end
box.internal.check_primary_index = check_primary_index -- for net.box

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
-- Lua 5.2 compatibility
base_index_mt.__len = base_index_mt.len
-- min and max
base_index_mt.min_ffi = function(index, key)
    check_index_arg(index, 'min')
    local pkey, pkey_end = tuple_encode(key)
    if builtin.box_index_min(index.space_id, index.id,
                             pkey, pkey_end, ptuple) ~= 0 then
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
    local pkey, pkey_end = tuple_encode(key)
    if builtin.box_index_max(index.space_id, index.id,
                             pkey, pkey_end, ptuple) ~= 0 then
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
    local pkey, pkey_end = tuple_encode(key)
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
    local pkey, pkey_end = tuple_encode(key)
    local itype = check_iterator_type(opts, pkey + 1 >= pkey_end);
    local count = builtin.box_index_count(index.space_id, index.id,
        itype, pkey, pkey_end);
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
    local key, key_end = tuple_encode(key)
    if builtin.box_index_get(index.space_id, index.id,
                             key, key_end, ptuple) ~= 0 then
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
    local key, key_end = tuple_encode(key)
    local iterator, offset, limit = check_select_opts(opts, key + 1 >= key_end)

    local port = ffi.cast('struct port *', port_tuple)

    if builtin.box_select(index.space_id, index.id,
        iterator, offset, limit, key, key_end, port) ~= 0 then
        return box.error()
    end

    local ret = {}
    local entry = port_tuple.first
    for i=1,tonumber(port_tuple.size),1 do
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

base_index_mt.info = function(index)
    return internal.info(index.space_id, index.id);
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
    mt_mt.__newindex = function(t, k, v)
        mt_mt.__newindex = nil
        mt.__index = function(t, k)
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
    end
end

local sequence_mt = {}
sequence_mt.__index = sequence_mt

sequence_mt.next = function(self)
    return internal.sequence.next(self.id)
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

local function sequence_tuple_decode(seq, tuple)
    seq.id, seq.uid, seq.name, seq.step, seq.min, seq.max,
        seq.start, seq.cache, seq.cycle = tuple:unpack()
end

local function sequence_new(tuple)
    local seq = setmetatable({}, sequence_mt)
    sequence_tuple_decode(seq, tuple)
    return seq
end

local function sequence_on_alter(old_tuple, new_tuple)
    if old_tuple and not new_tuple then
        local old_name = old_tuple[3]
        box.sequence[old_name] = nil
    elseif not old_tuple and new_tuple then
        local seq = sequence_new(new_tuple)
        box.sequence[seq.name] = seq
    else
        local old_name = old_tuple[3]
        local seq = box.sequence[old_name]
        if not seq then
            seq = sequence_new(seq, new_tuple)
        else
            sequence_tuple_decode(seq, new_tuple)
        end
        box.sequence[old_name] = nil
        box.sequence[seq.name] = seq
    end
end

box.sequence = {}
local function box_sequence_init()
    -- Install a trigger that will update Lua objects on
    -- _sequence space modifications.
    internal.sequence.on_alter(sequence_on_alter)
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

box.schema.sequence = {}
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
    sequence_tuple_decode(seq, tuple)
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
            numeric = numeric + 1
        end
        if string.find(privilege, 'write') then
            numeric = numeric + 2
        end
        if string.find(privilege, 'execute') then
            numeric = numeric + 4
        end
        if string.find(privilege, 'session') then
            numeric = numeric + 8
        end
        if string.find(privilege, 'usage') then
            numeric = numeric + 16
        end
        if string.find(privilege, 'create') then
            numeric = numeric + 32
        end
        if string.find(privilege, 'drop') then
            numeric = numeric + 64
        end
        if string.find(privilege, 'alter') then
            numeric = numeric + 128
        end
        if string.find(privilege, 'reference') then
            numeric = numeric + 256
        end
        if string.find(privilege, 'trigger') then
            numeric = numeric + 512
        end
        if string.find(privilege, 'insert') then
            numeric = numeric + 1024
        end
        if string.find(privilege, 'update') then
            numeric = numeric + 2048
        end
        if string.find(privilege, 'delete') then
            numeric = numeric + 4096
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
    if bit.band(privilege, 8) ~= 0 then
        table.insert(names, "session")
    end
    if bit.band(privilege, 16) ~= 0 then
        table.insert(names, "usage")
    end
    if bit.band(privilege, 32) ~= 0 then
        table.insert(names, "create")
    end
    if bit.band(privilege, 64) ~= 0 then
        table.insert(names, "drop")
    end
    if bit.band(privilege, 128) ~= 0 then
        table.insert(names, "alter")
    end
    if bit.band(privilege, 256) ~= 0 then
        table.insert(names, "reference")
    end
    if bit.band(privilege, 512) ~= 0 then
        table.insert(names, "trigger")
    end
    if bit.band(privilege, 1024) ~= 0 then
        table.insert(names, "insert")
    end
    if bit.band(privilege, 2048) ~= 0 then
        table.insert(names, "update")
    end
    if bit.band(privilege, 4096) ~= 0 then
        table.insert(names, "delete")
    end
    return table.concat(names, ",")
end

local function object_resolve(object_type, object_name)
    if object_type == 'universe' then
        if object_name ~= nil and type(object_name) ~= 'string'
                and type(object_name) ~= 'number' then
            box.error(box.error.ILLEGAL_PARAMS, "wrong object name type")
        end
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
    if object_type == 'sequence' then
        local seq = sequence_resolve(object_name)
        if seq == nil then
            box.error(box.error.NO_SUCH_SEQUENCE, object_name)
        end
        return seq
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
    elseif object_type == 'sequence' then
        space = box.space._sequence
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
    opts = update_param_table(opts, { setuid = false, language = 'lua'})
    opts.language = string.upper(opts.language)
    opts.setuid = opts.setuid and 1 or 0
    _func:auto_increment{session.euid(), name, opts.setuid, opts.language}
end

box.schema.func.drop = function(name, opts)
    opts = opts or {}
    check_param_table(opts, { if_exists = 'boolean' })
    local _func = box.space[box.schema.FUNC_ID]
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
    revoke_object_privs('function', fid)
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

box.schema.func.reload = internal.func_reload

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
    return coll[1]
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
    local auth_mech_list = setmap({})
    if opts.password then
        auth_mech_list["chap-sha1"] = box.schema.user.password(opts.password)
    end
    local _user = box.space[box.schema.USER_ID]
    uid = _user:auto_increment{session.euid(), name, 'user', auth_mech_list}[1]
    -- grant role 'public' to the user
    box.schema.user.grant(uid, 'public')
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
    if object_name == nil and object_type == nil then
        -- sic: avoid recursion, to not bother with roles
        -- named 'execute'
        object_type = 'role'
        object_name = privilege
        privilege = 'execute'
    end
    local privilege_hex = checked_privilege(privilege, object_type)

    local oid = object_resolve(object_type, object_name)
    options = options or {}
    if options.grantor == nil then
        options.grantor = session.euid()
    else
        options.grantor = user_or_role_resolve(options.grantor)
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
    elseif not options.if_not_exists then
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
    options = options or {}
    local oid = object_resolve(object_type, object_name)
    local _priv = box.space[box.schema.PRIV_ID]
    local tuple = _priv:get{uid, object_type, oid}
    -- system privileges of admin and guest can't be revoked
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
    local sequences = box.space[box.schema.SEQUENCE_ID].index.owner:select{uid}
    for k, tuple in pairs(sequences) do
        box.schema.sequence.drop(tuple[1])
    end
    -- xxx: hack, we have to revoke session and usage privileges
    -- of a user using a setuid function in absence of create/drop
    -- privileges and grant option
    if box.space._user:get{uid}[4] == 'user' then
        box.session.su('admin', box.schema.user.revoke, uid,
                       'session,usage', 'universe', nil, {if_exists = true})
    end
    local privs = _priv.index.primary:select{uid}
    for k, tuple in pairs(privs) do
        revoke(uid, uid, tuple[5], tuple[3], tuple[4])
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
        return drop(uid, opts)
    end
    if not opts.if_exists then
        box.error(box.error.NO_SUCH_USER, name)
    end
    return
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

function role_check_grant_revoke_of_sys_priv(priv)
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
            t[k] = { engine = v.engine, temporary = v.temporary }
        end
    end
    return t
end

setmetatable(box.space, { __serialize = box_space_mt })

box.internal.schema = {}
box.internal.schema.init = function()
    box_sequence_init()
end

box.feedback = {}
box.feedback.save = function(file_name)
    if type(file_name) ~= "string" then
        error("Usage: box.feedback.save(path)")
    end
    local feedback = json.encode(box.internal.feedback_daemon.generate_feedback())
    local fh, err = fio.open(file_name, {'O_CREAT', 'O_RDWR', 'O_TRUNC'},
        tonumber('0777', 8))
    if not fh then
        error(err)
    end
    fh:write(feedback)
    fh:close()
end

box.NULL = msgpack.NULL
