local log = require('log')
local bit = require('bit')
local json = require('json')

-- Guest user id - the default user
local GUEST = 0
-- Super User ID
local ADMIN = 1
-- role 'PUBLIC' is special, it's automatically granted to every user
local PUBLIC = 2
-- role 'REPLICATION'
local REPLICATION = 3
-- role 'SUPER'
-- choose a fancy id to not clash with any existing role or
-- user during upgrade
local SUPER = 31

--------------------------------------------------------------------------------
-- Utils
--------------------------------------------------------------------------------

local function setmap(tab)
    return setmetatable(tab, { __serialize = 'map' })
end

local function ismap(tab)
    if type(tab) ~= 'table' then
        return false
    end
    local mt = getmetatable(tab)
    return mt and (mt.__serialize == 'map' or mt.__serialize == 'mapping')
end

local mkversion = {}
mkversion.__index = mkversion
setmetatable(mkversion, {__call = function(c, ...) return c.new(...) end})

function mkversion.new(major, minor, patch)
    local self = setmetatable({}, mkversion)
    self.major = major
    self.minor = minor
    self.patch = patch
    self.id = bit.bor(bit.lshift(bit.bor(bit.lshift(major, 8), minor), 8), patch)
    return self
end

function mkversion.__tostring(self)
    return string.format('%s.%s.%s', self.major, self.minor, self.patch)
end

function mkversion.__eq(lhs, rhs)
    return lhs.id == rhs.id
end

function mkversion.__lt(lhs, rhs)
    return lhs.id < rhs.id
end

-- space:truncate() doesn't work with disabled triggers on __index
local function truncate(space)
    local pk = space.index[0]
    while pk:len() > 0 do
        local state, t
        for state, t in pk:pairs() do
            local key = {}
            for _k2, parts in ipairs(pk.parts) do
                table.insert(key, t[parts.fieldno])
            end
            space:delete(key)
        end
    end
end

local function set_system_triggers(val)
    box.space._space:run_triggers(val)
    box.space._index:run_triggers(val)
    box.space._user:run_triggers(val)
    box.space._func:run_triggers(val)
    box.space._priv:run_triggers(val)
end

--------------------------------------------------------------------------------
-- Bootstrap
--------------------------------------------------------------------------------

local function erase()
    truncate(box.space._space)
    truncate(box.space._index)
    truncate(box.space._user)
    truncate(box.space._func)
    truncate(box.space._priv)
    truncate(box.space._sequence_data)
    truncate(box.space._sequence)
    truncate(box.space._truncate)
    truncate(box.space._collation)
    --truncate(box.space._schema)
    box.space._schema:delete('version')
    box.space._schema:delete('max_id')
end

