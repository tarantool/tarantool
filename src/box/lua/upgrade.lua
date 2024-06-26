local log = require('log')
local bit = require('bit')
local json = require('json')
local fio = require('fio')
local xlog = require('xlog')
local ffi = require('ffi')
local fun = require('fun')
local utils = require('internal.utils')
local tarantool = require('tarantool')
local mkversion = require('internal.mkversion')

ffi.cdef([[
    void box_init_latest_dd_version_id(uint32_t version_id);
    bool box_schema_needs_upgrade(void);
    int box_schema_upgrade_begin(void);
    void box_schema_upgrade_end(void);
]])
local builtin = ffi.C

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

-- Used to give a hint that the table should be serialized in MsgPack as map
-- and not as a array. Typical use is to give hint for empty table when it is
-- not possible to infer type from table content.
local function setmap(tab)
    return setmetatable(tab, { __serialize = 'map' })
end

-- space:truncate() doesn't work with disabled triggers on __index
local function truncate(space)
    local pk = space.index[0]
    while pk:len() > 0 do
        for _, t in pk:pairs() do
            local key = {}
            for _, parts in ipairs(pk.parts) do
                table.insert(key, t[parts.fieldno])
            end
            space:delete(key)
        end
    end
end

local function foreach_system_space(cb)
    local max = box.schema.SYSTEM_ID_MAX
    for id, space in pairs(box.space) do
        -- Check for number, because box.space contains each space
        -- twice - by ID and by name. Here IDs are selected.
        -- Engine is checked to be a 'native' space, because other
        -- engines does not support DML, and does not have
        -- triggers to turn off/on. These are 'service',
        -- 'blackhole', and more may be added.
        -- When id > max system id is met, break is not done,
        -- because box.space is not an array, and it is not safe
        -- to assume all its numeric indexes are returned in
        -- ascending order.
        if type(id) == 'number' and id <= max and
           (space.engine == 'memtx' or space.engine == 'vinyl') then
            cb(space)
        end
    end
end

local function set_system_triggers(val)
    foreach_system_space(function(s) s:run_triggers(val) end)
end

local function with_disabled_system_triggers(func)
    set_system_triggers(false)
    local status, err = pcall(func)
    set_system_triggers(true)
    if not status then
        error(err)
    end
end

-- Clears formats of all system spaces. It is used to disable system space
-- format checking before creation of a bootstrap snapshot.
local function clear_system_formats()
    foreach_system_space(function(s)
        box.space._space:update({s.id}, {{'=', 7, {}}})
    end)
end

-- Applies no-op update to all system space records to run system triggers.
-- It is used to re-enable system space format checking after creation of
-- a bootstrap snapshot.
local function reset_system_formats()
    foreach_system_space(function(s)
        box.space._space:update({s.id}, {})
    end)
end

-- Get schema version, stored in _schema system space, by reading the latest
-- snapshot file from the snap_dir. Useful to define schema_version before
-- recovering the snapshot, because some schema versions are too old and cannot
-- be recovered normally.
local function get_snapshot_version(snap_dir)
    local snap_pattern = fio.pathjoin(snap_dir,
                                      string.rep('[0-9]', 20)..'.snap')
    local snap_list = fio.glob(snap_pattern)
    table.sort(snap_list)
    local snap = snap_list[#snap_list]
    if not snap then
        return nil
    end
    local version = nil
    for _, row in xlog.pairs(snap) do
        local sid = row.BODY and row.BODY.space_id
        if sid == box.schema.SCHEMA_ID then
            local tuple = row.BODY.tuple
            if tuple and tuple[1] == 'version' then
                version = mkversion.from_tuple(tuple)
                if not version then
                    log.error("Corrupted version tuple in space '_schema' "..
                              "in snapshot '%s': %s ", snap, tuple)
                end
                break
            end
        elseif sid and sid > box.schema.SCHEMA_ID then
            -- Exit early if version wasn't found in _schema space.
            -- Snapshot rows are ordered by space id.
            break
        end
    end
    return version
end

--------------------------------------------------------------------------------
-- Bootstrap
--------------------------------------------------------------------------------

local function erase()
    foreach_system_space(function(s) truncate(s) end)
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

--------------------------------------------------------------------------------
-- Tarantool 1.7.1
--------------------------------------------------------------------------------
local function user_trig_1_7_1(_, tuple)
    if tuple and tuple[3] == 'guest' and not tuple[5] then
        local auth_method_list = {}
        auth_method_list["chap-sha1"] = box.schema.user.password("")
        tuple = tuple:update{{'=', 5, auth_method_list}}
        log.info("Set empty password to user 'guest'")
    end
    return tuple
end

--------------------------------------------------------------------------------
-- Tarantool 1.7.2
--------------------------------------------------------------------------------
local function index_trig_1_7_2(_, tuple)
    local field_types_v16 = {
        num = 'unsigned',
        int = 'integer',
        str = 'string',
    }
    if not tuple then
        return tuple
    end
    local parts = tuple[6]
    local changed = false
    for _, part in pairs(parts) do
        local field_type = part[2]:lower()
        if field_types_v16[field_type] ~= nil then
            part[2] = field_types_v16[field_type]
            changed = true
        end
    end
    if changed then
        log.info("Update index '%s' on space '%s': set parts to %s", tuple[3],
                 box.space[tuple[1]].name, json.encode(parts))
        tuple = tuple:update{{'=', 6, parts}}
    end
    return tuple
end

--------------------------------------------------------------------------------
-- Tarantool 1.7.5
--------------------------------------------------------------------------------
local function create_truncate_space()
    local _truncate = box.space[box.schema.TRUNCATE_ID]

    log.info("create space _truncate")
    box.space._space:insert{
        _truncate.id, ADMIN, '_truncate', 'memtx', 0, setmap({}),
        {{name = 'id', type = 'unsigned'}, {name = 'count', type = 'unsigned'}}
    }

    log.info("create index primary on _truncate")
    box.space._index:insert{
        _truncate.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}
    }

    local _priv = box.space[box.schema.PRIV_ID]
    _priv:insert{ADMIN, PUBLIC, 'space', _truncate.id, box.priv.W}
end

local function upgrade_to_1_7_5()
    create_truncate_space()
end

local function user_trig_1_7_5(_, tuple)
    if tuple and not tuple[5] then
        tuple = tuple:update{{'=', 5, setmap({})}}
        log.info("Set empty password to %s '%s'", tuple[4], tuple[3])
    end
    return tuple
end

local space_formats_1_7_5 = {
    _schema = {
        {name = 'key', type = 'string'},
    },
    _space = {
        {name = 'id', type = 'unsigned'},
        {name = 'owner', type = 'unsigned'},
        {name = 'name', type = 'string'},
        {name = 'engine', type = 'string'},
        {name = 'field_count', type = 'unsigned'},
        {name = 'flags', type = 'map'},
        {name = 'format', type = 'array'},
    },
    _index = {
        {name = 'id', type = 'unsigned'},
        {name = 'iid', type = 'unsigned'},
        {name = 'name', type = 'string'},
        {name = 'type', type = 'string'},
        {name = 'opts', type = 'map'},
        {name = 'parts', type = 'array'},
    },
    _func = {
        {name = 'id', type = 'unsigned'},
        {name = 'owner', type = 'unsigned'},
        {name = 'name', type = 'string'},
        {name = 'setuid', type = 'unsigned'},
    },
    _user = {
        {name = 'id', type = 'unsigned'},
        {name = 'owner', type = 'unsigned'},
        {name = 'name', type = 'string'},
        {name = 'type', type = 'string'},
        {name = 'auth', type = 'map'},
    },
    _priv = {
        {name = 'grantor', type = 'unsigned'},
        {name = 'grantee', type = 'unsigned'},
        {name = 'object_type', type = 'string'},
        {name = 'object_id', type = 'unsigned'},
        {name = 'privilege', type = 'unsigned'},
    },
    _cluster = {
        {name = 'id', type = 'unsigned'},
        {name = 'uuid', type = 'string'},
    },
}

space_formats_1_7_5._vspace = space_formats_1_7_5._space
space_formats_1_7_5._vindex = space_formats_1_7_5._index
space_formats_1_7_5._vfunc = space_formats_1_7_5._func
space_formats_1_7_5._vuser = space_formats_1_7_5._user
space_formats_1_7_5._vpriv = space_formats_1_7_5._priv

local function space_trig_1_7_5(_, tuple)
    if tuple and space_formats_1_7_5[tuple[3]] and
       not table.equals(space_formats_1_7_5[tuple[3]], tuple[7]) then
        tuple = tuple:update{{'=', 7, space_formats_1_7_5[tuple[3]]}}
        log.info("Update space '%s' format: new format %s", tuple[3],
                 json.encode(tuple[7]))
    end
    return tuple
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
    _user:insert{GUEST, ADMIN, 'guest', 'user',
                 {['chap-sha1'] = 'vhvewKp0tNyweZQ+cFKAlsyphfg='}}
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
--------------------------------------------------------------------------------

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

local function priv_trig_1_7_7(_, tuple)
    if tuple and tuple[2] == ADMIN and tuple[3] == 'universe' and
       tuple[5] ~= box.priv.ALL then
        tuple = tuple:update{{'=', 5, box.priv.ALL}}
        log.info("Grant all privileges to user 'admin'")
    end
    return tuple
