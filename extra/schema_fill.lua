-- Super User ID
GUEST = 0
ADMIN = 1
_schema = box.space[box.schema.SCHEMA_ID]
_space = box.space[box.schema.SPACE_ID]
_index = box.space[box.schema.INDEX_ID]
_func = box.space[box.schema.FUNC_ID]
_user = box.space[box.schema.USER_ID]
_priv = box.space[box.schema.PRIV_ID]
_cluster = box.space[box.schema.CLUSTER_ID]
-- define schema version
_schema:insert{'version', 1, 6}
-- define system spaces
_space:insert{_schema.n, ADMIN, '_schema', 'memtx', 0}
_space:insert{_space.n, ADMIN, '_space', 'memtx', 0}
_space:insert{_index.n, ADMIN, '_index', 'memtx', 0}
_space:insert{_func.n, ADMIN, '_func', 'memtx', 0}
_space:insert{_user.n, ADMIN, '_user', 'memtx', 0}
_space:insert{_priv.n, ADMIN, '_priv', 'memtx', 0}
_space:insert{_cluster.n, ADMIN, '_cluster', 'memtx', 0}
-- define indexes
_index:insert{_schema.n, 0, 'primary', 'tree', 1, 1, 0, 'str'}

-- stick to the following convention:
-- prefer user id (owner id) in field #1
-- prefer object name in field #2
-- index on owner id is index #1
-- index on object name is index #2
--
-- space name is unique
_index:insert{_space.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_space.n, 1, 'owner', 'tree', 0, 1, 1, 'num'}
_index:insert{_space.n, 2, 'name', 'tree', 1, 1, 2, 'str'}

-- index name is unique within a space
_index:insert{_index.n, 0, 'primary', 'tree', 1, 2, 0, 'num', 1, 'num'}
_index:insert{_index.n, 2, 'name', 'tree', 1, 2, 0, 'num', 2, 'str'}
-- user name and id are unique
_index:insert{_user.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_user.n, 2, 'name', 'tree', 1, 1, 2, 'str'}
-- function name and id are unique
_index:insert{_func.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_func.n, 1, 'owner', 'tree', 0, 1, 1, 'num'}
_index:insert{_func.n, 2, 'name', 'tree', 1, 1, 2, 'str'}
--
-- space schema is: grantor id, user id, object_type, object_id, privilege
-- primary key: user id, object type, object id
_index:insert{_priv.n, 0, 'primary', 'tree', 1, 3, 1, 'num', 2, 'str', 3, 'num'}
_index:insert{_priv.n, 1, 'owner', 'tree', 0, 1, 1, 'num'}

-- primary key: node id
_index:insert{_cluster.n, 0, 'primary', 'tree', 1, 1, 0, 'num'}
-- node uuid key: node uuid
_index:insert{_cluster.n, 1, 'uuid', 'tree', 1, 1, 1, 'str'}

-- 
-- Pre-create user and grants
_user:insert{GUEST, '', 'guest'}
_user:insert{ADMIN, '', 'admin'}