local function initial()
    -- stick to the following convention:
    -- prefer user id (owner id) in field #1
    -- prefer object name in field #2
    -- index on owner id is index #1
    -- index on object name is index #2
    --

    local _schema = box.space[box.schema.SCHEMA_ID]
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _func = box.space[box.schema.FUNC_ID]
    local _user = box.space[box.schema.USER_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local _cluster = box.space[box.schema.CLUSTER_ID]
    local MAP = setmap({})

    --
    -- _schema
    --
    log.info("create space _schema")
    _space:insert{_schema.id, ADMIN, '_schema', 'memtx', 0, MAP, {}}
    log.info("create index primary on _schema")
    _index:insert{_schema.id, 0, 'primary', 'tree', { unique = true }, {{0, 'str'}}}

    --
    -- _space
    --
    log.info("create space _space")
    _space:insert{_space.id, ADMIN, '_space', 'memtx', 0, MAP, {}}
    -- space name is unique
    log.info("create index primary on _space")
    _index:insert{_space.id, 0, 'primary', 'tree', { unique = true }, {{0, 'num'}}}
    log.info("create index owner on _space")
    _index:insert{_space.id, 1, 'owner', 'tree', {unique = false }, {{1, 'num'}}}
    log.info("create index index name on _space")
    _index:insert{_space.id, 2, 'name', 'tree', { unique = true }, {{2, 'str'}}}

    --
    -- _index
    --
    log.info("create space _index")
    _space:insert{_index.id, ADMIN, '_index', 'memtx', 0, MAP, {}}
    -- index name is unique within a space
    log.info("create index primary on _index")
    _index:insert{_index.id, 0, 'primary', 'tree', {unique = true}, {{0, 'num'}, {1, 'num'}}}
    log.info("create index name on _index")
    _index:insert{_index.id, 2, 'name', 'tree', {unique = true}, {{0, 'num'}, {2, 'str'}}}

    --
    -- _func
    --
    log.info("create space _func")
    _space:insert{_func.id, ADMIN, '_func', 'memtx', 0, MAP, {}}
    -- function name and id are unique
    log.info("create index _func:primary")
    _index:insert{_func.id, 0, 'primary', 'tree', {unique = true}, {{0, 'num'}}}
    log.info("create index _func:owner")
    _index:insert{_func.id, 1, 'owner', 'tree', {unique = false}, {{1, 'num'}}}
    log.info("create index _func:name")
    _index:insert{_func.id, 2, 'name', 'tree', {unique = true}, {{2, 'str'}}}

    --
    -- _user
    --
    log.info("create space _user")
    _space:insert{_user.id, ADMIN, '_user', 'memtx', 0, MAP, {}}
    -- user name and id are unique
    log.info("create index _func:primary")
    _index:insert{_user.id, 0, 'primary', 'tree', {unique = true}, {{0, 'num'}}}
    log.info("create index _func:owner")
    _index:insert{_user.id, 1, 'owner', 'tree', {unique = false}, {{1, 'num'}}}
    log.info("create index _func:name")
    _index:insert{_user.id, 2, 'name', 'tree', {unique = true}, {{2, 'str'}}}

    --
    -- _priv
    --
    log.info("create space _priv")
    _space:insert{_priv.id, ADMIN, '_priv', 'memtx', 0, MAP, {}}
    --
    -- space schema is: grantor id, user id, object_type, object_id, privilege
    -- primary key: user id, object type, object id
    log.info("create index primary on _priv")
    _index:insert{_priv.id, 0, 'primary', 'tree', {unique = true}, {{1, 'num'}, {2, 'str'}, {3, 'num'}}}
    -- owner index  - to quickly find all privileges granted by a user
    log.info("create index owner on _priv")
    _index:insert{_priv.id, 1, 'owner', 'tree', {unique = false}, {{0, 'num'}}}
    -- object index - to quickly find all grants on a given object
    log.info("create index object on _priv")
    _index:insert{_priv.id, 2, 'object', 'tree', {unique = false}, {{2, 'str'}, {3, 'num'}}}

    --
    -- _cluster
    --
    log.info("create space _cluster")
    _space:insert{_cluster.id, ADMIN, '_cluster', 'memtx', 0, MAP, {}}
    -- primary key: node id
    log.info("create index primary on _cluster")
    _index:insert{_cluster.id, 0, 'primary', 'tree', {unique = true}, {{0, 'num'}}}
    -- node uuid key: node uuid
    log.info("create index uuid on _cluster")
    _index:insert{_cluster.id, 1, 'uuid', 'tree', {unique = true}, {{1, 'str'}}}

    --
    -- Pre-create user and grants
    log.info("create user guest")
    _user:insert{GUEST, ADMIN, 'guest', 'user'}
    log.info("create user admin")
    _user:insert{ADMIN, ADMIN, 'admin', 'user'}
    log.info("create role public")
    _user:insert{PUBLIC, ADMIN, 'public', 'role'}
    log.info("grant read,write,execute on universe to admin")
    _priv:insert{ADMIN, ADMIN, 'universe', 0, box.priv.R + box.priv.W + box.priv.X}

    -- grant role 'public' to 'guest'
    log.info("grant role public to guest")
    _priv:insert{ADMIN, GUEST, 'role', PUBLIC, box.priv.X}

    log.info("set max_id to box.schema.SYSTEM_ID_MAX")
    _schema:insert{'max_id', box.schema.SYSTEM_ID_MAX}

    log.info("set schema version to 1.6.0")
    _schema:insert({'version', 1, 6, 0})
end

--------------------------------------------------------------------------------
-- Tarantool 1.6.8
--------------------------------------------------------------------------------

local function upgrade_index_options_to_1_6_8()
    local indexes = {}
    for _, def in box.space._index:pairs() do
        if type(def[5]) == 'number' then
            -- Tarantool < 1.6.5 format
            local part_count = def[6]
            local new_def = def:update({{'#', 7, 2 * part_count}}):totable()
            new_def[5] = setmap({})
            new_def[5].unique = def[5] ~= 0
            new_def[6] = {}
            for i=1,part_count,1 do
                local field_id = def[7 + (i - 1) * 2]
                local field_type = def[7 + (i - 1) * 2 + 1]
                table.insert(new_def[6], { field_id, field_type })
            end
            table.insert(indexes, new_def)
        elseif not ismap(def[5]) then
            log.error("unexpected index options: %s", json.encode(def[5]))
        end
    end
    for _, new_def in ipairs(indexes) do
        log.info("alter index %s on %s set options to %s, parts to %s",
                 new_def[3], box.space[new_def[1]].name,
                 json.encode(new_def[5]), json.encode(new_def[6]))
        box.space._index:replace(new_def)
    end
end

local function upgrade_space_options_to_1_6_8()
    local spaces = {}
    for _, def in box.space._space:pairs() do
        local new_def = def:totable()
        new_def[6] = setmap({})
        if def[6] == nil or def[6] == "" then
            -- Tarantool < 1.6.8 format
            table.insert(spaces, new_def)
        elseif def[6] == 'temporary' then
            -- Tarantool < 1.6.8 format
            new_def[6].temporary = true
            table.insert(spaces, new_def)
        elseif not ismap(def[6]) then
            log.error("unexpected space options: %s", json.encode(def[6]))
        end
    end
    for _, new_def in ipairs(spaces) do
        log.info("alter space %s set options to %s", new_def[3],
                 json.encode(new_def[6]))
        box.space._space:update(new_def[1], {{'=', 6, new_def[6]}})
    end
end

local function upgrade_space_format_to_1_6_8()
    local space_def = box.space._space:get(box.space._schema.id)
    if space_def[7] == nil or next(space_def[7]) == nil then
        local format = {}
        format[1] = {type='str', name='key'}
        log.info("alter space _schema set format to %s", json.encode(format))
        box.space._space:update(box.space._schema.id, {{'=', 7, format}})
    end

    local space_def = box.space._space:get(box.space._space.id)
    if space_def[7] == nil or next(space_def[7]) == nil then
        local format = {}
        format[1] = {name='id', type='num'}
        format[2] = {name='owner', type='num'}
        format[3] = {name='name', type='str'}
        format[4] = {name='engine', type='str'}
        format[5] = {name='field_count', type='num'}
        format[6] = {name='flags', type='str'}
        format[7] = {name='format', type='*'}
        log.info("alter space _space set format")
        box.space._space:format(format)
    end

    local space_def = box.space._space:get(box.space._index.id)
    if space_def[7] == nil or next(space_def[7]) == nil or
       space_def[7][5].name == 'unique' then
        local format = {}
        format[1] = {name = 'id', type = 'num'}
        format[2] = {name = 'iid', type = 'num'}
        format[3] = {name = 'name', type = 'str'}
        format[4] = {name = 'type', type = 'str'}
        format[5] = {name = 'opts', type = 'array'}
        format[6] = {name = 'parts', type = 'array'}
        log.info("alter space _index set format")
        box.space._index:format(format)
    end

    local space_def = box.space._space:get(box.space._func.id)
    if space_def[7] == nil or next(space_def[7]) == nil then
        local format = {}
        format[1] = {name='id', type='num'}
        format[2] = {name='owner', type='num'}
        format[3] = {name='name', type='str'}
        format[4] = {name='setuid', type='num'}
        log.info("alter space _func set format")
        box.space._func:format(format)
    end

    local space_def = box.space._space:get(box.space._user.id)
    if space_def[7] == nil or next(space_def[7]) == nil then
        local format = {}
        format[1] = {name='id', type='num'}
        format[2] = {name='owner', type='num'}
        format[3] = {name='name', type='str'}
        format[4] = {name='type', type='str'}
        format[5] = {name='auth', type='*'}
        log.info("alter space _user set format")
        box.space._user:format(format)
    end

    local space_def = box.space._space:get(box.space._priv.id)
    if space_def[7] == nil or next(space_def[7]) == nil then
        local format = {}
        format={}
        format[1] = {name='grantor', type='num'}
        format[2] = {name='grantee', type='num'}
        format[3] = {name='object_type', type='str'}
        format[4] = {name='object_id', type='num'}
        format[5] = {name='privilege', type='num'}
        log.info("alter space _priv set format")
        box.space._priv:format(format)
    end

    local space_def = box.space._space:get(box.space._cluster.id)
    if space_def[7] == nil or next(space_def[7]) == nil then
        local format = {}
        format[1] = {name='id', type='num'}
        format[2] = {name='uuid', type='str'}
        log.info("alter space _schema set format")
        box.space._cluster:format(format)
    end

    local spaces = {}
    for _, space_def in box.space._space:pairs() do
        if space_def[7] == nil then
            table.insert(spaces, space_def)
        end
    end
    for _, space_def in ipairs(spaces) do
        log.info("alter space %s set format", space_def[3])
        box.space._space:update(space_def[1], {{'=', 7, {}}})
    end
end

local function create_sysview(source_id, target_id)
    --
    -- Create definitions for the system view, and grant
    -- privileges on system views to 'PUBLIC' role
    --
    local def = box.space._space:get(source_id):totable()
    def[1] = target_id
    def[3] = "_v"..def[3]:sub(2)
    def[4] = 'sysview'
    local space_def = box.space._space:get(target_id)
    if space_def == nil then
        log.info("create view %s...", def[3])
        box.space._space:replace(def)
    elseif json.encode(space_def[7]) ~= json.encode(def[7]) then
        -- sync box.space._vXXX format with box.space._XXX format
        log.info("alter space %s set format", def[3])
        box.space._space:update(def[1], {{ '=', 7, def[7] }})
    end
    local idefs = {}
    for _, idef in box.space._index:pairs(source_id, { iterator = 'EQ'}) do
        idef = idef:totable()
        idef[1] = target_id
        table.insert(idefs, idef)
    end
    for _, idef in ipairs(idefs) do
        if box.space._index:get({idef[1], idef[2]}) == nil then
            log.info("create index %s on %s", idef[3], def[3])
            box.space._index:replace(idef)
        end
    end
    -- public can read system views
    if box.space._priv.index.primary:count({PUBLIC, 'space', target_id}) == 0 then
        log.info("grant read access to 'public' role for %s view", def[3])
        box.space._priv:insert({1, PUBLIC, 'space', target_id, box.priv.R})
    end
end

local function upgrade_users_to_1_6_8()
    if box.space._user.index.name:count({'replication'}) == 0 then
        log.info("create role replication")
        local RPL_ID = box.space._user:auto_increment{ADMIN, 'replication', 'role'}[1]
        -- replication can read the entire universe
        log.info("grant read on universe to replication")
        box.space._priv:replace{1, RPL_ID, 'universe', 0, box.priv.R}
        -- replication can append to '_cluster' system space
        log.info("grant write on space _cluster to replication")
        box.space._priv:replace{1, RPL_ID, 'space', box.space._cluster.id, box.priv.W}
    end

    if box.space._priv.index.primary:count({ADMIN, 'universe', 0}) == 0 then
        -- grant admin access to the universe
        log.info("grant all on universe to admin")
        box.space._priv:insert{ADMIN, ADMIN, 'universe', 0, box.priv.R +
                                                        box.priv.W + box.priv.X}
    end

    if box.space._func.index.name:count("box.schema.user.info") == 0 then
        -- create "box.schema.user.info" function
        log.info('create function "box.schema.user.info" with setuid')
        box.space._func:auto_increment{ADMIN, 'box.schema.user.info', 1}

        -- grant 'public' role access to 'box.schema.user.info' function
        log.info('grant execute on function "box.schema.user.info" to public')
        box.space._priv:replace{ADMIN, PUBLIC, 'function', 1, box.priv.X}
    end
end

local function upgrade_priv_to_1_6_8()
    -- see e5862c387c7151b812810b1a51086b82a7eedcc4
    local index_def = box.space._index.index.name:get({312, 'owner'})
    local parts = index_def[6]
    if parts[1][1] == 1 then
        log.info("fix index owner for _priv")
        parts = {{0, 'num'}}
        box.space._index:update({index_def[1], index_def[2]},
                                {{'=', 6, parts }})
    end
end

local function upgrade_func_to_1_6_8()
    local funcs = {}
    for _, def in box.space._func:pairs() do
        local new_def = def:totable()
        if new_def[5] == nil then
            new_def[5] = 'LUA'
            table.insert(funcs, new_def)
        end
    end
    for _, def in ipairs(funcs) do
        box.space._func:update(def[1], {{ '=', 5, def[5] }})
    end
end

local function upgrade_to_1_6_8()
    upgrade_index_options_to_1_6_8()
    upgrade_space_options_to_1_6_8()
    upgrade_space_format_to_1_6_8()
    upgrade_users_to_1_6_8()
    upgrade_priv_to_1_6_8()
    upgrade_func_to_1_6_8()

    create_sysview(box.schema.SPACE_ID, box.schema.VSPACE_ID)
    create_sysview(box.schema.INDEX_ID, box.schema.VINDEX_ID)
    create_sysview(box.schema.USER_ID, box.schema.VUSER_ID)
    create_sysview(box.schema.FUNC_ID, box.schema.VFUNC_ID)
    create_sysview(box.schema.PRIV_ID, box.schema.VPRIV_ID)

    local max_id = box.space._schema:get('max_id')
    if max_id == nil then
        local id = box.space._space.index.primary:max()[1]
        if id < box.schema.SYSTEM_ID_MAX then
            id = box.schema.SYSTEM_ID_MAX
        end
        log.info("set max_id to %d", id)
        box.space._schema:insert{'max_id', id}
    end
end

--------------------------------------------------------------------------------
-- Tarantool 1.7.1
--------------------------------------------------------------------------------

local function upgrade_users_to_1_7_1()
    box.schema.user.passwd('guest', '')
end

local function upgrade_to_1_7_1()
    upgrade_users_to_1_7_1()
end

--------------------------------------------------------------------------------
-- Tarantool 1.7.2
--------------------------------------------------------------------------------

local function upgrade_field_types_to_1_7_2()
    local field_types_v16 = {
        num = 'unsigned';
        int = 'integer';
        str = 'string';
    };
    local indexes = {}
    for _, deftuple in box.space._index:pairs() do
        local def = deftuple:totable()
        local changed = false
        local parts = def[6]
        for _, part in pairs(parts) do
            local field_type = part[2]:lower()
            part[2] = field_types_v16[field_type] or field_type
            if field_type ~= part[2] then
                changed = true
            end
        end
        if changed then
            table.insert(indexes, def)
        end
    end
    for _, new_def in ipairs(indexes) do
        log.info("alter index %s on %s set parts to %s",
                 new_def[3], box.space[new_def[1]].name,
                 json.encode(new_def[6]))
        box.space._index:replace(new_def)
    end
end

local function upgrade_to_1_7_2()
    upgrade_field_types_to_1_7_2()
end

--------------------------------------------------------------------------------
-- Tarantool 1.7.5
--------------------------------------------------------------------------------

local function create_truncate_space()
    local _truncate = box.space[box.schema.TRUNCATE_ID]

    log.info("create space _truncate")
    box.space._space:insert{_truncate.id, ADMIN, '_truncate', 'memtx', 0, setmap({}),
                            {{name = 'id', type = 'unsigned'}, {name = 'count', type = 'unsigned'}}}

    log.info("create index primary on _truncate")
    box.space._index:insert{_truncate.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}}

    local _priv = box.space[box.schema.PRIV_ID]
    _priv:insert{ADMIN, PUBLIC, 'space', _truncate.id, box.priv.W}
