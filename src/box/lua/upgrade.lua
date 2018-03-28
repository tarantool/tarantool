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

local function create_sysview(def, target_id)
    --
    -- Create definitions for the system view, and grant
    -- privileges on system views to 'PUBLIC' role
    --
    def = def:totable()
    local source_id = def[1]
    def[1] = target_id
    def[3] = "_v"..def[3]:sub(2)
    def[4] = 'sysview'
    log.info("create view %s...", def[3])
    box.space._space:replace(def)
    local idefs = {}
    for _, idef in box.space._index:pairs(source_id, { iterator = 'EQ'}) do
        idef = idef:totable()
        idef[1] = target_id
        table.insert(idefs, idef)
    end
    for _, idef in ipairs(idefs) do
        log.info("create index %s on %s", idef[3], def[3])
        box.space._index:replace(idef)
    end
    -- public can read system views
    log.info("grant read access to 'public' role for %s view", def[3])
    box.space._priv:insert({ADMIN, PUBLIC, 'space', target_id, 1})
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
    local def = _space:insert{_space.id, ADMIN, '_space', 'memtx', 0, MAP, format}
    -- space name is unique
    log.info("create index primary on _space")
    _index:insert{_space.id, 0, 'primary', 'tree', { unique = true }, {{0, 'unsigned'}}}
    log.info("create index owner on _space")
    _index:insert{_space.id, 1, 'owner', 'tree', {unique = false }, {{1, 'unsigned'}}}
    log.info("create index index name on _space")
    _index:insert{_space.id, 2, 'name', 'tree', { unique = true }, {{2, 'string'}}}
    create_sysview(def, box.schema.VSPACE_ID)

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
    def = _space:insert{_index.id, ADMIN, '_index', 'memtx', 0, MAP, format}
    -- index name is unique within a space
    log.info("create index primary on _index")
    _index:insert{_index.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}, {1, 'unsigned'}}}
    log.info("create index name on _index")
    _index:insert{_index.id, 2, 'name', 'tree', {unique = true}, {{0, 'unsigned'}, {2, 'string'}}}
    create_sysview(def, box.schema.VINDEX_ID)

    --
    -- _func
    --
    log.info("create space _func")
    format = {}
    format[1] = {name='id', type='unsigned'}
    format[2] = {name='owner', type='unsigned'}
    format[3] = {name='name', type='string'}
    format[4] = {name='setuid', type='unsigned'}
    def = _space:insert{_func.id, ADMIN, '_func', 'memtx', 0, MAP, format}
    -- function name and id are unique
    log.info("create index _func:primary")
    _index:insert{_func.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}}
    log.info("create index _func:owner")
    _index:insert{_func.id, 1, 'owner', 'tree', {unique = false}, {{1, 'unsigned'}}}
    log.info("create index _func:name")
    _index:insert{_func.id, 2, 'name', 'tree', {unique = true}, {{2, 'string'}}}
    create_sysview(def, box.schema.VFUNC_ID)

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
    def = _space:insert{_user.id, ADMIN, '_user', 'memtx', 0, MAP, format}
    -- user name and id are unique
    log.info("create index _func:primary")
    _index:insert{_user.id, 0, 'primary', 'tree', {unique = true}, {{0, 'unsigned'}}}
    log.info("create index _func:owner")
    _index:insert{_user.id, 1, 'owner', 'tree', {unique = false}, {{1, 'unsigned'}}}
    log.info("create index _func:name")
    _index:insert{_user.id, 2, 'name', 'tree', {unique = true}, {{2, 'string'}}}
    create_sysview(def, box.schema.VUSER_ID)

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
    def = _space:insert{_priv.id, ADMIN, '_priv', 'memtx', 0, MAP, format}
    -- user id, object type and object id are unique
    log.info("create index primary on _priv")
    _index:insert{_priv.id, 0, 'primary', 'tree', {unique = true}, {{1, 'unsigned'}, {2, 'string'}, {3, 'unsigned'}}}
    -- owner index  - to quickly find all privileges granted by a user
    log.info("create index owner on _priv")
    _index:insert{_priv.id, 1, 'owner', 'tree', {unique = false}, {{0, 'unsigned'}}}
    -- object index - to quickly find all grants on a given object
    log.info("create index object on _priv")
    _index:insert{_priv.id, 2, 'object', 'tree', {unique = false}, {{2, 'string'}, {3, 'unsigned'}}}
    create_sysview(def, box.schema.VPRIV_ID)

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
    _priv:insert{ADMIN, ADMIN, 'universe', 0, 7}

    -- grant role 'public' to 'guest'
    log.info("grant role public to guest")
    _priv:insert{ADMIN, GUEST, 'role', PUBLIC, 4}

    -- replication can read the entire universe
    log.info("grant read on universe to replication")
    _priv:replace{ADMIN, REPLICATION, 'universe', 0, 1}
    -- replication can append to '_cluster' system space
    log.info("grant write on space _cluster to replication")
    _priv:replace{ADMIN, REPLICATION, 'space', _cluster.id, 2}

    _priv:insert{ADMIN, PUBLIC, 'space', _truncate.id, 2}

    -- create "box.schema.user.info" function
    log.info('create function "box.schema.user.info" with setuid')
    _func:replace{1, ADMIN, 'box.schema.user.info', 1, 'LUA'}

    -- grant 'public' role access to 'box.schema.user.info' function
    log.info('grant execute on function "box.schema.user.info" to public')
    _priv:replace{ADMIN, PUBLIC, 'function', 1, 4}

    log.info("set max_id to box.schema.SYSTEM_ID_MAX")
    _schema:insert{'max_id', box.schema.SYSTEM_ID_MAX}

    log.info("set schema version to 1.7.5")
    _schema:insert({'version', 1, 7, 5})
