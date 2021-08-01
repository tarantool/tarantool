local log = require('log')
local bit = require('bit')
local json = require('json')
local fio = require('fio')
local xlog = require('xlog')

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

local function version_from_tuple(tuple)
    local major, minor, patch = tuple:unpack(2, 4)
    patch = patch or 0
    if major and minor and type(major) == 'number' and
       type(minor) == 'number' and type(patch) == 'number' then
        return mkversion(major, minor, patch)
    end
    return nil
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
                version = version_from_tuple(tuple)
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
    local datetime = os.date("%Y-%m-%d %H:%M:%S")

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
    _func:replace({1, ADMIN, 'box.schema.user.info', 1, 'LUA', '', 'function',
                  {}, 'any', 'none', 'none', false, false, true, {'LUA'},
                  MAP, '', datetime, datetime})

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
    box.space._vsequence:format(sequence_format)
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
    local format = _priv:format()

    format[4].type = 'scalar'
    _priv:format(format)
    format = _vpriv:format()
    format[4].type = 'scalar'
    _vpriv:format(format)
    _priv.index.primary:alter{parts={2, 'unsigned', 3, 'string', 4, 'scalar'}}
    _vpriv.index.primary:alter{parts={2, 'unsigned', 3, 'string', 4, 'scalar'}}
    _priv.index.object:alter{parts={3, 'string', 4, 'scalar'}}
    _vpriv.index.object:alter{parts={3, 'string', 4, 'scalar'}}
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
    box.space._schema:format(format)

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
        local opts = index.opts
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
        if collation.type == 'ICU' and collation.opts.strength == nil then
            local new_collation = collation:totable()
            new_collation[6].strength = 'tertiary'
            _collation:delete{collation.id}
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
    local format = _space_sequence:format()
    format[4] = {name = 'field', type = 'unsigned'}
    format[5] = {name = 'path', type = 'string'}
    _space_sequence:format(format)
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
        local flags = space.flags
        if flags.checks then
            for i, check in pairs(flags.checks) do
                local expr_str = check.expr
                local check_name = check.name or
                                   "CK_CONSTRAINT_"..i.."_"..space.name
                _ck_constraint:insert({space.id, check_name, false,
                                       'SQL', expr_str})
            end
            flags.checks = nil
            _space:replace({space.id, space.owner, space.name, space.engine,
                            space.field_count, flags, space.format})
        end
    end
end

local function create_vcollation_space()
    local _collation = box.space._collation
    local format = _collation:format()
    create_sysview(box.schema.COLLATION_ID, box.schema.VCOLLATION_ID)
    box.space[box.schema.VCOLLATION_ID]:format(format)
end

local function upgrade_func_to_2_2_1()
    log.info("Update _func format")
    local _func = box.space[box.schema.FUNC_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local datetime = os.date("%Y-%m-%d %H:%M:%S")
    for _, v in box.space._func:pairs() do
        box.space._func:replace({v.id, v.owner, v.name, v.setuid, v[5] or 'LUA',
                                 '', 'function', {}, 'any', 'none', 'none',
                                 false, false, true, v[15] or {'LUA'},
                                 setmap({}), '', datetime, datetime})
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
        _priv:replace{ADMIN, PUBLIC, 'function', t.id, box.priv.X}
    end
    local t = _func:auto_increment({ADMIN, 'LUA', 1, 'LUA',
                        'function(code) return assert(loadstring(code))() end',
                        'function', {'string'}, 'any', 'none', 'none',
                        false, false, true, {'LUA', 'SQL'},
                        setmap({}), '', datetime, datetime})
    _priv:replace{ADMIN, PUBLIC, 'function', t.id, box.priv.X}
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
    _func:format(format)
    _func.index.name:alter({parts = {{'name', 'string',
                                      collation = 'unicode_ci'}}})
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
    local _func = box.space[box.schema.FUNC_ID]
    local _priv = box.space[box.schema.PRIV_ID]
    local datetime = os.date("%Y-%m-%d %H:%M:%S")
    local new_builtins = {"GREATEST", "LEAST"}
    for _, v in pairs(new_builtins) do
        local t = _func:auto_increment({ADMIN, v, 1, 'SQL_BUILTIN', '',
                                       'function', {}, 'any', 'none', 'none',
                                        false, false, true, {}, setmap({}), '',
                                        datetime, datetime})
        _priv:replace{ADMIN, PUBLIC, 'function', t.id, box.priv.X}
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
    _ck_constraint:format(format)
end

--------------------------------------------------------------------------------
-- Tarantool 2.3.1
--------------------------------------------------------------------------------

local function drop_func_collation()
    local _func = box.space[box.schema.FUNC_ID]
    _func.index.name:alter({parts = {{'name', 'string'}}})
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
            local id = func.id
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

local handlers = {
    {version = mkversion(1, 7, 5), func = upgrade_to_1_7_5, auto=true},
    {version = mkversion(1, 7, 6), func = upgrade_to_1_7_6, auto = true},
    {version = mkversion(1, 7, 7), func = upgrade_to_1_7_7, auto = true},
    {version = mkversion(1, 10, 0), func = upgrade_to_1_10_0, auto = true},
    {version = mkversion(1, 10, 2), func = upgrade_to_1_10_2, auto = true},
    {version = mkversion(2, 1, 0), func = upgrade_to_2_1_0, auto = true},
    {version = mkversion(2, 1, 1), func = upgrade_to_2_1_1, auto = true},
    {version = mkversion(2, 1, 2), func = upgrade_to_2_1_2, auto = true},
    {version = mkversion(2, 1, 3), func = upgrade_to_2_1_3, auto = true},
    {version = mkversion(2, 2, 1), func = upgrade_to_2_2_1, auto = true},
    {version = mkversion(2, 3, 0), func = upgrade_to_2_3_0, auto = true},
    {version = mkversion(2, 3, 1), func = upgrade_to_2_3_1, auto = true},
    {version = mkversion(2, 7, 1), func = upgrade_to_2_7_1, auto = true},
}

-- Schema version of the snapshot.
local function get_version()
    local version = box.space._schema:get{'version'}
    if version == nil then
        error('Missing "version" in box.space._schema')
    end
    local major = version[2]
    local minor = version[3]
    local patch = version[4] or 0

    return mkversion(major, minor, patch),
           string.format("%s.%s.%s", major, minor, patch)
end

local function schema_needs_upgrade()
    -- Schema needs upgrade if current schema version is greater
    -- than schema version of the snapshot.
    local schema_version, schema_version_str = get_version()
    if schema_version ~= nil and
        handlers[#handlers].version > schema_version then
        return true, schema_version_str
    end
    return false
end

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
        local version = version_from_tuple(tuple)
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

local function upgrade(options)
    options = options or {}
    setmetatable(options, {__index = {auto = false}})

    local version = get_version()
    if version < mkversion(1, 6, 8) then
        log.warn('can upgrade from 1.6.8 only')
        return
    end

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

    -- erase current schema
    erase()
    -- insert initial schema
    initial_1_7_5()
    -- upgrade schema to the latest version
    upgrade()

    set_system_triggers(true)

    -- save new bootstrap.snap
    box.snapshot()
end

box.schema.upgrade = upgrade;
box.internal.bootstrap = bootstrap;
box.internal.schema_needs_upgrade = schema_needs_upgrade;
box.internal.get_snapshot_version = get_snapshot_version;
box.internal.set_recovery_triggers = set_recovery_triggers;
box.internal.clear_recovery_triggers = clear_recovery_triggers;