end

local function update_existing_users_to_1_7_5()
    local def_ids_to_update = {}
    for _, def in box.space._user:pairs() do
        local new_def = def:totable()
        if new_def[5] == nil then
            table.insert(def_ids_to_update, new_def[1])
        end
    end
    for _, id in ipairs(def_ids_to_update) do
        box.space._user:update(id, {{'=', 5, setmap({})}})
    end
end

local function update_space_formats_to_1_7_5()
    local format = {}
    format[1] = {type='string', name='key'}
    box.space._schema:format(format)

    format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='owner', type='unsigned'}
    format[3] = {name='name', type='string'}
    format[4] = {name='engine', type='string'}
    format[5] = {name='field_count', type='unsigned'}
    format[6] = {name='flags', type='map'}
    format[7] = {name='format', type='array'}
    box.space._space:format(format)
    box.space._vspace:format(format)

    format = {}
    format[1] = {name = 'id', type = 'unsigned'}
    format[2] = {name = 'iid', type = 'unsigned'}
    format[3] = {name = 'name', type = 'string'}
    format[4] = {name = 'type', type = 'string'}
    format[5] = {name = 'opts', type = 'map'}
    format[6] = {name = 'parts', type = 'array'}
    box.space._index:format(format)
    box.space._vindex:format(format)

    format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='owner', type='unsigned'}
    format[3] = {name='name', type='string'}
    format[4] = {name='setuid', type='unsigned'}
    box.space._func:format(format)
    box.space._vfunc:format(format)

    format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='owner', type='unsigned'}
    format[3] = {name='name', type='string'}
    format[4] = {name='type', type='string'}
    format[5] = {name='auth', type='map'}
    box.space._user:format(format)
    box.space._vuser:format(format)

    format = {}
    format[1] = {name='grantor', type='unsigned'}
    format[2] = {name='grantee', type='unsigned'}
    format[3] = {name='object_type', type='string'}
    format[4] = {name='object_id', type='unsigned'}
    format[5] = {name='privilege', type='unsigned'}
    box.space._priv:format(format)
    box.space._vpriv:format(format)

    format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='uuid', type='string'}
    box.space._cluster:format(format)