end

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
    _space:insert{_sequence.id, ADMIN, '_sequence', 'memtx', 0, MAP,
                  {{name = 'id', type = 'unsigned'},
                   {name = 'owner', type = 'unsigned'},
                   {name = 'name', type = 'string'},
                   {name = 'step', type = 'integer'},
                   {name = 'min', type = 'integer'},
                   {name = 'max', type = 'integer'},
                   {name = 'start', type = 'integer'},
                   {name = 'cache', type = 'integer'},
                   {name = 'cycle', type = 'boolean'}}}
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
    _priv:insert{ADMIN, PUBLIC, 'space', _collation.id, 2}
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
            _priv:upsert({ADMIN, v[1], "universe", 0, 24}, {{"|", 5, 24}})
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
            _priv:update({v[2], v[3], v[4]}, {{ "|", 5, 32}})
        end
    end
    -- grant admin all new privileges (session, usage, grant option,
    -- create, alter, drop and anything that might come up in the future
    --
    _priv:upsert({ADMIN, ADMIN, 'universe', 0, 4294967295},
                 {{ "|", 5, 4294967295}})
    --
    -- create role 'super' and grant it all privileges on universe
    --
    _user:replace{SUPER, ADMIN, 'super', 'role', setmap({})}
    _priv:replace({ADMIN, SUPER, 'universe', 0, 4294967295})
end

--------------------------------------------------------------------------------
-- Tarantool 1.8.2
--------------------------------------------------------------------------------

local function upgrade_to_1_8_2()
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local _trigger = box.space[box.schema.TRIGGER_ID]
    local format = {{name='name', type='string'},
                    {name='opts', type='map'}}

    log.info("create space _trigger")
    _space:insert{_trigger.id, ADMIN, '_trigger', 'memtx', 0, setmap({}), {}}
    log.info("create index primary on _trigger")
    _index:insert{_trigger.id, 0, 'primary', 'tree', { unique = true },
        {{0, 'string'}}}

    log.info("alter space _trigger set format")
    _trigger:format(format)
end

--------------------------------------------------------------------------------
-- Tarantool 1.8.4
--------------------------------------------------------------------------------

local function upgrade_to_1_8_4()
    local _space = box.space[box.schema.SPACE_ID]
    local _index = box.space[box.schema.INDEX_ID]
    local stat1_ft = {{name='tbl', type='string'},
                      {name='idx', type='string'},
	              {name='stat', type='string'}}
    local stat4_ft = {{name='tbl', type='string'},
                      {name='idx', type='string'},
                      {name='neq', type='string'},
                      {name='nlt', type='string'},
                      {name='ndlt', type='string'},
                      {name='sample', type='scalar'}}
    local MAP = setmap({})

    log.info("create space _sql_stat1")
    _space:insert{box.schema.SQL_STAT1_ID, ADMIN, '_sql_stat1', 'memtx', 0,
                  MAP, stat1_ft}

    log.info("create index primary on _sql_stat1")
    _index:insert{box.schema.SQL_STAT1_ID, 0, 'primary', 'tree',
                  {unique = true}, {{0, 'string'}, {1, 'string'}}}

    log.info("create space _sql_stat4")
    _space:insert{box.schema.SQL_STAT4_ID, ADMIN, '_sql_stat4', 'memtx', 0,
                  MAP, stat4_ft}

    log.info("create index primary on _sql_stat4")
    _index:insert{box.schema.SQL_STAT4_ID, 0, 'primary', 'tree',
                  {unique = true}, {{0, 'string'}, {1, 'string'},
                  {5, 'scalar'}}}

    -- Nullability wasn't skipable. This was fixed in 1-7.
    -- Now, abscent field means NULL, so we can safely set second
    -- field in format, marking it nullable.
    log.info("Add nullable value field to space _schema")
    local format = {}
    format[1] = {type='string', name='key'}
    format[2] = {type='any', name='value', is_nullable=true}
    box.space._schema:format(format)
end

--------------------------------------------------------------------------------


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
    if version < mkversion(1, 7, 5) then
        log.warn('can upgrade from 1.7.5 only')
        return
    end

    local handlers = {
        {version = mkversion(1, 7, 6), func = upgrade_to_1_7_6, auto = true},
        {version = mkversion(1, 7, 7), func = upgrade_to_1_7_7, auto = true},
        {version = mkversion(1, 8, 2), func = upgrade_to_1_8_2, auto = true},
        {version = mkversion(1, 8, 4), func = upgrade_to_1_8_4, auto = true},
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