end

--------------------------------------------------------------------------------
--- Tarantool 1.10.0
--------------------------------------------------------------------------------
local function create_vsequence_space()
    create_sysview(box.schema.SEQUENCE_ID, box.schema.VSEQUENCE_ID)
    box.space._space:update({box.schema.VSEQUENCE_ID},
                            {{'=', 7, sequence_format}})
end

local function upgrade_to_1_10_0()
    create_vsequence_space()
end

--------------------------------------------------------------------------------
--- Tarantool 1.10.2
--------------------------------------------------------------------------------
local function upgrade_priv_to_1_10_2()
    local _priv = box.space._priv
    local _vpriv = box.space._vpriv
    local _space = box.space._space
    local _index = box.space._index
    _space:update({_priv.id}, {{'=', '[7][4].type', 'scalar'}})
    _space:update({_vpriv.id}, {{'=', '[7][4].type', 'scalar'}})
    _index:update({_priv.id, _priv.index.primary.id},
                  {{'=', 6, {{1, 'unsigned'}, {2, 'string'}, {3, 'scalar'}}}})
    _index:update({_vpriv.id, _vpriv.index.primary.id},
                  {{'=', 6, {{1, 'unsigned'}, {2, 'string'}, {3, 'scalar'}}}})
    _index:update({_priv.id, _priv.index.object.id},
                  {{'=', 6, {{2, 'string'}, {3, 'scalar'}}}})
    _index:update({_vpriv.id, _priv.index.object.id},
                  {{'=', 6, {{2, 'string'}, {3, 'scalar'}}}})
end

local function create_vinyl_deferred_delete_space()
    local _space = box.space[box.schema.SPACE_ID]
    local _vinyl_deferred_delete = box.space[box.schema.VINYL_DEFERRED_DELETE_ID]

    local format = {}
    format[1] = {name = 'space_id', type = 'unsigned'}
    format[2] = {name = 'lsn', type = 'unsigned'}
    format[3] = {name = 'tuple', type = 'array'}

    log.info("create space _vinyl_deferred_delete")
    _space:insert{_vinyl_deferred_delete.id, ADMIN, '_vinyl_deferred_delete',
                  'blackhole', 0, {group_id = 1}, format}
end

local function upgrade_to_1_10_2()
    upgrade_priv_to_1_10_2()
    create_vinyl_deferred_delete_space()
end

--------------------------------------------------------------------------------
-- Tarantool 2.1.0
--------------------------------------------------------------------------------

local function upgrade_priv_to_2_1_0()
    local _priv = box.space[box.schema.PRIV_ID]
    local _user = box.space[box.schema.USER_ID]
    -- Since we remove 1.7 compatibility in 2.1.0, we have to
    -- grant ALTER and DROP to all users with READ + WRITE on
    -- respective objects. We also grant CREATE on entities
    -- or on universe if a user has READ and WRITE on an entity
    -- or on universe respectively. We do not grant CREATE on
    -- objects, since it has no effect. We also skip grants for
    -- sequences since they were added after the new privileges
    -- and compatibility mode was always off for them.
    for _, user in _user:pairs() do
        if user[0] ~= ADMIN and user[0] ~= SUPER then
            for _, priv in _priv:pairs(user[0]) do
                if priv[3] ~= 'sequence' and
                   bit.band(priv[5], box.priv.W) ~= 0 and
                   bit.band(priv[5], box.priv.R) ~= 0 then
                    local new_privs = bit.bor(box.priv.A, box.priv.D)
                    if priv[3] == 'universe' or priv[4] == '' then
                        new_privs = bit.bor(new_privs, box.priv.C)
                    end
                    _priv:update({priv[2], priv[3], priv[4]},
                                 {{"|", 5, new_privs}})
                end
            end
        end
    end
end

local function upgrade_to_2_1_0()
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _trigger = box.space[box.schema.TRIGGER_ID]
    local MAP = setmap({})

    log.info("create space _trigger")
    local format = {{name='name', type='string'},
                    {name='space_id', type='unsigned'},
                    {name='opts', type='map'}}
    _space:insert{_trigger.id, ADMIN, '_trigger', 'memtx', 0, MAP, format}

    log.info("create index primary on _trigger")
    _index:insert{_trigger.id, 0, 'primary', 'tree', { unique = true },
                  {{0, 'string'}}}
    log.info("create index secondary on _trigger")
    _index:insert{_trigger.id, 1, 'space_id', 'tree', { unique = false },
                  {{1, 'unsigned'}}}

    local fk_constr_ft = {{name='name', type='string'},
                          {name='child_id', type='unsigned'},
                          {name='parent_id', type='unsigned'},
                          {name='is_deferred', type='boolean'},
                          {name='match', type='string'},
                          {name='on_delete', type='string'},
                          {name='on_update', type='string'},
                          {name='child_cols', type='array'},
                          {name='parent_cols', type='array'}}
    log.info("create space _fk_constraint")
    _space:insert{box.schema.FK_CONSTRAINT_ID, ADMIN, '_fk_constraint', 'memtx',
                  0, setmap({}), fk_constr_ft}

    log.info("create index primary on _fk_constraint")
    _index:insert{box.schema.FK_CONSTRAINT_ID, 0, 'primary', 'tree',
                  {unique = true}, {{0, 'string'}, {1, 'unsigned'}}}

    log.info("create secondary index child_id on _fk_constraint")
    _index:insert{box.schema.FK_CONSTRAINT_ID, 1, 'child_id', 'tree',
                  {unique = false}, {{1, 'unsigned'}}}

    -- Nullability wasn't skipable. This was fixed in 1-7.
    -- Now, abscent field means NULL, so we can safely set second
    -- field in format, marking it nullable.
    log.info("Add nullable value field to space _schema")
    local format = {}
    format[1] = {type='string', name='key'}
    format[2] = {type='any', name='value', is_nullable=true}
    _space:update({box.schema.SCHEMA_ID}, {{'=', 7, format}})

    box.space._collation:replace{0, "none", ADMIN, "BINARY", "", setmap{}}
    box.space._collation:replace{3, "binary", ADMIN, "BINARY", "", setmap{}}

    upgrade_priv_to_2_1_0()
end

--------------------------------------------------------------------------------
-- Tarantool 2.1.1
--------------------------------------------------------------------------------

local function upgrade_to_2_1_1()
    local _index = box.space[box.schema.INDEX_ID]
    for _, index in _index:pairs() do
        local opts = index[5]
        if opts['sql'] ~= nil then
            opts['sql'] = nil
            _index:replace(box.tuple.new({index.id, index.iid, index.name,
                                        index.type, opts, index.parts}))
        end
    end
end

--------------------------------------------------------------------------------
-- Tarantool 2.1.2
--------------------------------------------------------------------------------

local function update_collation_strength_field()
    local _collation = box.space[box.schema.COLLATION_ID]
    for _, collation in _collation:pairs() do
        if collation[4] == 'ICU' and collation[6].strength == nil then
            local new_collation = collation:totable()
            new_collation[6].strength = 'tertiary'
            _collation:delete{collation[1]}
            _collation:insert(new_collation)
        end
    end
end

local function upgrade_to_2_1_2()
    update_collation_strength_field()
end

--------------------------------------------------------------------------------
-- Tarantool 2.1.3
--------------------------------------------------------------------------------

