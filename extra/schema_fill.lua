_space:run_triggers(false)
_index:run_triggers(false)
_user:run_triggers(false)
_func:run_triggers(false)
_priv:run_triggers(false)

-- Guest user id - the default user
GUEST = 0
-- Super User ID
ADMIN = 1
-- role 'PUBLIC' is special, it's automatically granted to every user
PUBLIC = 2

EMPTY_MAP = setmetatable({}, { __serialize = 'map' })
--
-- system spaces
--
_schema = box.space[box.schema.SCHEMA_ID]
_space = box.space[box.schema.SPACE_ID]
_index = box.space[box.schema.INDEX_ID]
_func = box.space[box.schema.FUNC_ID]
_user = box.space[box.schema.USER_ID]
_priv = box.space[box.schema.PRIV_ID]
_cluster = box.space[box.schema.CLUSTER_ID]
-- define schema version
_schema:insert{'version', 1, 6}
_schema:insert{'max_id', box.schema.SYSTEM_ID_MAX}
-- define system spaces
--
-- _schema
--
_space:insert{_schema.id, ADMIN, '_schema', 'memtx', 0, EMPTY_MAP, {}}
--
-- _space
--
_space:insert{_space.id, ADMIN, '_space', 'memtx', 0, EMPTY_MAP, {}}
--
-- _index
--
_space:insert{_index.id, ADMIN, '_index', 'memtx', 0, EMPTY_MAP, {}}
--
-- _func
--
_space:insert{_func.id, ADMIN, '_func', 'memtx', 0, EMPTY_MAP, {}}
--
-- _user
--
_space:insert{_user.id, ADMIN, '_user', 'memtx', 0, EMPTY_MAP, {}}
--
-- _priv
--
_space:insert{_priv.id, ADMIN, '_priv', 'memtx', 0, EMPTY_MAP, {}}
--
-- _cluster
--
_space:insert{_cluster.id, ADMIN, '_cluster', 'memtx', 0, EMPTY_MAP, {}}

-- define formats.
-- stick to the following convention:
-- prefer user id (owner id) in field #2 (base-1)
-- prefer object name in field #3 (base-1)
-- 
format={}
format[1] = {type='str', name='key'}
_schema:format(format)
format={}
format[1] = {name='id', type='num'}
format[2] = {name='owner', type='num'}
format[3] = {name='name', type='str'}
format[4] = {name='engine', type='str'}
format[5] = {name='field_count', type='num'}
format[6] = {name='flags', type='str'}
format[7] = {name='format', type='*'}
_space:format(format)
format = {}
format[1] = {name = 'id', type = 'num'}
format[2] = {name = 'iid', type = 'num'}
format[3] = {name = 'name', type = 'str'}
format[4] = {name = 'type', type = 'str'}
format[5] = {name = 'opts', type = 'array'}
format[6] = {name = 'parts', type = 'array'}
_index:format(format)
format={}
format[1] = {name='id', type='num'}
format[2] = {name='owner', type='num'}
format[3] = {name='name', type='str'}
format[4] = {name='setuid', type='num'}
_func:format(format)
format={}
format[1] = {name='id', type='num'}
format[2] = {name='owner', type='num'}
format[3] = {name='name', type='str'}
format[4] = {name='type', type='str'}
format[5] = {name='auth', type='*'}
_user:format(format)
format={}
format[1] = {name='grantor', type='num'}
format[2] = {name='grantee', type='num'}
format[3] = {name='object_type', type='str'}
format[4] = {name='object_id', type='num'}
format[5] = {name='privilege', type='num'}
_priv:format(format)
format = {}
format[1] = {name='id', type='num'}
format[2] = {name='uuid', type='str'}
_cluster:format(format)
-- define indexes
-- stick to the following convention:
-- index on owner id is index #1 (base-0)
-- index on object name is index #2 (base-0)
-- space name is unique
_index:insert{_schema.id, 0, 'primary', 'tree', { unique = true }, {{0, 'str'}}}
_index:insert{_space.id, 0, 'primary', 'tree', { unique = true }, {{0, 'num'}}}
_index:insert{_space.id, 1, 'owner', 'tree', {unique = false }, {{1, 'num'}}}
_index:insert{_space.id, 2, 'name', 'tree', { unique = true }, {{2, 'str'}}}

