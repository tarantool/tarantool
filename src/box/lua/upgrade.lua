local log = require('log')
local bit = require('bit')
local json = require('json')

local VERSION_ID

-- Guest user id - the default user
local GUEST = 0
-- Super User ID
local ADMIN = 1
-- role 'PUBLIC' is special, it's automatically granted to every user
local PUBLIC = 2


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

local function version_id(major, minor, patch)
    return bit.bor(bit.lshift(bit.bor(bit.lshift(major, 8), minor), 8), patch)
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
    _priv:insert{ADMIN, ADMIN, 'universe', 0, 7}

    -- grant role 'public' to 'guest'
    log.info("grant role public to guest")
    _priv:insert{ADMIN, GUEST, 'role', PUBLIC, 4}

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
        box.space._priv:insert({1, PUBLIC, 'space', target_id, 1})
    end
end

local function upgrade_users_to_1_6_8()
    if box.space._user.index.name:count({'replication'}) == 0 then
        log.info("create role replication")
        local RPL_ID = box.space._user:auto_increment{ADMIN, 'replication', 'role'}[1]
        -- replication can read the entire universe
        log.info("grant read on universe to replication")
        box.space._priv:replace{1, RPL_ID, 'universe', 0, 1}
        -- replication can append to '_cluster' system space
        log.info("grant write on space _cluster to replication")
        box.space._priv:replace{1, RPL_ID, 'space', box.space._cluster.id, 2}
    end

    if box.space._priv.index.primary:count({ADMIN, 'universe', 0}) == 0 then
        -- grant admin access to the universe
        log.info("grant all on universe to admin")
        box.space._priv:insert{ADMIN, ADMIN, 'universe', 0, 7}
    end

    if box.space._func.index.name:count("box.schema.user.info") == 0 then
        -- create "box.schema.user.info" function
        log.info('create function "box.schema.user.info" with setuid')
        box.space._func:auto_increment{ADMIN, 'box.schema.user.info', 1}

        -- grant 'public' role access to 'box.schema.user.info' function
        log.info('grant execute on function "box.schema.user.info" to public')
        box.space._priv:replace{ADMIN, PUBLIC, 'function', 1, 4}
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
    if VERSION_ID >= version_id(1, 6, 8) then
        return
    end

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

    log.info("set schema version to 1.6.8")
    box.space._schema:replace({'version', 1, 6, 8})
end

--------------------------------------------------------------------------------
-- Tarantool 1.7.1
--------------------------------------------------------------------------------

local function upgrade_users_to_1_7_1()
    box.schema.user.passwd('guest', '')
end

local function upgrade_to_1_7_1()
    if VERSION_ID >= version_id(1, 7, 0) then
        return
    end

    upgrade_users_to_1_7_1()

    log.info("set schema version to 1.7.1")
    box.space._schema:replace({'version', 1, 7, 1})
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
    if VERSION_ID >= version_id(1, 7, 2) then
        return
    end

    upgrade_field_types_to_1_7_2()

    log.info("set schema version to 1.7.2")
    box.space._schema:replace({'version', 1, 7, 2})
end

local function upgrade_to_1_8_2()
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _trigger = box.space[box.schema.TRIGGER_ID]
    local format = {{name='name', type='str'},
                    {name='opts', type='map'}}

    log.info("create space _trigger")
    _space:insert{_trigger.id, ADMIN, '_trigger', 'memtx', 0, setmap({}), {}}
    log.info("create index primary on _trigger")
    _index:insert{_trigger.id, 0, 'primary', 'tree', { unique = true }, {{0, 'str'}}}

    log.info("alter space _trigger set format")
    _trigger:format(format)

    log.info("set schema version to 1.8.2")
    box.space._schema:replace({'version', 1, 8, 2})
end

--------------------------------------------------------------------------------

local function upgrade()
    box.cfg{}
    local version = box.space._schema:get{'version'}
    if version == nil then
        error('Missing "version" in box.space._schema')
    end
    local major = version[2]
    local minor = version[3]
    local patch = version[4] or 0
    VERSION_ID = version_id(major, minor, patch)

    upgrade_to_1_6_8()
    upgrade_to_1_7_1()
    upgrade_to_1_7_2()
    upgrade_to_1_8_2()
end

local function bootstrap()
    set_system_triggers(false)

    -- erase current schema
    erase()
    -- insert initial schema
    initial()
    -- upgrade schema to the latest version
    upgrade()

    set_system_triggers(true)

    -- save new bootstrap.snap
    box.snapshot()
end

box.schema.upgrade = upgrade;
box.internal.bootstrap = bootstrap;