-- Add new collations
local function upgrade_collation_to_2_1_3()
    local coll_lst = {
        {name="af", loc_str="af"},  -- Afrikaans
        {name="am", loc_str="am"},  -- Amharic (no character changes, just re-ordering)
        {name="ar", loc_str="ar"},  -- Arabic (use only "standard")
        {name="as", loc_str="as"},  -- Assamese
        {name="az", loc_str="az"},  -- Azerbaijani (Azeri)
        {name="be", loc_str="be"},  -- Belarusian
        {name="bn", loc_str="bn"},  -- Bengali (Bangla actually)
        {name="bs", loc_str="bs"},  -- Bosnian (tailored as Croatian)
        {name="bs_Cyrl", loc_str="bs_Cyrl"}, -- Bosnian in Cyrillic (tailored as Serbian)
        {name="ca", loc_str="ca"},  -- Catalan
        {name="cs", loc_str="cs"},  -- Czech
        {name="cy", loc_str="cy"},  -- Welsh
        {name="da", loc_str="da"},  -- Danish
        {name="de__phonebook", loc_str="de_DE_u_co_phonebk"}, -- German (umlaut as 'ae', 'oe', 'ue')
        {name="de_AT_phonebook", loc_str="de_AT_u_co_phonebk"}, -- Austrian German (umlaut primary greater)
        {name="dsb", loc_str="dsb"}, -- Lower Sorbian
        {name="ee", loc_str="ee"},  -- Ewe
        {name="eo", loc_str="eo"},  -- Esperanto
        {name="es", loc_str="es"},  -- Spanish
        {name="es__traditional", loc_str="es_u_co_trad"}, -- Spanish ('ch' and 'll' as a grapheme)
        {name="et", loc_str="et"},  -- Estonian
        {name="fa", loc_str="fa"},  -- Persian
        {name="fi", loc_str="fi"},  -- Finnish (v and w are primary equal)
        {name="fi__phonebook", loc_str="fi_u_co_phonebk"}, -- Finnish (v and w as separate characters)
        {name="fil", loc_str="fil"}, -- Filipino
        {name="fo", loc_str="fo"},  -- Faroese
        {name="fr_CA", loc_str="fr_CA"}, -- Canadian French
        {name="gu", loc_str="gu"},  -- Gujarati
        {name="ha", loc_str="ha"},  -- Hausa
        {name="haw", loc_str="haw"}, -- Hawaiian
        {name="he", loc_str="he"},  -- Hebrew
        {name="hi", loc_str="hi"},  -- Hindi
        {name="hr", loc_str="hr"},  -- Croatian
        {name="hu", loc_str="hu"},  -- Hungarian
        {name="hy", loc_str="hy"},  -- Armenian
        {name="ig", loc_str="ig"},  -- Igbo
        {name="is", loc_str="is"},  -- Icelandic
        {name="ja", loc_str="ja"},  -- Japanese
        {name="kk", loc_str="kk"},  -- Kazakh
        {name="kl", loc_str="kl"},  -- Kalaallisut
        {name="kn", loc_str="kn"},  -- Kannada
        {name="ko", loc_str="ko"},  -- Korean
        {name="kok", loc_str="kok"}, -- Konkani
        {name="ky", loc_str="ky"},  -- Kyrgyz
        {name="lkt", loc_str="lkt"}, -- Lakota
        {name="ln", loc_str="ln"},  -- Lingala
        {name="lt", loc_str="lt"},  -- Lithuanian
        {name="lv", loc_str="lv"},  -- Latvian
        {name="mk", loc_str="mk"},  -- Macedonian
        {name="ml", loc_str="ml"},  -- Malayalam
        {name="mr", loc_str="mr"},  -- Marathi
        {name="mt", loc_str="mt"},  -- Maltese
        {name="nb", loc_str="nb"},  -- Norwegian Bokmal
        {name="nn", loc_str="nn"},  -- Norwegian Nynorsk
        {name="nso", loc_str="nso"}, -- Northern Sotho
        {name="om", loc_str="om"},  -- Oromo
        {name="or", loc_str="or"},  -- Oriya (Odia)
        {name="pa", loc_str="pa"},  -- Punjabi
        {name="pl", loc_str="pl"},  -- Polish
        {name="ro", loc_str="ro"},  -- Romanian
        {name="sa", loc_str="sa"},  -- Sanskrit
        {name="se", loc_str="se"},  -- Northern Sami
        {name="si", loc_str="si"},  -- Sinhala
        {name="si__dictionary", loc_str="si_u_co_dict"}, -- Sinhala (U+0DA5 = U+0DA2,0DCA,0DA4)
        {name="sk", loc_str="sk"},  -- Slovak
        {name="sl", loc_str="sl"},  -- Slovenian
        {name="sq", loc_str="sq"},  -- Albanian (just "standard")
        {name="sr", loc_str="sr"},  -- Serbian
        {name="sr_Latn", loc_str="sr_Latn"}, -- Serbian in Latin (tailored as Croatian)
        {name="sv", loc_str="sv"},  -- Swedish (v and w are primary equal)
        {name="sv__reformed", loc_str="sv_u_co_reformed"}, -- Swedish (v and w as separate characters)
        {name="ta", loc_str="ta"},  -- Tamil
        {name="te", loc_str="te"},  -- Telugu
        {name="th", loc_str="th"},  -- Thai
        {name="tn", loc_str="tn"},  -- Tswana
        {name="to", loc_str="to"},  -- Tonga
        {name="tr", loc_str="tr"},  -- Turkish
        {name="ug_Cyrl", loc_str="ug"}, -- Uyghur in Cyrillic - is there such locale?
        {name="uk", loc_str="uk"},  -- Ukrainian
        {name="ur", loc_str="ur"},  -- Urdu
        {name="vi", loc_str="vi"},  -- Vietnamese
        {name="vo", loc_str="vo"},  -- Volapük
        {name="wae", loc_str="wae"}, -- Walser
        {name="wo", loc_str="wo"},  -- Wolof
        {name="yo", loc_str="yo"},  -- Yoruba
        {name="zh", loc_str="zh"},  -- Chinese
        {name="zh__big5han", loc_str="zh_u_co_big5han"},  -- Chinese (ideographs: big5 order)
        {name="zh__gb2312han", loc_str="zh_u_co_gb2312"}, -- Chinese (ideographs: GB-2312 order)
        {name="zh__pinyin", loc_str="zh_u_co_pinyin"}, -- Chinese (ideographs: pinyin order)
        {name="zh__stroke", loc_str="zh_u_co_stroke"}, -- Chinese (ideographs: stroke order)
        {name="zh__zhuyin", loc_str="zh_u_co_zhuyin"}, -- Chinese (ideographs: zhuyin order)
    }
    local coll_strengths = {
        {s="s1", opt={strength='primary'}},
        {s="s2", opt={strength='secondary'}},
        {s="s3", opt={strength='tertiary'}}
    }

    local id = 4
    for _, collation in ipairs(coll_lst) do
        for _, strength in ipairs(coll_strengths) do
            local coll_name = 'unicode_' .. collation.name .. "_" .. strength.s
            log.info("creating collation %s", coll_name)
            box.space._collation:replace{id, coll_name, ADMIN, "ICU", collation.loc_str, strength.opt }
            id = id + 1
        end
    end
end

local function upgrade_to_2_1_3()
    upgrade_collation_to_2_1_3()
end

--------------------------------------------------------------------------------
-- Tarantool 2.2.1
--------------------------------------------------------------------------------

-- Add sequence field to _space_sequence table
local function upgrade_sequence_to_2_2_1()
    log.info("add sequence field to space _space_sequence")
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _space_sequence = box.space[box.schema.SPACE_SEQUENCE_ID]
    for _, v in _space_sequence:pairs() do
        if #v > 3 then
            -- Must be a sequence created after upgrade.
            -- It doesn't need to get updated.
            goto continue
        end
        -- Explicitly attach the sequence to the first index part.
        local pk = _index:get{v[1], 0}
        local part = pk[6][1]
        local field = part.field or part[1]
        local path = part.path or ''
        local t = _space_sequence:get(v[1])
        -- Update in-place is banned due to complexity of its
        -- handling. Delete + insert still work.
        t = t:update({{'!', 4, field}, {'!', 5, path}})
        _space_sequence:delete({v[1]})
        _space_sequence:insert(t)
        ::continue::
    end
    _space:update({_space_sequence.id}, {
        {'=', '[7][4]', {name = 'field', type = 'unsigned'}},
        {'=', '[7][5]', {name = 'path', type = 'string'}},
    })
end

local function upgrade_ck_constraint_to_2_2_1()
    -- In previous Tarantool releases check constraints were
    -- stored in space opts. Now we use separate space
    -- _ck_constraint for this purpose. Perform legacy data
    -- migration.
    local MAP = setmap({})
    local _space = box.space._space
    local _index = box.space._index
    local _ck_constraint = box.space._ck_constraint
    log.info("create space _ck_constraint")
    local format = {{name='space_id', type='unsigned'},
                    {name='name', type='string'},
                    {name='is_deferred', type='boolean'},
                    {name='language', type='str'}, {name='code', type='str'}}
    _space:insert{_ck_constraint.id, ADMIN, '_ck_constraint', 'memtx', 0, MAP, format}

    log.info("create index primary on _ck_constraint")
    _index:insert{_ck_constraint.id, 0, 'primary', 'tree',
                  {unique = true}, {{0, 'unsigned'}, {1, 'string'}}}

    for _, space in _space:pairs() do
        local id = space[1]
        local name = space[3]
        local flags = space[6]
        if flags.checks then
            for i, check in pairs(flags.checks) do
                local expr_str = check.expr
                local check_name = check.name or
                                   "CK_CONSTRAINT_" .. i .. "_" .. name
                _ck_constraint:insert({id, check_name, false, 'SQL', expr_str})
            end
            flags.checks = nil
            _space:update({id}, {{'=', 6, flags}})
        end
    end
end

local function create_vcollation_space()
    local _space = box.space._space
    local format = _space:get({box.schema.COLLATION_ID})[7]
    create_sysview(box.schema.COLLATION_ID, box.schema.VCOLLATION_ID)
    _space:update({box.schema.VCOLLATION_ID}, {{'=', 7, format}})
end