end

local function upgrade_to_1_7_5()
    create_truncate_space()
    update_space_formats_to_1_7_5()
    update_existing_users_to_1_7_5()
end

local function initial_1_7_5()
    -- stick to the following convention:
    -- prefer user id (owner id) in field #1
    -- prefer object name in field #2
    -- index on owner id is index #1
    -- index on object name is index #2
    --

    local _schema = box.space[box.schema.SCHEMA_ID]
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _func = box.space[box.schema.FUNC_ID]
    local _user = box.space[box.schema.USER_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local _cluster = box.space[box.schema.CLUSTER_ID]
    local _truncate = box.space[box.schema.TRUNCATE_ID]
    local MAP = setmap({})

    --
    -- _schema
    --
    log.info("create space _schema")
    local format = {}
    format[1] = {type='string', name='key'}
    _space:insert{_schema.id, ADMIN, '_schema', 'memtx', 0, MAP, format}
    log.info("create index primary on _schema")
    _index:insert{_schema.id, 0, 'primary', 'tree', { unique = true }, {{0, 'string'}}}

    --
    -- _space
    --
    log.info("create space _space")
    format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='owner', type='unsigned'}
    format[3] = {name='name', type='string'}
    format[4] = {name='engine', type='string'}
    format[5] = {name='field_count', type='unsigned'}
    format[6] = {name='flags', type='map'}
    format[7] = {name='format', type='array'}
    _space:insert{_space.id, ADMIN, '_space', 'memtx', 0, MAP, format}
    -- space name is unique
    log.info("create index primary on _space")
    _index:insert{_space.id, 0, 'primary', 'tree', { unique = true }, {{0, 'unsigned'}}}
    log.info("create index owner on _space")
    _index:insert{_space.id, 1, 'owner', 'tree', {unique = false }, {{1, 'unsigned'}}}
    log.info("create index index name on _space")
    _index:insert{_space.id, 2, 'name', 'tree', { unique = true }, {{2, 'string'}}}
    create_sysview(box.schema.SPACE_ID, box.schema.VSPACE_ID)

    --
    -- _index
    --
    log.info("create space _index")
    format = {}
    format[1] = {name = 'id', type = 'unsigned'}
    format[2] = {name = 'iid', type = 'unsigned'}
    format[3] = {name = 'name', type = 'string'}
    format[4] = {name = 'type', type = 'string'}
    format[5] = {name = 'opts', type = 'map'}
    format[6] = {name = 'parts', type = 'array'}
    _space:insert{_index.id, ADMIN, '_index', 'memtx', 0, MAP, format}
    -- index name is unique within a space
    log.info("create index primary on _index")
    _index:insert{_index.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}, {1, 'unsigned'}}}
    log.info("create index name on _index")
    _index:insert{_index.id, 2, 'name', 'tree', {unique = true}, {{0, 'unsigned'}, {2, 'string'}}}
    create_sysview(box.schema.INDEX_ID, box.schema.VINDEX_ID)

    --
    -- _func
    --
    log.info("create space _func")
    format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='owner', type='unsigned'}
    format[3] = {name='name', type='string'}
    format[4] = {name='setuid', type='unsigned'}
    _space:insert{_func.id, ADMIN, '_func', 'memtx', 0, MAP, format}
    -- function name and id are unique
    log.info("create index _func:primary")
    _index:insert{_func.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}}
    log.info("create index _func:owner")
    _index:insert{_func.id, 1, 'owner', 'tree', {unique = false}, {{1, 'unsigned'}}}
    log.info("create index _func:name")
    _index:insert{_func.id, 2, 'name', 'tree', {unique = true}, {{2, 'string'}}}
    create_sysview(box.schema.FUNC_ID, box.schema.VFUNC_ID)

    --
    -- _user
    --
    log.info("create space _user")
    format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='owner', type='unsigned'}
    format[3] = {name='name', type='string'}
    format[4] = {name='type', type='string'}
    format[5] = {name='auth', type='map'}
    _space:insert{_user.id, ADMIN, '_user', 'memtx', 0, MAP, format}
    -- user name and id are unique
    log.info("create index _func:primary")
    _index:insert{_user.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}}
    log.info("create index _func:owner")
    _index:insert{_user.id, 1, 'owner', 'tree', {unique = false}, {{1, 'unsigned'}}}
    log.info("create index _func:name")
    _index:insert{_user.id, 2, 'name', 'tree', {unique = true}, {{2, 'string'}}}
    create_sysview(box.schema.USER_ID, box.schema.VUSER_ID)

    --
    -- _priv
    --
    log.info("create space _priv")
    format = {}
    format[1] = {name='grantor', type='unsigned'}
    format[2] = {name='grantee', type='unsigned'}
    format[3] = {name='object_type', type='string'}
    format[4] = {name='object_id', type='unsigned'}
    format[5] = {name='privilege', type='unsigned'}
    _space:insert{_priv.id, ADMIN, '_priv', 'memtx', 0, MAP, format}
    -- user id, object type and object id are unique
    log.info("create index primary on _priv")
    _index:insert{_priv.id, 0, 'primary', 'tree', {unique = true}, {{1, 'unsigned'}, {2, 'string'}, {3, 'unsigned'}}}
    -- owner index  - to quickly find all privileges granted by a user
    log.info("create index owner on _priv")
    _index:insert{_priv.id, 1, 'owner', 'tree', {unique = false}, {{0, 'unsigned'}}}
    -- object index - to quickly find all grants on a given object
    log.info("create index object on _priv")
    _index:insert{_priv.id, 2, 'object', 'tree', {unique = false}, {{2, 'string'}, {3, 'unsigned'}}}
    create_sysview(box.schema.PRIV_ID, box.schema.VPRIV_ID)

    --
    -- _cluster
    --
    log.info("create space _cluster")
    format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='uuid', type='string'}
    _space:insert{_cluster.id, ADMIN, '_cluster', 'memtx', 0, MAP, format}
    -- primary key: node id
    log.info("create index primary on _cluster")
    _index:insert{_cluster.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}}
    -- node uuid key: node uuid
    log.info("create index uuid on _cluster")
    _index:insert{_cluster.id, 1, 'uuid', 'tree', {unique = true}, {{1, 'string'}}}

    --
    -- _truncate
    --
    log.info("create space _truncate")
    format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='count', type='unsigned'}
    _space:insert{_truncate.id, ADMIN, '_truncate', 'memtx', 0, MAP, format}
    -- primary key: space id
    log.info("create index primary on _truncate")
    _index:insert{_truncate.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}}

    --
    -- Create users
    --
    log.info("create user guest")
    _user:insert{GUEST, ADMIN, 'guest', 'user', MAP}
    box.schema.user.passwd('guest', '')
    log.info("create user admin")
    _user:insert{ADMIN, ADMIN, 'admin', 'user', MAP}
    log.info("create role public")
    _user:insert{PUBLIC, ADMIN, 'public', 'role', MAP}
    log.info("create role replication")
    _user:insert{REPLICATION, ADMIN, 'replication', 'role', MAP}

    --
    -- Create grants
    --
    log.info("grant read,write,execute on universe to admin")
    _priv:insert{ADMIN, ADMIN, 'universe', 0, box.priv.R + box.priv.W + box.priv.X}

    -- grant role 'public' to 'guest'
    log.info("grant role public to guest")
    _priv:insert{ADMIN, GUEST, 'role', PUBLIC, box.priv.X}

    -- replication can read the entire universe
    log.info("grant read on universe to replication")
    _priv:replace{ADMIN, REPLICATION, 'universe', 0, box.priv.R}
    -- replication can append to '_cluster' system space
    log.info("grant write on space _cluster to replication")
    _priv:replace{ADMIN, REPLICATION, 'space', _cluster.id, box.priv.W}

    _priv:insert{ADMIN, PUBLIC, 'space', _truncate.id, box.priv.W}

    -- create "box.schema.user.info" function
    log.info('create function "box.schema.user.info" with setuid')
    _func:replace{1, ADMIN, 'box.schema.user.info', 1, 'LUA'}

    -- grant 'public' role access to 'box.schema.user.info' function
    log.info('grant execute on function "box.schema.user.info" to public')
    _priv:replace{ADMIN, PUBLIC, 'function', 1, box.priv.X}

    log.info("set max_id to box.schema.SYSTEM_ID_MAX")
    _schema:insert{'max_id', box.schema.SYSTEM_ID_MAX}

    log.info("set schema version to 1.7.5")
    _schema:insert({'version', 1, 7, 5})
