session = box.session
utils = require('utils')
EMPTY_MAP = utils.setmap({})

--
-- Check a double create space
--
s = box.schema.space.create('test')
s = box.schema.space.create('test')
--
-- Check a double drop space
--
s:drop()
s:drop()
--
-- Check double create user
--
box.schema.user.create('testus')
box.schema.user.create('testus')

s = box.schema.space.create('admin_space')
index = s:create_index('primary', {type = 'hash', parts = {1, 'unsigned'}})
s:insert({1})
s:insert({2})
--
-- Check double grant and read access
--
box.schema.user.grant('testus', 'read', 'space', 'admin_space')
box.schema.user.grant('testus', 'read', 'space', 'admin_space')

session.su('testus')
s:select(1)
s:insert({3})
s:delete(1)
s:drop()
--
-- Check double revoke
--
session.su('admin')
box.schema.user.revoke('testus', 'read', 'space', 'admin_space')
box.schema.user.revoke('testus', 'read', 'space', 'admin_space')

session.su('testus')
s:select(1)
session.su('admin')
--
-- Check write access on space
--
box.schema.user.grant('testus', 'write', 'space', 'admin_space')

session.su('testus')
s:select(1)
s:delete(1)
s:insert({3})
s:drop()
session.su('admin')
--
-- Check double drop user
--
box.schema.user.drop('testus')
box.schema.user.drop('testus')
--
-- Check 'guest' user
--
session.su('guest')
session.uid()
box.space._user:select(1)
s:select(1)
s:insert({4})
s:delete({3})
s:drop()
gs = box.schema.space.create('guest_space')
--
-- FIXME: object create calls system space auto_increment, which requires
-- read and write privileges. Create privilege must solve this.
--
box.schema.func.create('guest_func')
session.su('admin', box.schema.user.grant, "guest", "read", "universe")
box.schema.func.create('guest_func')
session.su('admin')
box.schema.user.revoke("guest", "read", "universe")

s:select()
--
-- Create user with universe read&write grants
-- and create this user session
--
box.schema.user.create('uniuser')
box.schema.user.grant('uniuser', 'read, write, execute, create', 'universe')
session.su('uniuser')
uid = session.uid()
--
-- Check universal user
-- Check delete currently authenticated user
--
box.schema.user.drop('uniuser')
--
--Check create, call and drop function
--
box.schema.func.create('uniuser_func')
function uniuser_func() return 'hello' end
uniuser_func()
box.schema.func.drop('uniuser_func')
--
-- Check create and drop space
--
us = box.schema.space.create('uniuser_space')
us:drop()
--
-- Check create and drop user
--
box.schema.user.create('uniuser_testus')
box.schema.user.drop('uniuser_testus')
--
-- Check access system and any spaces
--
box.space.admin_space:select()
box.space._user:select(1)
box.space._space:select(280)

us = box.schema.space.create('uniuser_space')
box.schema.func.create('uniuser_func')

session.su('admin')
box.schema.user.create('someuser')
box.schema.user.grant('someuser', 'read, write, execute, create', 'universe')
session.su('someuser')
--
-- Check drop objects of another user
--
s:drop()
us:drop()
box.schema.func.drop('uniuser_func')
box.schema.user.drop('uniuser_testus')

session.su('admin')
box.schema.func.drop('uniuser_func')
box.schema.user.drop('someuser')
box.schema.user.drop('uniuser_testus')
box.schema.user.drop('uniuser')
_ = box.space._user:delete(uid)
s:drop()
--
-- Check write grant on _user
--
box.schema.user.create('testuser')
maxuid = box.space._user.index.primary:max()[1]

box.schema.user.grant('testuser', 'write', 'space', '_user')
box.schema.user.grant('testuser', 'create', 'universe')
session.su('testuser')
testuser_uid = session.uid()
_ = box.space._user:delete(2)
box.space._user:select(1)
uid = box.space._user:insert{maxuid+1, session.uid(), 'someone', 'user', EMPTY_MAP}[1]
_ = box.space._user:delete(uid)

session.su('admin')
box.space._user:select(1)
_ = box.space._user:delete(testuser_uid)
box.schema.user.revoke('testuser', 'write', 'space', '_user')
--
-- Check read grant on _user
--
box.schema.user.grant('testuser', 'read', 'space', '_user')
session.su('testuser')
_  = box.space._user:delete(2)
box.space._user:select(1)
box.space._user:insert{uid, session.uid(), 'someone2', 'user'}