local function upgrade_func_to_2_2_1()
    log.info("Update _func format")
    local _func = box.space[box.schema.FUNC_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local datetime = os.date("%Y-%m-%d %H:%M:%S")
    for _, v in box.space._func:pairs() do
        _func:replace({v[1], v[2], v[3], v[4], v[5] or 'LUA', '', 'function',
                      {}, 'any', 'none', 'none', false, false, true,
                      v[15] or {'LUA'}, setmap({}), '', datetime, datetime})
    end
    local sql_builtin_list = {
        "TRIM", "TYPEOF", "PRINTF", "UNICODE", "CHAR", "HEX", "VERSION",
        "QUOTE", "REPLACE", "SUBSTR", "GROUP_CONCAT", "JULIANDAY", "DATE",
        "TIME", "DATETIME", "STRFTIME", "CURRENT_TIME", "CURRENT_TIMESTAMP",
        "CURRENT_DATE", "LENGTH", "POSITION", "ROUND", "UPPER", "LOWER",
        "IFNULL", "RANDOM", "CEIL", "CEILING", "CHARACTER_LENGTH",
        "CHAR_LENGTH", "FLOOR", "MOD", "OCTET_LENGTH", "ROW_COUNT", "COUNT",
        "LIKE", "ABS", "EXP", "LN", "POWER", "SQRT", "SUM", "TOTAL", "AVG",
        "RANDOMBLOB", "NULLIF", "ZEROBLOB", "MIN", "MAX", "COALESCE", "EVERY",
        "EXISTS", "EXTRACT", "SOME", "GREATER", "LESSER", "SOUNDEX",
        "LIKELIHOOD", "LIKELY", "UNLIKELY", "_sql_stat_get", "_sql_stat_push",
        "_sql_stat_init",
    }
    for _, v in pairs(sql_builtin_list) do
        local t = _func:auto_increment({ADMIN, v, 1, 'SQL_BUILTIN', '',
                                       'function', {}, 'any', 'none', 'none',
                                        false, false, true, {}, setmap({}), '',
                                        datetime, datetime})
        _priv:replace{ADMIN, PUBLIC, 'function', t[1], box.priv.X}
    end
    local t = _func:auto_increment({ADMIN, 'LUA', 1, 'LUA',
                        'function(code) return assert(loadstring(code))() end',
                        'function', {'string'}, 'any', 'none', 'none',
                        false, false, true, {'LUA', 'SQL'},
                        setmap({}), '', datetime, datetime})
    _priv:replace{ADMIN, PUBLIC, 'function', t[1], box.priv.X}
    local format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='owner', type='unsigned'}
    format[3] = {name='name', type='string'}
    format[4] = {name='setuid', type='unsigned'}
    format[5] = {name='language', type='string'}
    format[6] = {name='body', type='string'}
    format[7] = {name='routine_type', type='string'}
    format[8] = {name='param_list', type='array'}
    format[9] = {name='returns', type='string'}
    format[10] = {name='aggregate', type='string'}
    format[11] = {name='sql_data_access', type='string'}
    format[12] = {name='is_deterministic', type='boolean'}
    format[13] = {name='is_sandboxed', type='boolean'}
    format[14] = {name='is_null_call', type='boolean'}
    format[15] = {name='exports', type='array'}
    format[16] = {name='opts', type='map'}
    format[17] = {name='comment', type='string'}
    format[18] = {name='created', type='string'}
    format[19] = {name='last_altered', type='string'}
    box.space._space:update({_func.id}, {{'=', 7, format}})
    box.space._index:update(
        {_func.id, _func.index.name.id},
        {{'=', 6, {{field = 2, type = 'string', collation = 2}}}})
end

local function create_func_index()
    log.info("Create _func_index space")
    local _func_index = box.space[box.schema.FUNC_INDEX_ID]
    local _space = box.space._space
    local _index = box.space._index
    local format = {{name='space_id', type='unsigned'},
                    {name='index_id', type='unsigned'},
                    {name='func_id',  type='unsigned'}}
    _space:insert{_func_index.id, ADMIN, '_func_index', 'memtx', 0,
                  setmap({}), format}
    _index:insert{_func_index.id, 0, 'primary', 'tree', {unique = true},
                  {{0, 'unsigned'}, {1, 'unsigned'}}}
    _index:insert{_func_index.id, 1, 'fid', 'tree', {unique = false},
                  {{2, 'unsigned'}}}

end

local function upgrade_to_2_2_1()
    upgrade_sequence_to_2_2_1()
    upgrade_ck_constraint_to_2_2_1()
    create_vcollation_space()
    upgrade_func_to_2_2_1()
    create_func_index()
end

--------------------------------------------------------------------------------
-- Tarantool 2.3.0
--------------------------------------------------------------------------------