-- index name is unique within a space
_index:insert{_index.id, 0, 'primary', 'tree', {unique = true}, {{0, 'num'}, {1, 'num'}}}
_index:insert{_index.id, 2, 'name', 'tree', {unique = true}, {{0, 'num'}, {2, 'str'}}}
-- user name and id are unique
_index:insert{_user.id, 0, 'primary', 'tree', {unique = true}, {{0, 'num'}}}
_index:insert{_user.id, 1, 'owner', 'tree', {unique = false}, {{1, 'num'}}}
_index:insert{_user.id, 2, 'name', 'tree', {unique = true}, {{2, 'str'}}}
-- function name and id are unique
_index:insert{_func.id, 0, 'primary', 'tree', {unique = true}, {{0, 'num'}}}
_index:insert{_func.id, 1, 'owner', 'tree', {unique = false}, {{1, 'num'}}}
_index:insert{_func.id, 2, 'name', 'tree', {unique = true}, {{2, 'str'}}}
--
_index:insert{_priv.id, 0, 'primary', 'tree', {unique = true}, {{1, 'num'}, {2, 'str'}, {3, 'num'}}}
-- owner index  - to quickly find all privileges granted by a user
_index:insert{_priv.id, 1, 'owner', 'tree', {unique = false}, {{0, 'num'}}}
-- object index - to quickly find all grants on a given object
_index:insert{_priv.id, 2, 'object', 'tree', {unique = false}, {{2, 'str'}, {3, 'num'}}}
-- primary key: node id
_index:insert{_cluster.id, 0, 'primary', 'tree', {unique = true}, {{0, 'num'}}}
-- node uuid key: node uuid
_index:insert{_cluster.id, 1, 'uuid', 'tree', {unique = true}, {{1, 'str'}}}

--
-- Pre-created users and grants
--
_user:insert{GUEST, ADMIN, 'guest', 'user'}
_user:insert{ADMIN, ADMIN, 'admin', 'user'}
_user:insert{PUBLIC, ADMIN, 'public', 'role'}
-- space schema is: grantor id, user id, object_type, object_id, privilege
-- primary key: user id, object type, object id
RPL_ID = _user:auto_increment{ADMIN, 'replication', 'role'}[1]
-- grant admin access to the universe
_priv:insert{1, 1, 'universe', 0, 7}
-- grant 'public' role access to 'box.schema.user.info' function
_func:insert{1, 1, 'box.schema.user.info', 1}
_priv:insert{1, 2, 'function', 1, 4}
-- replication can read the entire universe
_priv:insert{1, RPL_ID, 'universe', 0, 1}
-- replication can append to '_cluster' system space
_priv:insert{1, RPL_ID, 'space', box.schema.CLUSTER_ID, 2}
-- grant role 'public' to 'guest'
_priv:insert{1, 0, 'role', 2, 4}

--
-- Create system views
--

_space:run_triggers(true)

local views = {
    [box.schema.SPACE_ID] = box.schema.VSPACE_ID;
    [box.schema.INDEX_ID] = box.schema.VINDEX_ID;
    [box.schema.USER_ID] = box.schema.VUSER_ID;
    [box.schema.FUNC_ID] = box.schema.VFUNC_ID;
    [box.schema.PRIV_ID] = box.schema.VPRIV_ID;
}

--
-- create definitions for system views, and grant
-- privileges on system views to 'PUBLIC' role
--

for source_id, target_id in pairs(views) do
    local def = _space:get(source_id):totable()
    def[1] = target_id
    def[3] = "_v"..def[3]:sub(2)
    def[4] = 'sysview'
    _space:insert(def)
    local idefs = {}
    for _, idef in _index:pairs(source_id, { iterator = 'EQ'}) do
        idef = idef:totable()
        idef[1] = target_id
        table.insert(idefs, idef)
    end
    for _, idef in ipairs(idefs) do
        _index:insert(idef)
    end
    -- public can read system views
    _priv:insert{1, PUBLIC, 'space', target_id, 1}
end

_space:run_triggers(true)
_index:run_triggers(true)
_user:run_triggers(true)
_func:run_triggers(true)
_priv:run_triggers(true)