session.su('admin')
--
-- Check read grant on _index
--
box.schema.user.grant('testuser', 'read', 'space', '_index')
session.su('testuser')
box.space._index:select(272)
box.space._index:insert{512, 1,'owner','tree', 1, 1, 0,'unsigned'}



session.su('admin')
box.schema.user.revoke('testuser', 'usage,session', 'universe')
box.schema.user.revoke('testuser', 'read, write, execute', 'universe')
box.schema.user.revoke('testuser', 'create', 'universe')
box.schema.user.grant('testuser', 'usage,session', 'universe')
--
-- Check that itertors check privileges
--
s = box.schema.space.create('glade')
box.schema.user.grant('testuser', 'read', 'space', 'glade')
index = s:create_index('primary', {unique = true, parts = {1, 'unsigned', 2, 'string'}})
s:insert({1, 'A'})
s:insert({2, 'B'})
s:insert({3, 'C'})
s:insert({4, 'D'})

t = {}
for key, v in s.index.primary:pairs(3, {iterator = 'GE'}) do table.insert (t, v) end
t
t = {}
session.su('testuser')
s:select()
for key, v in s.index.primary:pairs(3, {iterator = 'GE'}) do table.insert (t, v) end
t
t = {}
session.su('admin')
box.schema.user.revoke('testuser', 'read', 'space', 'glade')
box.schema.user.grant('testuser', 'write', 'space', 'glade')
session.su('testuser')
s:select()
for key, v in s.index.primary:pairs(1, {iterator = 'GE'}) do table.insert (t, v) end
t
t = {}
session.su('admin')
box.schema.user.grant('testuser', 'read, write, execute', 'space', 'glade')
session.su('testuser')
s:select()
for key, v in s.index.primary:pairs(3, {iterator = 'GE'}) do table.insert (t, v) end
t
t = {}

session.su('guest')
s:select()
for key, v in s.index.primary:pairs(3, {iterator = 'GE'}) do table.insert (t, v) end
t
t = {}

session.su('guest')
s:select()
for key, v in s.index.primary:pairs(3, {iterator = 'GE'}) do table.insert (t, v) end
t

--
-- Check that alter and truncate do not affect space access control.
--
session.su('admin')
_ = s:create_index('secondary', {unique = false, parts = {2, 'string'}})

session.su('testuser')
s:select()

session.su('admin')
s:truncate()
s:insert({1234, 'ABCD'})

session.su('testuser')
s:select()

session.su('admin')
box.schema.user.drop('testuser')

s:drop()

--
-- gh-3089 usage access is not applied to owner
--
box.schema.user.grant("guest","read, write, execute, create", "universe")
box.session.su("guest")
s = box.schema.space.create("test")
_ = s:create_index("prim")
test_func = function() end
box.schema.func.create('test_func')
sq = box.schema.sequence.create("test")
box.session.su("admin")
box.schema.user.revoke("guest", "usage", "universe")
box.session.su("guest")

s:select{}
s:drop()
sq:set(100)
sq:drop()
c = require("net.box").connect(os.getenv("LISTEN"))
c:call("test_func")

box.session.su("admin")
box.schema.user.revoke("guest","read, write, execute, create", "universe")
box.session.su("guest")

s:select{}
s:drop()
sq:set(100)
sq:drop()
c = require("net.box").connect(os.getenv("LISTEN"))
c:call("test_func")

box.session.su("admin")

box.schema.user.grant("guest","usage", "universe")

box.schema.func.drop("test_func")
s:drop()
sq:drop()

session = nil

-- an error when granting or revoking non-existent privilege
box.schema.user.grant("guest", "everything", "universe")
box.schema.user.revoke("guest", "everything", "universe")
-- an error when granting or revoking a privilege on a non-existent entity
box.schema.user.grant("guest", "read", "everywhere")
box.schema.user.revoke("guest", "read", "everywhere")
-- an error even when granting or revoking a non-existent privilege
-- on a non-existent entity
box.schema.user.grant("guest", "everything", "everywhere")
box.schema.user.revoke("guest", "everything", "everywhere")

--  produce an error if revoking a non-granted privilege
box.schema.user.create("tester")
box.schema.user.grant('tester', 'read', 'universe')
-- error: the privilege is not granted
box.schema.user.revoke('tester', 'create', 'universe')
-- no error: if_exists clause
box.schema.user.revoke('tester', 'create', 'universe', nil, { if_exists = true })
-- no error: the privilege is granted
box.schema.user.revoke('tester', 'read', 'universe')
box.schema.user.grant('tester', 'read', 'universe')
-- no error: some privileges are revoked
box.schema.user.revoke('tester', 'read,create', 'universe')
box.schema.user.drop('tester')