local function upgrade_to_2_3_0()
    log.info("Create GREATEST and LEAST SQL Builtins")
    local _space = box.space[box.schema.SPACE_ID]
    local _func = box.space[box.schema.FUNC_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local datetime = os.date("%Y-%m-%d %H:%M:%S")
    local new_builtins = {"GREATEST", "LEAST"}
    for _, v in pairs(new_builtins) do
        local t = _func:auto_increment({ADMIN, v, 1, 'SQL_BUILTIN', '',
                                       'function', {}, 'any', 'none', 'none',
                                        false, false, true, {}, setmap({}), '',
                                        datetime, datetime})
        _priv:replace{ADMIN, PUBLIC, 'function', t[1], box.priv.X}
    end

    log.info("Extend _ck_constraint space format with is_enabled field")
    local _ck_constraint = box.space._ck_constraint
    for _, tuple in _ck_constraint:pairs() do
        _ck_constraint:update({tuple[1], tuple[2]}, {{'=', 6, true}})
    end
    local format = {{name='space_id', type='unsigned'},
                    {name='name', type='string'},
                    {name='is_deferred', type='boolean'},
                    {name='language', type='str'},
                    {name='code', type='str'},
                    {name='is_enabled', type='boolean'}}
    _space:update({_ck_constraint.id}, {{'=', 7, format}})
end

--------------------------------------------------------------------------------
-- Tarantool 2.3.1
--------------------------------------------------------------------------------

local function drop_func_collation()
    local _func = box.space[box.schema.FUNC_ID]
    box.space._index:update({_func.id, _func.index.name.id},
                            {{'=', 6, {{2, 'string'}}}})
end

local function create_session_settings_space()
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local format = {}
    format[1] = {name='name', type='string'}
    format[2] = {name='value', type='any'}
    log.info("create space _session_settings")
    _space:insert{box.schema.SESSION_SETTINGS_ID, ADMIN, '_session_settings',
                  'service', 2, {temporary = true}, format}
    log.info("create index _session_settings:primary")
    _index:insert{box.schema.SESSION_SETTINGS_ID, 0, 'primary', 'tree',
                  {unique = true}, {{0, 'string'}}}
end

local function upgrade_to_2_3_1()
    drop_func_collation()
    create_session_settings_space()
end

--------------------------------------------------------------------------------
-- Tarantool 2.7.1
--------------------------------------------------------------------------------
local function function_access()
    local _func = box.space._func
    local _priv = box.space._priv
    local datetime = os.date("%Y-%m-%d %H:%M:%S")
    local funcs_to_change = {'LUA', 'box.schema.user.info'}
    for _, name in pairs(funcs_to_change) do
        local func = _func.index['name']:get(name)
        if func ~= nil and func.setuid ~= 0 then
            local id = func[1]
            log.info('remove old function "'..name..'"')
            _priv:delete({2, 'function', id})
            _func:delete({id})
            log.info('create function "'..name..'" with unset setuid')
            local new_func = func:update({{'=', 4, 0}, {'=', 18, datetime},
                                          {'=', 19, datetime}})
            _func:replace(new_func)
            log.info('grant execute on function "'..name..'" to public')
            _priv:replace{ADMIN, PUBLIC, 'function', id, box.priv.X}
        end
    end
end

local function upgrade_to_2_7_1()
    function_access()
end

--------------------------------------------------------------------------------
-- Tarantool 2.9.1
--------------------------------------------------------------------------------
local function remove_sql_builtin_functions_from_func()
    local _func = box.space._func
    local _priv = box.space._priv
    for _, v in _func:pairs() do
        local id = v[1]
        local language = v[5]
        if language == "SQL_BUILTIN" then
            _priv:delete({2, 'function', id})
            _func:delete({id})
        end
    end
end

local function upgrade_to_2_9_1()
    remove_sql_builtin_functions_from_func()
end

--------------------------------------------------------------------------------
-- Tarantool 2.10.1
--------------------------------------------------------------------------------
local function grant_rw_access_on__session_settings_to_role_public()
    local _priv = box.space[box.schema.PRIV_ID]
    log.info("grant read,write access on _session_settings space to public role")
    _priv:replace({ADMIN, PUBLIC, 'space', box.schema.SESSION_SETTINGS_ID,
                   box.priv.R + box.priv.W})
end

local function upgrade_to_2_10_1()
    grant_rw_access_on__session_settings_to_role_public()
end

--------------------------------------------------------------------------------
-- Tarantool 2.10.4
--------------------------------------------------------------------------------
local function revoke_execute_access_to_lua_function_from_role_public()
    local _priv = box.space[box.schema.PRIV_ID]
    if box.func.LUA then
        local row = _priv:get{PUBLIC, 'function', box.func.LUA.id}
        local privilege = row and row[5] or nil
        if privilege and bit.band(privilege, box.priv.X) ~= 0 then
            local privilege = bit.bxor(privilege, box.priv.X)
            -- Note that X privilege sometimes implies R privilege,
            -- for example executable functions are visible in _vfunc.
            -- Let's make minimal changes, leaving R privilege instead of X.
            privilege = bit.bor(privilege, box.priv.R)
            log.info("revoke execute access to 'LUA' function from public role")
            _priv:update({PUBLIC, 'function', box.func.LUA.id},
                         {{'=', 5, privilege}})
        end
    end
end

local function make_vfunc_same_format_as_func()
    log.info("Make format of _vfunc the same as the format of _func")
    local format = box.space._space:get({box.schema.FUNC_ID})[7]
    box.space._space:update({box.schema.VFUNC_ID}, {{'=', 7, format}})
end

local function upgrade_to_2_10_4()
    revoke_execute_access_to_lua_function_from_role_public()
    make_vfunc_same_format_as_func()
end

--------------------------------------------------------------------------------
-- Tarantool 2.10.5
--------------------------------------------------------------------------------
local function create_vspace_sequence_space()
    create_sysview(box.schema.SPACE_SEQUENCE_ID, box.schema.VSPACE_SEQUENCE_ID)
end

local function upgrade_to_2_10_5()
    create_vspace_sequence_space()
end

--------------------------------------------------------------------------------
-- Tarantool 2.11.0
--------------------------------------------------------------------------------
local function revoke_write_access_on__collation_from_role_public()
    local _priv = box.space[box.schema.PRIV_ID]
    log.info("revoke write access on _collation space from public role")
    _priv:delete{PUBLIC, 'space', box.schema.COLLATION_ID}
end

local function convert_sql_constraints_to_tuple_constraints()
    local _space = box.space._space
    local _fk = box.space[box.schema.FK_CONSTRAINT_ID]
    local _ck = box.space[box.schema.CK_CONSTRAINT_ID]
    log.info("convert constraints from _ck_constraint and _fk_constraint")
    for _, v in _fk:pairs() do
        local name = v[1]
        local child_id = v[2]
        local parent_id = v[3]
        local child_cols = v[8]
        local parent_cols = v[9]
        local def = _space:get{child_id}
        local mapping = setmap({})
        for i, id in pairs(child_cols) do
            mapping[id] = parent_cols[i]
        end
        local fk = def[6].foreign_key or {}
        fk[name] = {space = parent_id, field = mapping}
        _space:update({child_id}, {{'=', '[6].foreign_key', fk}})
        _fk:delete({name, child_id})
    end
    for _, v in _ck:pairs() do
        local space_id = v[1]
        local name = v[2]
        local code = v[5]
        local _func = box.space._func
        local def = _space:get{space_id}
        local datetime = os.date("%Y-%m-%d %H:%M:%S")
        local func_name = 'check_' .. def[3] .. '_' .. name
        local t = _func:auto_increment({ADMIN, func_name, 1, 'SQL_EXPR', code,
                                       'function', {}, 'any', 'none', 'none',
                                        true, true, true, {'LUA'}, setmap({}),
                                        '', datetime, datetime})
        local ck = def.flags.constraint or {}
        ck[name] = t[1]
        _space:update({space_id}, {{'=', '[6].constraint', ck}})
        _ck:delete({space_id, name})
    end
end

local function add_user_auth_history_and_last_modified()
    log.info("add auth_history and last_modified fields to space _user")
    local _space = box.space[box.schema.SPACE_ID]
    local _user = box.space[box.schema.USER_ID]
    local _vuser = box.space[box.schema.VUSER_ID]
    for _, v in _user:pairs() do
        if #v == 5 then
            _user:update(v[1], {{'=', 6, {}}, {'=', 7, 0}})
        end
    end
    local ops = {
        {'=', '[7][6]', {name = 'auth_history', type = 'array'}},
        {'=', '[7][7]', {name = 'last_modified', type = 'unsigned'}},
    }
    _space:update({_user.id}, ops)
    _space:update({_vuser.id}, ops)
end

local function upgrade_to_2_11_0()
    revoke_write_access_on__collation_from_role_public()
    convert_sql_constraints_to_tuple_constraints()
    add_user_auth_history_and_last_modified()
end

--------------------------------------------------------------------------------
-- Tarantool 2.11.1
--------------------------------------------------------------------------------
local function drop_schema_max_id()
    log.info("drop field max_id in space _schema")
    box.space._schema:delete("max_id")
end

local function upgrade_to_2_11_1()
    drop_schema_max_id()
end

--------------------------------------------------------------------------------
-- Tarantool 2.11.4
--------------------------------------------------------------------------------
local function create_persistent_gc_state(issue_handler)
    -- The function is also called on downgrade
    if issue_handler ~= nil and issue_handler.dry_run then
        return
    end

    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local space_id = box.schema.GC_CONSUMERS_ID
    local opts = {group_id = 1}

    -- We should create space and its index atomically so that we can
    -- rely only on the fact that the space is created
    log.info("open transaction to atomically create persistent WAL GC state")
    box.begin()

    log.info("create space _gc_consumers")
    local format = {{name = 'uuid', type = 'string'},
                    {name = 'vclock', type = 'map', is_nullable = true},
                    {name = 'opts', type = 'map', is_nullable = true}}
    _space:insert{space_id, ADMIN, '_gc_consumers', 'memtx', 0, opts, format}

    log.info("create primary index for space _gc_consumers")
    _index:insert{space_id, 0, 'primary', 'tree', { unique = true },
                  {{0, 'string'}}}

    -- replication can create persistent gc consumers
    log.info("grant read,write on space _gc_consumers to replication")
    local priv = box.priv.R + box.priv.W
    _priv:replace{ADMIN, REPLICATION, 'space', box.schema.GC_CONSUMERS_ID, priv}

    log.info("commit transaction atomically creating persistent WAL GC state")
    box.commit()
end

local function upgrade_to_2_11_4()
    create_persistent_gc_state()
end

--------------------------------------------------------------------------------
-- Tarantool 3.0.0
--------------------------------------------------------------------------------
local function drop_persistent_gc_state(issue_handler)
    -- The function is also called on downgrade
    if issue_handler ~= nil and issue_handler.dry_run then
        return
    end

    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local space_id = box.schema.GC_CONSUMERS_ID

    -- We should drop space and its index atomically so that we can
    -- rely only on the fact that the space is created.
    log.info("open transaction to atomically delete persistent WAL GC state")
    box.begin()

    log.info("dropping all privileges for _gc_consumers")
    for _, v in _priv.index.object:pairs{'space', space_id} do
        -- Extract parts of primary key in space _priv
        local key = {v[2], v[3], v[4]}
        _priv:delete(key)
    end

    log.info("dropping primary index of _gc_consumers")
    _index:delete{space_id, 0}

    log.info("dropping space _gc_consumers")
    _space:delete{space_id}

    log.info("commit transaction atomically deleting persistent WAL GC state")
    box.commit()
end

local function change_replicaset_uuid_key(old_key, new_key)
    local _schema = box.space._schema
    local old = _schema:get{old_key}
    -- 'replicaset_uuid' can be not found during bootstrap.snap generation when
    -- the entire _schema is cleared.
    if old ~= nil then
        _schema:insert{new_key, old[2]}
        _schema:delete{old_key}
    end
end

local function store_replicaset_uuid_in_new_way()
    log.info('update replicaset uuid key')
    change_replicaset_uuid_key('cluster', 'replicaset_uuid')
end

local function add_instance_names()
    log.info('add instance names to _cluster')
    local format = {
        {name = 'id', type = 'unsigned'},
        {name = 'uuid', type = 'string'},
        {name = 'name', type = 'string', is_nullable = true}
    }
    box.space._space:update({box.schema.CLUSTER_ID}, {{'=', 7, format}})
end

local function upgrade_to_3_0_0()
    store_replicaset_uuid_in_new_way()
    add_instance_names()
    drop_persistent_gc_state()
end

--------------------------------------------------------------------------------
-- Tarantool 3.1.0
--------------------------------------------------------------------------------

local function add_trigger_to_func()
    log.info('add trigger to _func')
    local _space = box.space[box.schema.SPACE_ID]
    local _func = box.space[box.schema.FUNC_ID]
    local _vfunc = box.space[box.schema.VFUNC_ID]
    for _, v in _func:pairs() do
        _func:update(v[1], {{'=', 20, {}}})
    end
    local ops = {{'=', '[7][20]', {name = 'trigger', type = 'array'}}}
    _space:update({_func.id}, ops)
    _space:update({_vfunc.id}, ops)
end

local function upgrade_to_3_1_0()
    add_trigger_to_func()
end

--------------------------------------------------------------------------------
-- Tarantool 3.2.0
--------------------------------------------------------------------------------
local function upgrade_to_3_2_0()
    create_persistent_gc_state()
end

--------------------------------------------------------------------------------

local handlers = {
    {version = mkversion(1, 7, 5), func = upgrade_to_1_7_5},
    {version = mkversion(1, 7, 6), func = upgrade_to_1_7_6},
    {version = mkversion(1, 7, 7), func = upgrade_to_1_7_7},
    {version = mkversion(1, 10, 0), func = upgrade_to_1_10_0},
    {version = mkversion(1, 10, 2), func = upgrade_to_1_10_2},
    {version = mkversion(2, 1, 0), func = upgrade_to_2_1_0},
    {version = mkversion(2, 1, 1), func = upgrade_to_2_1_1},
    {version = mkversion(2, 1, 2), func = upgrade_to_2_1_2},
    {version = mkversion(2, 1, 3), func = upgrade_to_2_1_3},
    {version = mkversion(2, 2, 1), func = upgrade_to_2_2_1},
    {version = mkversion(2, 3, 0), func = upgrade_to_2_3_0},
    {version = mkversion(2, 3, 1), func = upgrade_to_2_3_1},
    {version = mkversion(2, 7, 1), func = upgrade_to_2_7_1},
    {version = mkversion(2, 9, 1), func = upgrade_to_2_9_1},
    {version = mkversion(2, 10, 1), func = upgrade_to_2_10_1},
    {version = mkversion(2, 10, 4), func = upgrade_to_2_10_4},
    {version = mkversion(2, 10, 5), func = upgrade_to_2_10_5},
    {version = mkversion(2, 11, 0), func = upgrade_to_2_11_0},
    {version = mkversion(2, 11, 1), func = upgrade_to_2_11_1},
    {version = mkversion(2, 11, 4), func = upgrade_to_2_11_4},
    {version = mkversion(3, 0, 0), func = upgrade_to_3_0_0},
    {version = mkversion(3, 1, 0), func = upgrade_to_3_1_0},
    {version = mkversion(3, 2, 0), func = upgrade_to_3_2_0},
}
builtin.box_init_latest_dd_version_id(handlers[#handlers].version.id)

local trig_oldest_version = nil

-- Some schema changes before version 1.7.7 make it impossible to recover from
-- older snapshot. The table below consists of before_replace triggers on system
-- spaces, which make old snapshot schema compatible with current Tarantool
-- (version 2.x). The triggers replace old format tuples with new ones
-- in-memory, thus making it possible to recover from a rather old snapshot
-- (up to schema version 1.6.8). Once the snapshot is recovered, a normal
-- upgrade procedure may set schema version to the latest one.
--
-- The triggers mostly repeat the corresponding upgrade_to_1_7_x functions,
-- which were used when pre-1.7.x snapshot schema was still recoverable.
--
-- When the triggers are used (i.e. when snapshot schema version is below 1.7.5,
-- the upgrade procedure works as follows:
-- * first the snapshot is recovered and 1.7.5-compatible schema is applied to
--   it in-memory with the help of triggers.
-- * then usual upgrade_to_X_X_X() handlers may be fired to turn schema into the
--   latest one.
local recovery_triggers = {
    {version = mkversion(1, 7, 1), tbl = {
        _user   = user_trig_1_7_1,
    }},
    {version = mkversion(1, 7, 2), tbl = {
        _index = index_trig_1_7_2,
    }},
    {version = mkversion(1, 7, 5), tbl = {
        _space = space_trig_1_7_5,
        _user  = user_trig_1_7_5,
    }},
    {version = mkversion(1, 7, 7), tbl = {
        _priv   = priv_trig_1_7_7,
    }},
}

-- Once newer schema version is recovered (say, from an xlog following the old
-- snapshot), the triggers helping recover the old schema should be removed.
local function schema_trig_last(_, tuple)
    if tuple and tuple[1] == 'version' then
        local version = mkversion.from_tuple(tuple)
        if version then
            log.info("Recovery trigger: recovered schema version %s. "..
                     "Removing outdated recovery triggers.", version)
            box.internal.clear_recovery_triggers(version)
            trig_oldest_version = version
        end
    end
    return tuple
end

recovery_triggers[#recovery_triggers].tbl['_schema'] = schema_trig_last

local function on_init_set_recovery_triggers()
    log.info("Recovering snapshot with schema version %s", trig_oldest_version)
    for _, trig_tbl in ipairs(recovery_triggers) do
        if trig_tbl.version > trig_oldest_version then
            for space, trig in pairs(trig_tbl.tbl) do
                box.space[space]:before_replace(trig)
                log.info("Set recovery trigger on space '%s' to comply with "..
                         "version %s format", space, trig_tbl.version)
            end
        end
    end
end

local function set_recovery_triggers(version)
    trig_oldest_version = version
    box.ctl.on_schema_init(on_init_set_recovery_triggers)
end

local function clear_recovery_triggers(version)
    for _, trig_tbl in ipairs(recovery_triggers) do
        if trig_tbl.version > trig_oldest_version and
           (not version or trig_tbl.version <= version) then
            for space, trig in pairs(trig_tbl.tbl) do
                box.space[space]:before_replace(nil, trig)
                log.info("Remove recovery trigger on space '%s' for version %s",
                         space, trig_tbl.version)
            end
        end
    end
end

local function upgrade_from(version)
    if version < mkversion(1, 6, 8) then
        log.warn('can upgrade from 1.6.8 only')
        return
    end

    for _, handler in ipairs(handlers) do
        if version >= handler.version then
            goto continue
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

-- Runs the given function with the permission to execute DDL operations
-- with an old schema. Used by upgrade/downgrade scripts.
local function run_upgrade(func, ...)
    utils.box_check_configured()
    if builtin.box_schema_upgrade_begin() ~= 0 then
        box.error()
    end
    local ok, err = pcall(func, ...)
    builtin.box_schema_upgrade_end()
    if not ok then
        error(err)
    end
end

local function upgrade()
    local version = mkversion.get()
    run_upgrade(upgrade_from, version)
end

--------------------------------------------------------------------------------
-- Downgrade part
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
-- Tarantool 2.9.1
--------------------------------------------------------------------------------

-- See remove_sql_builtin_functions_from_func and upgrades adding
-- SQL_BUILTIN functions.
local function restore_sql_builtin_functions(issue_handler)
    if issue_handler.dry_run then
        return
    end
    local sql_builtin_list = {
        "TRIM", "TYPEOF", "PRINTF", "UNICODE", "CHAR", "HEX", "VERSION",
        "QUOTE", "REPLACE", "SUBSTR", "GROUP_CONCAT", "JULIANDAY", "DATE",
        "TIME", "DATETIME", "STRFTIME", "CURRENT_TIME", "CURRENT_TIMESTAMP",
        "CURRENT_DATE", "LENGTH", "POSITION", "ROUND", "UPPER", "LOWER",
        "IFNULL", "RANDOM", "CEIL", "CEILING", "CHARACTER_LENGTH",
        "CHAR_LENGTH", "FLOOR", "MOD", "OCTET_LENGTH", "ROW_COUNT", "COUNT",
        "LIKE", "ABS", "EXP", "LN", "POWER", "SQRT", "SUM", "TOTAL", "AVG",
        "RANDOMBLOB", "NULLIF", "ZEROBLOB", "MIN", "MAX", "COALESCE", "EVERY",
        "EXISTS", "EXTRACT", "SOME", "GREATER", "LESSER", "SOUNDEX",
        "LIKELIHOOD", "LIKELY", "UNLIKELY", "_sql_stat_get", "_sql_stat_push",
        "_sql_stat_init", "GREATEST", "LEAST",
    }
    local datetime = os.date("%Y-%m-%d %H:%M:%S")
    local _func = box.space._func
    for _, func in ipairs(sql_builtin_list) do
        if _func.index.name:get(func) == nil then
            local t = _func:auto_increment{
                ADMIN, func, 1, 'SQL_BUILTIN', '', 'function', {}, 'any',
                'none', 'none', false, false, true, {}, setmap({}), '',
                datetime, datetime}
            box.space._priv:replace{ADMIN, PUBLIC, 'function', t.id, box.priv.X}
        end
    end
end

local function downgrade_from_2_9_1(issue_handler)
    restore_sql_builtin_functions(issue_handler)
end

--------------------------------------------------------------------------------
-- Tarantool 2.10.0
--------------------------------------------------------------------------------

-- See tarantool 2.10.0-beta2-169-gb9f6d3858.
local function remove_deferred_deletes(issue_handler)
    if issue_handler.dry_run then
        return
    end
    log.info("remove defer_deletes from space options")
    for _, space in box.space._space:pairs() do
        local new_flags = table.copy(space.flags)
        if new_flags.defer_deletes ~= nil then
            new_flags.defer_deletes = nil
            setmap(new_flags)
            box.space._space:update(space.id, {{'=', 'flags', new_flags}})
        end
    end
end

-- See tarantool-ee 2.10.0-beta2-97-g042a213.
local function disable_background_space_upgrade(issue_handler)
    for _, space in box.space._space:pairs() do
        if space.flags.upgrade then
            issue_handler(
                "Background update is active in space '%s'. " ..
                "It is supported starting from version 2.10.0.",
                space.name)
        end
    end
end

-- See tarantool 2.10.0-beta2-140-ga51313a45.
local function disable_tuple_compression(issue_handler)
    for _, space in box.space._space:pairs() do
        for _, format in pairs(space.format) do
            if format.compression then
                issue_handler(
                    "Tuple compression is found in space '%s', field '%s'. " ..
                    "It is supported starting from version 2.10.0.",
                    space.name, format.name)
            end
        end
    end
end

-- See tarantool 2.10.0-beta2-200-gd950fdde4.
local function disable_core_field_foreign_key_constraints(issue_handler)
    for _, space in box.space._space:pairs() do
        for _, format in pairs(space.format) do
            if format.foreign_key then
                issue_handler(
                    "Foreign key constraint is found in space '%s'," ..
                    " field '%s'. It is supported starting from" ..
                    " version 2.10.0.",
                    space.name, format.name)
            end
        end
    end
end

--
-- Since we cannot distinguish between manually created core tuple foreign keys
-- and core tuple foreign keys generated by SQL foreign key conversion, we must
-- convert all core tuple foreign keys to SQL foreign keys. SQL foreign key were
-- converted to core foreign keys in tarantool 2.11.0-entrypoint-740-g930674810.
--
local function convert_tuple_foreing_keys_to_sql_foreing_keys(issue_handler)
    if issue_handler.dry_run then
        return
    end

    log.info("convert tuple foreign keys to SQL foreign keys")
    local _fk = box.space._fk_constraint
    for _, space in box.space._space:pairs() do
        local new_space = space:totable()
        local is_space_changed = false
        if space.flags.foreign_key then
            for name, value in pairs(space.flags.foreign_key) do
                local parent_id = value.space or space.id
                local child_cols = {}
                local parent_cols = {}
                for k, v in pairs(value.field) do
                    table.insert(child_cols, k)
                    table.insert(parent_cols, v)
                end
                _fk:replace{name, space.id, parent_id, false, "full",
                            "no_action", "no_action", child_cols, parent_cols}
                new_space[6].foreign_key[name] = nil
                is_space_changed = true
            end
            new_space[6].foreign_key = nil
        end
        if is_space_changed then
            box.space._space:replace(new_space)
        end
    end
end

-- See tarantool 2.10.0-beta2-194-ged9b982d3.
local function disable_core_field_constraints(issue_handler)
    for _, space in box.space._space:pairs() do
        for _, format in pairs(space.format) do
            if format.constraint then
                issue_handler(
                    "Field constraint is found in space '%s', field '%s'. " ..
                    "It is supported starting from version 2.10.0.",
                    space.name, format.name)
            end
        end
    end
end

-- See tarantool 2.10.0-beta2-196-g53f5d4e79.
local function disable_core_tuple_constraints(issue_handler)
    for _, space in box.space._space:pairs() do
        if space.flags.constraint then
            for name, _ in pairs(space.flags.constraint) do
                if issue_handler.constraints[name] == nil then
                    issue_handler(
                        "Tuple constraint is found in space '%s'. " ..
                        "It is supported starting from version 2.10.0.",
                        space.name)
                end
            end
        end
    end
end

-- See tarantool 2.10.0-beta1-376-gd2a012455
local function disable_takes_raw_args(issue_handler)
    for _, func in box.space._func:pairs() do
        if func.opts.takes_raw_args then
            issue_handler(
                "takes_raw_args option is set for function '%s'" ..
                " It is supported starting from version 2.10.0.",
                func.name)
        end
    end
end

local function downgrade_from_2_10_0(issue_handler)
    remove_deferred_deletes(issue_handler)
    disable_background_space_upgrade(issue_handler)
    disable_tuple_compression(issue_handler)
    disable_core_field_foreign_key_constraints(issue_handler)
    convert_tuple_foreing_keys_to_sql_foreing_keys(issue_handler)
    disable_core_field_constraints(issue_handler)
    disable_core_tuple_constraints(issue_handler)
    disable_takes_raw_args(issue_handler)
end

--------------------------------------------------------------------------------
-- Tarantool 2.10.5
--------------------------------------------------------------------------------

-- See create_vspace_sequence_space.
local function drop_vspace_sequence_space(issue_handler)
    if issue_handler.dry_run then
        return
    end
    log.info("revoke grants for 'public' role for _vspace_sequence")
    box.space._priv:delete{PUBLIC, 'space', box.schema.VSPACE_SEQUENCE_ID}
    for _, index in box.space._index:pairs(box.schema.VSPACE_SEQUENCE_ID,
                                           {iterator = 'REQ'}) do
        log.info("drop index %s on _vspace_sequence", index[3])
        box.space._index:delete{index[1], index[2]}
    end
    log.info("drop view _vspace_sequence")
    box.space._space:delete{box.schema.VSPACE_SEQUENCE_ID}
end

local function downgrade_from_2_10_5(issue_handler)
    drop_vspace_sequence_space(issue_handler)
end

--------------------------------------------------------------------------------
-- Tarantool 2.11.0
--------------------------------------------------------------------------------

-- See tarantool-ee 2.11.0-entrypoint-97-g67fccd4.
local function disable_zlib_tuple_compression(issue_handler)
    for _, space in box.space._space:pairs() do
        for _, format in pairs(space.format) do
            if format.compression and format.compression == 'zlib' then
                issue_handler(
                    "Tuple compression with 'zlib' algo is found in" ..
                    " space '%s', field '%s'. " ..
                    "It is supported starting from version 2.11.0.",
                    space.name, format.name)
            end
        end
    end
end

--
-- See tarantool 2.11.0-entrypoint-740-g930674810 and
-- tarantool 2.11.0-entrypoint-409-g0dea6493f.
--
local function convert_tuple_constraints_to_sql_check_constraints(issue_handler)
    if not issue_handler.dry_run then
        log.info("convert tuple constraints to SQL check constraints")
    end
    local funcs = {}
    local _ck = box.space._ck_constraint
    for _, space in box.space._space:pairs() do
        local new_space = space:totable()
        local is_space_changed = false
        if space.flags.constraint then
            for name, func_id in pairs(space.flags.constraint) do
                local func = box.space._func:get{func_id}
                if func ~= nil and func.language == 'SQL_EXPR' then
                    funcs[func_id] = true
                    if not issue_handler.dry_run then
                        _ck:replace{space.id, name, false, "SQL", func.body,
                                    true}
                        new_space[6].constraint[name] = nil
                        is_space_changed = true
                    else
                        issue_handler.constraints[name] = true
                    end
                end
            end
            if not issue_handler.dry_run and
               next(new_space[6].constraint) == nil then
                new_space[6].constraint = nil
            end
        end
        if is_space_changed then
            box.space._space:replace(new_space)
            for id, _ in pairs(funcs) do
                box.space._func:delete(id)
                funcs[id] = nil
            end
        end
    end
    for _, func in box.space._func:pairs() do
        if func.language == 'SQL_EXPR' and funcs[func.id] == nil then
            issue_handler(
                "Function '%s' has language type SQL_EXPR. " ..
                "It is supported starting from version 2.11.0.",
                func.name)
        end
    end
end

-- See tarantool-ee 2.11.0-entrypoint-104-ga005915.
local function disable_pap_sha256_auth_method(issue_handler)
    for _, user in box.space._user:pairs() do
        for k in pairs(user.auth) do
            if k == "pap-sha256" then
                issue_handler(
                    "Auth type 'pap-sha256' is found for user '%s'. " ..
                    "It is supported starting from version 2.11.0.",
                    user.name)
            end
        end
    end
end

-- Check corresponding upgrade's add_user_auth_history_and_last_modified.
-- See also tarantool 2.11.0-entrypoint-821-g1c33484d5.
local function remove_user_auth_history_and_last_modified(issue_handler)
    if issue_handler.dry_run then
        return
    end
    log.info("remove auth_history and last_modified fields from space _user")
    local ops = {{'#', '[7][6]', 2}}
    if box.space._space:get(box.space._user.id)[7][6] ~= nil then
        box.space._space:update(box.space._user.id, ops)
    end
    if box.space._space:get(box.space._vuser.id)[7][6] ~= nil then
        box.space._space:update(box.space._vuser.id, ops)
    end
    for _, user in box.space._user:pairs() do
        if #user == 7 then
            box.space._user:update(user[1], {{'#', 6, 2}})
        end
    end
end

local function downgrade_from_2_11_0(issue_handler)
    disable_zlib_tuple_compression(issue_handler)
    convert_tuple_constraints_to_sql_check_constraints(issue_handler)
    disable_pap_sha256_auth_method(issue_handler)
    remove_user_auth_history_and_last_modified(issue_handler)
end

--------------------------------------------------------------------------------
-- Tarantool 2.11.1
--------------------------------------------------------------------------------

-- See drop_schema_max_id.
local function add_schema_max_id(issue_handler)
    if issue_handler.dry_run then
        return
    end
    log.info("add field max_id to space _schema")
    local max_id = box.space._space.index.primary:max()[1]
    if max_id < box.schema.SYSTEM_ID_MAX then
        max_id = box.schema.SYSTEM_ID_MAX
    end
    box.space._schema:replace({"max_id", max_id})
end

local function downgrade_from_2_11_1(issue_handler)
    add_schema_max_id(issue_handler)
end

--------------------------------------------------------------------------------
-- Tarantool 2.11.4
--------------------------------------------------------------------------------

local function downgrade_from_2_11_4(issue_handler)
    drop_persistent_gc_state(issue_handler)
end

--------------------------------------------------------------------------------
-- Tarantool 3.0.0
--------------------------------------------------------------------------------

-- Revert store_replicaset_uuid_in_new_way().
local function store_replicaset_uuid_in_old_way(issue_handler)
    if issue_handler.dry_run then
        return
    end
    log.info('drop replicaset uuid key')
    change_replicaset_uuid_key('replicaset_uuid', 'cluster')
end

-- Global names are stored in spaces. Can't silently delete them. It might break
-- the cluster. The user has to do it manually and carefully.
local function check_names_are_not_set(issue_handler)
    local _schema = box.space._schema
    local msg_suffix = 'name is set. It is supported from version 3.0.0'
    if _schema:get{'cluster_name'} ~= nil then
        issue_handler('Cluster %s', msg_suffix)
    end
    if _schema:get{'replicaset_name'} ~= nil then
        issue_handler('Replicaset %s', msg_suffix)
    end
    local row = box.space._cluster:get{box.info.id}
    if row ~= nil and row.name ~= nil then
        issue_handler('Instance %s', msg_suffix)
    end
end

local function drop_instance_names(issue_handler)
    if issue_handler.dry_run then
        return
    end
    log.info('drop instance names from _cluster format')
    local format = {
        {name = 'id', type = 'unsigned'},
        {name = 'uuid', type = 'string'},
    }
    box.space._space:update({box.schema.CLUSTER_ID}, {{'=', 7, format}})
end

local function downgrade_from_3_0_0(issue_handler)
    store_replicaset_uuid_in_old_way(issue_handler)
    check_names_are_not_set(issue_handler)
    drop_instance_names(issue_handler)
    -- Since persistent gc is part of schema 2.11.4 but not 3.0.0,
    -- it should be created on downgrade to it.
    create_persistent_gc_state(issue_handler)
end

--------------------------------------------------------------------------------
-- Tarantool 3.1.0
--------------------------------------------------------------------------------

local function drop_trigger_from_func(issue_handler)
    local _space = box.space[box.schema.SPACE_ID]
    local _func = box.space[box.schema.FUNC_ID]
    local _vfunc = box.space[box.schema.VFUNC_ID]
    local fmt = 'Function %s is registered as event trigger. ' ..
                'It is supported starting from version 3.1.0'
    if #_func:format() == 19 then
        return
    end
    if not issue_handler.dry_run then
        log.info('drop trigger from _func')
        local ops = {{'#', '[7][20]', 1}}
        _space:update({_func.id}, ops)
        _space:update({_vfunc.id}, ops)
    end
    for _, v in _func:pairs() do
        if #v > 19 and not table.equals(v[20], {}) then
            issue_handler(fmt, v.name)
        end
        if not issue_handler.dry_run then
            _func:update(v[1], {{'#', 20, 1}})
        end
    end
end

local function downgrade_from_3_1_0(issue_handler)
    drop_trigger_from_func(issue_handler)
end

--------------------------------------------------------------------------------
-- Tarantool 3.2.0
--------------------------------------------------------------------------------

local function downgrade_from_3_2_0(issue_handler)
    drop_persistent_gc_state(issue_handler)
end

-- Versions should be ordered from newer to older.
--
-- Every step can be called in 2 modes. In dry_run mode (issue_handler.dry_run
-- is set) step should only check for downgrade issues that cannot be handled
-- without client help. In this mode step should NOT apply any changes.
-- In regular mode (issue_handler.dry_run is not set) step should actually
-- apply the required changes.
--
-- NOTICE: all downgrade steps SHOULD be idempotent.
--
-- We require steps to be idempotent because downgrade steps are run not
-- considering current schema version. For example when downgrade('2.10.0') is
-- run on Tarantool version 2.11.0 then step for 2.10.5 will be applied.
-- It will be applied if schema version is 2.11.0 and it will be applied
-- if schema version is 2.10.0.
--
local downgrade_handlers = {
    {version = mkversion(3, 2, 0), func = downgrade_from_3_2_0},
    {version = mkversion(3, 1, 0), func = downgrade_from_3_1_0},
    {version = mkversion(3, 0, 0), func = downgrade_from_3_0_0},
    {version = mkversion(2, 11, 4), func = downgrade_from_2_11_4},
    {version = mkversion(2, 11, 1), func = downgrade_from_2_11_1},
    {version = mkversion(2, 11, 0), func = downgrade_from_2_11_0},
    {version = mkversion(2, 10, 5), func = downgrade_from_2_10_5},
    {version = mkversion(2, 10, 0), func = downgrade_from_2_10_0},
    {version = mkversion(2, 9, 1), func = downgrade_from_2_9_1},
}

-- This downgrade issue handler is used to raise an error when issue is
-- encountered.
local downgrade_raise_error = {}

local downgrade_raise_error_mt = {
    __call = function(self, fmt, ...)
        error(string.format(fmt, ...))
    end
}

downgrade_raise_error.new = function()
    local handler = {}
    return setmetatable(handler, downgrade_raise_error_mt)
end

-- This downgrade issue handler is used to collect all downgrade issues.
local downgrade_list_issue = {}

local downgrade_list_issue_mt = {
    __call = function(self, fmt, ...)
        table.insert(self.list, string.format(fmt, ...))
    end
}

downgrade_list_issue.new = function()
    local handler = {}
    handler.list = {}
    handler.dry_run = true
    handler.constraints = {}
    return setmetatable(handler, downgrade_list_issue_mt)
end

-- Find schema version which does not require upgrade for given application
-- version. For example:
--
-- app2schema_version(mkversion('2.10.3')) == mkversion('2.10.1')
-- app2schema_version(mkversion('2.10.0')) == mkversion('2.9.1')
local function app2schema_version(app_version)
    local schema_version
    for _, handler in ipairs(handlers) do
        if handler.version > app_version then
            break
        end
        schema_version = handler.version
    end
    return schema_version
end

-- Call required downgrade step given application version we downgrade to.
-- Version should have mkversion type.
local function downgrade_steps(version, issue_handler)
    for _, handler in ipairs(downgrade_handlers) do
        if handler.version < version then
            break
        end
        if handler.version ~= version then
            handler.func(issue_handler)
        end
    end
end

-- List of all Tarantool releases we can downgrade to.
local downgrade_versions = {
    -- DOWNGRADE VERSIONS BEGIN
    "2.8.2",
    "2.8.3",
    "2.8.4",
    "2.10.0",
    "2.10.1",
    "2.10.2",
    "2.10.3",
    "2.10.4",
    "2.10.5",
    "2.11.0",
    "2.11.1",
    "2.11.4",
    "3.0.0",
    "3.1.0",
    "3.2.0",
    -- DOWNGRADE VERSIONS END
}

-- Downgrade or list downgrade issues depending of dry_run argument value.
--
-- If dry_run is true then list downgrade issues.  if dry_run is false then
-- downgrade.
--
-- In case of downgrade check for issues is done before making any changes.
-- If any issue is found then downgrade is failed and no any changes are done.
local function downgrade_impl(version_str, dry_run)
    utils.box_check_configured()
    utils.check_param(version_str, 'version_str', 'string')
    local version = mkversion.from_string(version_str)
    if fun.index(version_str, downgrade_versions) == nil then
        error("Downgrade is only possible to version listed in" ..
              " box.schema.downgrade_versions().")
    end

    local schema_version_cur = mkversion.get()
    local app_version = tarantool.version:match('^%d+%.%d+%.%d+')
    if schema_version_cur > mkversion.from_string(app_version) then
        local err = "Cannot downgrade as current schema version %s is newer" ..
                    " than Tarantool version %s"
        error(err:format(schema_version_cur, app_version))
    end
    local schema_version_dst = app2schema_version(version)
    if schema_version_cur < schema_version_dst then
        local err = "Cannot downgrade as current schema version %s is older" ..
                    " then schema version %s for Tarantool %s"
        error(err:format(schema_version_cur, schema_version_dst, version))
    end

    local issue_handler = downgrade_list_issue.new()
    downgrade_steps(version, issue_handler)
    if dry_run then
        return issue_handler.list
    end

    if #issue_handler.list > 0 then
        local err = issue_handler.list[1]
        if #issue_handler.list > 1 then
            local more = " There are more downgrade issues. To list them" ..
                         " all call box.schema.downgrade_issues."
            err = err .. more
        end
        error(err)
    end

    downgrade_steps(version, downgrade_raise_error.new())

    log.info("set schema version to %s", version)
    box.space._schema:replace{'version',
                              schema_version_dst.major,
                              schema_version_dst.minor,
                              schema_version_dst.patch}
end

local function bootstrap()
    -- Disabling system triggers doesn't turn off space format checks.
    -- Since a system space format may be updated during the bootstrap
    -- sequence, we clear all formats so that we can insert any data
    -- into system spaces and reset them back after we're done.
    clear_system_formats()

    with_disabled_system_triggers(function()
        -- erase current schema
        erase()
        -- insert initial schema
        initial_1_7_5()
        -- upgrade schema to the latest version
        upgrade_from(mkversion(1, 7, 5))
    end)

    reset_system_formats()

    -- save new bootstrap.snap
    box.snapshot()
end

box.schema.upgrade = upgrade
box.schema.downgrade_versions = function()
    return table.copy(downgrade_versions)
end
box.schema.downgrade = function(version)
    run_upgrade(downgrade_impl, version, false)
end
box.schema.downgrade_issues = function(version)
    return downgrade_impl(version, true)
end
box.internal.bootstrap = function()
    run_upgrade(bootstrap)
end
box.internal.schema_needs_upgrade = builtin.box_schema_needs_upgrade
box.internal.get_snapshot_version = get_snapshot_version;
box.internal.set_recovery_triggers = set_recovery_triggers;
box.internal.clear_recovery_triggers = clear_recovery_triggers;

-- Export the run_upgrade() helper to let users perform schema upgrade
-- manually in case box.schema.upgrade() failed.
box.internal.run_schema_upgrade = run_upgrade
