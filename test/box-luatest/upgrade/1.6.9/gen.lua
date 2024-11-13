--
-- This script creates a snapshot on Tarantool 1.6.9-94-ga5da60bdd.
-- Such snapshot should have all possible rows in system spaces.
-- Available system spaces on 1.6: _cluster, _func, _index, _priv, _schema,
-- _space, _user.
--

box.cfg{}

-- Create all possible spaces.
local f = {
    {'a', type = 'number'},
    {'b', 'num'},
    {name = 'c', type = 'array'},
    {'d', type = 'array'},
    {'e'},
}
box.schema.space.create('fc', {field_count = 5})
box.schema.space.create('user', {user = 'guest'})
box.schema.space.create('format', {format = f})
box.schema.space.create('fc_user', {field_count = 5, user = 'guest'})
box.schema.space.create('fc_format', {field_count = 5, format = f})
box.schema.space.create('user_format', {user = 'guest', format = f})
box.schema.space.create('all', {field_count = 5, user = 'guest', format = f})

-- Create all possible indexes.
box.space['all']:create_index('pk')
box.space['all']:create_index('hash', {type = 'hash', parts = {2, 'num'}})
box.space['all']:create_index('rtree', {type = 'rtree', parts = {3, 'array'}})

box.space['format']:create_index('pk')
box.space['format']:create_index('bs', {type = 'bitset', parts = {2, 'num'}})
box.space['format']:create_index('rtree_1',
    {type = 'rtree', parts = {3, 'array'}, dimension = 4})
box.space['format']:create_index('rtree_2',
    {type = 'rtree', parts = {4, 'array'}, distance = 'manhattan'})

-- Create function.
box.schema.func.create('calculate', {language = 'LUA'})

-- Create roles and users.
box.schema.role.create('test_role')
box.schema.role.grant('test_role', 'read,write', 'space', 'fc')
box.schema.user.create('user1')
box.schema.user.create('user2', {password = 'passwd'})
box.schema.user.grant('user1', 'execute', 'function', 'calculate')
box.schema.user.grant('user2', 'test_role')

-- DDL is disabled until schema is upgraded (see gh-7149) so we have to grant
-- permissions required to run the test manually.

-- `super` role still doesn't exist here, so let's create it. Since
-- `role.grant` doesn't allow to specify id, we manually insert into _user.
box.space._user:insert{31, 1, "super", "role"}
-- Since box.priv.ALL is different on the latest version rather than on 1.6.8,
-- manually insert 'super' role in the _priv and grant it all privileges to
-- the 'universe'.
box.space._priv:insert{1, 31, 'universe', 0, 4294967295}
box.schema.user.grant('guest', 'super')

box.snapshot()
os.exit(0)