end

local sequence_format = {{name = 'id', type = 'unsigned'},
                         {name = 'owner', type = 'unsigned'},
                         {name = 'name', type = 'string'},
                         {name = 'step', type = 'integer'},
                         {name = 'min', type = 'integer'},
                         {name = 'max', type = 'integer'},
                         {name = 'start', type = 'integer'},
                         {name = 'cache', type = 'integer'},
                         {name = 'cycle', type = 'boolean'}}
--------------------------------------------------------------------------------
-- Tarantool 1.7.6
local function create_sequence_space()
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _sequence = box.space[box.schema.SEQUENCE_ID]
    local _sequence_data = box.space[box.schema.SEQUENCE_DATA_ID]
    local _space_sequence = box.space[box.schema.SPACE_SEQUENCE_ID]
    local MAP = setmap({})

    log.info("create space _sequence")
    _space:insert{_sequence.id, ADMIN, '_sequence', 'memtx', 0, MAP, sequence_format}
    log.info("create index _sequence:primary")
    _index:insert{_sequence.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}}
    log.info("create index _sequence:owner")
    _index:insert{_sequence.id, 1, 'owner', 'tree', {unique = false}, {{1, 'unsigned'}}}
    log.info("create index _sequence:name")
    _index:insert{_sequence.id, 2, 'name', 'tree', {unique = true}, {{2, 'string'}}}

    log.info("create space _sequence_data")
    _space:insert{_sequence_data.id, ADMIN, '_sequence_data', 'memtx', 0, MAP,
                  {{name = 'id', type = 'unsigned'}, {name = 'value', type = 'integer'}}}
    log.info("create index primary on _sequence_data")
    _index:insert{_sequence_data.id, 0, 'primary', 'hash', {unique = true}, {{0, 'unsigned'}}}

    log.info("create space _space_sequence")
    _space:insert{_space_sequence.id, ADMIN, '_space_sequence', 'memtx', 0, MAP,
                  {{name = 'id', type = 'unsigned'},
                   {name = 'sequence_id', type = 'unsigned'},
                   {name = 'is_generated', type = 'boolean'}}}
    log.info("create index _space_sequence:primary")
    _index:insert{_space_sequence.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}}
    log.info("create index _space_sequence:sequence")
    _index:insert{_space_sequence.id, 1, 'sequence', 'tree', {unique = false}, {{1, 'unsigned'}}}
end

local function create_collation_space()
    local _collation = box.space[box.schema.COLLATION_ID]

    log.info("create space _collation")
    box.space._space:insert{_collation.id, ADMIN, '_collation', 'memtx', 0, setmap({}),
        { { name = 'id', type = 'unsigned' }, { name = 'name', type = 'string' },
          { name = 'owner', type = 'unsigned' }, { name = 'type', type = 'string' },
          { name = 'locale', type = 'string' }, { name = 'opts', type = 'map' } } }

    log.info("create index primary on _collation")
    box.space._index:insert{_collation.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}}

    log.info("create index name on _collation")
    box.space._index:insert{_collation.id, 1, 'name', 'tree', {unique = true}, {{1, 'string'}}}

    log.info("create predefined collations")
    box.space._collation:replace{1, "unicode", ADMIN, "ICU", "", setmap{}}
    box.space._collation:replace{2, "unicode_ci", ADMIN, "ICU", "", {strength='primary'}}

    local _priv = box.space[box.schema.PRIV_ID]
    _priv:insert{ADMIN, PUBLIC, 'space', _collation.id, box.priv.W}
end

local function upgrade_to_1_7_6()
    create_sequence_space()
    create_collation_space()
    -- Trigger space format checking by updating version in _schema.
end

--------------------------------------------------------------------------------
--- Tarantool 1.7.7
--------------------------------------------------------------------------------
local function upgrade_to_1_7_7()
    local _priv = box.space[box.schema.PRIV_ID]
    local _user = box.space[box.schema.USER_ID]
    --
    -- grant 'session' and 'usage' to all existing users
    --
    for _, v in _user:pairs() do
        if v[4] ~= "role" then
            _priv:upsert({ADMIN, v[1], "universe", 0, box.priv.S + box.priv.U},
                                                {{"|", 5, box.priv.S + box.priv.U}})
        end
    end
    --
    -- grant 'create' to all users with 'read' and 'write'
    -- on the universe, since going forward we will require
    -- 'create' rather than 'read,write' to be able to create
    -- objects
    --
    for _, v in _priv.index.object:pairs{'universe'} do
        if bit.band(v[5], 1) ~= 0 and bit.band(v[5], 2) ~= 0 then
            _priv:update({v[2], v[3], v[4]}, {{ "|", 5, box.priv.C}})
        end
    end
    -- grant admin all new privileges (session, usage, grant option,
    -- create, alter, drop and anything that might come up in the future
    --
    _priv:upsert({ADMIN, ADMIN, 'universe', 0, box.priv.ALL},
                 {{ "|", 5, box.priv.ALL}})
    --
    -- create role 'super' and grant it all privileges on universe
    --
    _user:replace{SUPER, ADMIN, 'super', 'role', setmap({})}
    _priv:replace({ADMIN, SUPER, 'universe', 0, 4294967295})
end

--------------------------------------------------------------------------------
--- Tarantool 1.10.0
--------------------------------------------------------------------------------
local function create_vsequence_space()
    create_sysview(box.schema.SEQUENCE_ID, box.schema.VSEQUENCE_ID)
    box.space._vsequence:format(sequence_format)
end

local function upgrade_to_1_10_0()
    create_vsequence_space()
end


local function get_version()
    local version = box.space._schema:get{'version'}
    if version == nil then
        error('Missing "version" in box.space._schema')
    end
    local major = version[2]
    local minor = version[3]
    local patch = version[4] or 0

    return mkversion(major, minor, patch)
end

local function upgrade(options)
    options = options or {}
    setmetatable(options, {__index = {auto = false}})

    local version = get_version()

    local handlers = {
        {version = mkversion(1, 6, 8), func = upgrade_to_1_6_8, auto = false},
        {version = mkversion(1, 7, 1), func = upgrade_to_1_7_1, auto = false},
        {version = mkversion(1, 7, 2), func = upgrade_to_1_7_2, auto = false},
        {version = mkversion(1, 7, 5), func = upgrade_to_1_7_5, auto = true},
        {version = mkversion(1, 7, 6), func = upgrade_to_1_7_6, auto = false},
        {version = mkversion(1, 7, 7), func = upgrade_to_1_7_7, auto = true},
        {version = mkversion(1, 10, 0), func = upgrade_to_1_10_0, auto = true},
    }

    for _, handler in ipairs(handlers) do
        if version >= handler.version then
            goto continue
        end
        if options.auto and not handler.auto then
            log.warn("cannot auto upgrade schema version to %s, " ..
                     "please call box.schema.upgrade() manually",
                     handler.version)
            return
        end
        handler.func()
        log.info("set schema version to %s", handler.version)
        box.space._schema:replace({'version',
                                   handler.version.major,
                                   handler.version.minor,
                                   handler.version.patch})
        ::continue::
    end
end

local function bootstrap()
    set_system_triggers(false)
    local version = get_version()

    -- Initial() creates a spaces with 1.6.0 format, but 1.7.6
    -- checks space formats, that fails initial(). It is because
    -- bootstrap() is called after box.cfg{}. If box.cfg{} is run
    -- on 1.7.6, then spaces in the cache contains new 1.7.6
    -- formats (gh-2754). Spaces in the cache are not updated on
    -- erase(), because system triggers are turned off.

    -- erase current schema
    erase()
    -- insert initial schema
    if version < mkversion(1, 7, 6) then
        initial()
    else
        initial_1_7_5()
    end

    -- upgrade schema to the latest version
    upgrade()

    set_system_triggers(true)

    -- save new bootstrap.snap
    box.snapshot()
end

box.schema.upgrade = upgrade;
box.internal.bootstrap = bootstrap;
