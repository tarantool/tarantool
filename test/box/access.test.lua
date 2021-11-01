env = require('test_run')
test_run = env.new()
net = require('net.box')

session = box.session
-- user id for a Lua session is admin - 1
session.uid()
-- extra arguments are ignored
session.uid(nil)
-- admin
session.user()
session.effective_user()
-- extra argumentes are ignored
session.user(nil)
-- password() is a function which returns base64(sha1(sha1(password))
-- a string to store in _user table
box.schema.user.password('test')
box.schema.user.password('test1')
-- admin can create any user
box.schema.user.create('test', { password = 'test' })
-- su() let's you change the user of the session
-- the user will be unabe to change back unless he/she
-- is granted access to 'su'
session.su('test')
-- you can't create spaces unless you have a write access on
-- system space _space
-- in future we may  introduce a separate privilege
box.schema.space.create('test')
-- su() goes through because called from admin
-- console, and it has no access checks
-- for functions
session.su('admin')
box.schema.user.grant('test', 'write', 'space', '_space')

test_run:cmd("setopt delimiter ';'")
function usermax()
    local i = 1
    while true do
        box.schema.user.create('user'..i)
        i = i + 1
    end
end;
usermax();
function usermax()
    local i = 1
    while true do
        box.schema.user.drop('user'..i)
        i = i + 1
    end
end;
usermax();
test_run:cmd("setopt delimiter ''");
box.schema.user.create('rich')
box.schema.user.grant('rich', 'read,write', 'space', '_func')
box.schema.user.grant('rich', 'create', 'function')
session.su('rich')
uid = session.uid()
box.schema.func.create('dummy')
session.su('admin')
box.space['_user']:delete{uid}
box.schema.func.drop('dummy')
box.space['_user']:delete{uid}
box.schema.user.revoke('rich', 'public')
box.schema.user.revoke('rich', 'read,write', 'space', '_func')
box.schema.user.revoke('rich', 'alter', 'user', 'rich')
box.schema.user.revoke('rich', 'create', 'function')
box.schema.user.disable("rich")
-- test double disable is a no op
box.schema.user.disable("rich")
box.space['_user']:delete{uid}
box.schema.user.drop('test')

-- gh-944 name is too long
name = string.rep('a', box.schema.NAME_MAX - 1)
box.schema.user.create(name..'aa')

box.schema.user.create(name..'a')
box.schema.user.drop(name..'a')

box.schema.user.create(name)
box.schema.user.drop(name)

-- sudo
box.schema.user.create('tester')
-- admin -> user
session.user()
session.su('tester', function() return session.user(), session.effective_user() end)
session.user()

-- user -> admin
session.su('tester')
session.effective_user()
session.su('admin', function() return session.user(), session.effective_user() end)
session.user()
session.effective_user()

-- drop current user
session.su('admin', function() return box.schema.user.drop('tester') end)
session.user()
session.su('admin')
box.schema.user.drop('tester')
session.user()

-- Invalid user.
session.su('does not exist')
-- The point of this test is to try a name > max
-- allowed name.
session.su(string.rep('a', 66000))

--------------------------------------------------------------------------------
-- Check if identifiers obey the common constraints
--------------------------------------------------------------------------------
identifier = require("identifier")
test_run:cmd("setopt delimiter ';'")
identifier.run_test(
	function (identifier)
		box.schema.user.create(identifier)
		box.schema.user.grant(identifier, 'super')
		box.session.su(identifier)
		box.session.su("admin")
		box.schema.user.revoke(identifier, 'super')
	end,
	box.schema.user.drop
);
identifier.run_test(
	function (identifier)
		box.schema.role.create(identifier)
		box.schema.role.grant(identifier, 'execute,read,write',
			'universe', nil, {if_not_exists = false})
	end,
	box.schema.role.drop
);
test_run:cmd("setopt delimiter ''");

-- valid identifiers
box.schema.user.create('Петя_Иванов')
box.schema.user.drop('Петя_Иванов')

-- gh-300: misleading error message if a function does not exist
LISTEN = require('uri').parse(box.cfg.listen)
LISTEN ~= nil
c = net.connect(LISTEN.host, LISTEN.service)

c:call('nosuchfunction')
function nosuchfunction() end
c:call('nosuchfunction')
nosuchfunction = nil
c:call('nosuchfunction')
c:close()
-- Dropping a space recursively drops all grants - it's possible to
-- restore from a snapshot
box.schema.user.create('testus')
s = box.schema.space.create('admin_space')
index = s:create_index('primary', {type = 'hash', parts = {1, 'unsigned'}})
box.schema.user.grant('testus', 'write', 'space', 'admin_space')
s:drop()
box.snapshot()
test_run:cmd('restart server default')
net = require('net.box')
box.schema.user.drop('testus')
-- ------------------------------------------------------------
-- a test case for gh-289
-- box.schema.user.drop() with cascade doesn't work
-- ------------------------------------------------------------
session = box.session
box.schema.user.create('uniuser')
box.schema.user.grant('uniuser', 'create', 'space')
box.schema.user.grant('uniuser', 'write', 'space', '_schema')
box.schema.user.grant('uniuser', 'write', 'space', '_space')
session.su('uniuser')
us = box.schema.space.create('uniuser_space')
session.su('admin')
box.schema.user.drop('uniuser')
-- ------------------------------------------------------------
-- A test case for gh-253
-- A user with universal grant has no access to drop oneself
-- ------------------------------------------------------------
-- This behaviour is expected, since an object may be destroyed
-- only by its creator at the moment
-- ------------------------------------------------------------
box.schema.user.create('grantor')
box.schema.user.grant('grantor', 'read, write, execute, create, alter, drop', 'universe')
session.su('grantor')
box.schema.user.create('grantee')
box.schema.user.grant('grantee', 'read, write, execute, create, alter, drop', 'universe')
session.su('grantee')
-- fails - can't suicide - ask the creator to kill you
box.schema.user.drop('grantee')
session.su('grantor')
box.schema.user.drop('grantee')
-- fails, can't kill oneself
box.schema.user.drop('grantor')
session.su('admin')
box.schema.user.drop('grantor')
-- ----------------------------------------------------------
-- A test case for gh-299
-- It appears to be too easy to read all fields in _user
-- table
-- guest can't read _user table, add a test case
-- ----------------------------------------------------------
session.su('guest')
box.space._user:select{0}
box.space._user:select{1}
session.su('admin')
-- ----------------------------------------------------------
-- A test case for gh-358 Change user does not work from lua
-- Correct the update syntax in schema.lua
-- ----------------------------------------------------------
box.schema.user.create('user1')
box.space._user.index.name:select{'user1'}
session.su('user1')
box.schema.user.passwd('new_password')
session.su('admin')
box.space._user.index.name:select{'user1'}
box.schema.user.passwd('user1', 'extra_new_password')
box.space._user.index.name:select{'user1'}
box.schema.user.passwd('invalid_user', 'some_password')
box.schema.user.passwd()
session.su('user1')
-- permission denied
box.schema.user.passwd('admin', 'xxx')
session.su('admin')
box.schema.user.drop('user1')
box.space._user.index.name:select{'user1'}
-- ----------------------------------------------------------
-- A test case for gh-421 Granting a privilege revokes an
-- existing grant
-- ----------------------------------------------------------
box.schema.user.create('user')
id = box.space._user.index.name:get{'user'}[1]
box.schema.user.grant('user', 'read,write', 'universe')
box.space._priv:select{id}
box.schema.user.grant('user', 'read', 'universe')
box.space._priv:select{id}
box.schema.user.revoke('user', 'write', 'universe')
box.space._priv:select{id}
box.schema.user.revoke('user', 'read', 'universe')
box.space._priv:select{id}
box.schema.user.grant('user', 'write', 'universe')
box.space._priv:select{id}
box.schema.user.grant('user', 'read', 'universe')
box.space._priv:select{id}
box.schema.user.drop('user')
box.space._priv:select{id}
-- -----------------------------------------------------------
-- Be a bit more rigorous in what is accepted in space _user
-- -----------------------------------------------------------
utils = require('utils')
box.space._user:insert{10, 1, 'name', 'strange-object-type', utils.setmap({})}
box.space._user:insert{10, 1, 'name', 'role', utils.setmap{'password'}}
session = nil
-- -----------------------------------------------------------
-- admin can't manage grants on not owned objects
-- -----------------------------------------------------------
box.schema.user.create('twostep')
box.schema.user.grant('twostep', 'create', 'space')
box.schema.user.grant('twostep', 'create', 'function')
box.schema.user.grant('twostep', 'write', 'space', '_schema')
box.schema.user.grant('twostep', 'write', 'space', '_space')
box.schema.user.grant('twostep', 'write', 'space', '_index')
box.schema.user.grant('twostep', 'read', 'space', '_space_sequence')
box.schema.user.grant('twostep', 'read,write', 'space', '_func')
box.session.su('twostep')
twostep = box.schema.space.create('twostep')
index2 = twostep:create_index('primary')
box.schema.func.create('test')
box.session.su('admin')
box.schema.user.create('twostep_client')
box.schema.user.grant('twostep_client', 'execute', 'function', 'test')
box.schema.user.drop('twostep')
box.schema.user.drop('twostep_client')
-- the space is dropped when the user is dropped
--
-- box.schema.user.exists()
box.schema.user.exists('guest')
box.schema.user.exists(nil)
box.schema.user.exists(0)
box.schema.user.exists(1)
box.schema.user.exists(100500)
box.schema.user.exists('admin')
box.schema.user.exists('nosuchuser')
box.schema.user.exists{}
-- gh-671: box.schema.func.exists()
box.schema.func.exists('nosuchfunc')
box.schema.func.exists('guest')
box.schema.func.exists(1)
box.schema.func.exists(69)
box.schema.func.exists('box.schema.user.info')
box.schema.func.exists()
box.schema.func.exists(nil)
-- gh-665: user.exists() should nto be true for roles
box.schema.user.exists('public')
box.schema.role.exists('public')
box.schema.role.exists(nil)
-- test if_exists/if_not_exists in grant/revoke
box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'read,write,execute', 'universe', '', { if_not_exists = true })
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
box.schema.user.revoke('guest', 'usage,session', 'universe')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
box.schema.user.revoke('guest', 'read,write,execute', 'universe', '', { if_exists = true })
box.schema.user.grant('guest', 'usage,session', 'universe')
box.schema.func.create('dummy', { if_not_exists = true })
box.schema.func.create('dummy', { if_not_exists = true })
box.schema.func.drop('dummy')

-- gh-664 roles: accepting bad syntax for create
box.schema.user.create('user', 'blah')
box.schema.user.drop('user', 'blah')

-- gh-664 roles: accepting bad syntax for create
box.schema.func.create('func', 'blah')
box.schema.func.drop('blah', 'blah')
-- gh-758 attempt to set password for user guest
box.schema.user.passwd('guest', 'sesame')
-- gh-1205 box.schema.user.info fails
box.schema.user.drop('guest')
box.schema.role.drop('guest')
box.space._user.index.name:delete{'guest'}
box.space._user:delete{box.schema.GUEST_ID}
#box.schema.user.info('guest') > 0
box.schema.user.drop('admin')
box.schema.role.drop('admin')
box.space._user.index.name:delete{'admin'}
box.space._user:delete{box.schema.ADMIN_ID}
#box.schema.user.info('admin') > 0
box.schema.user.drop('public')
box.schema.role.drop('public')
box.space._user.index.name:delete{'public'}
box.space._user:delete{box.schema.PUBLIC_ROLE_ID}
#box.schema.role.info('public') > 0
box.schema.role.drop('super')
box.schema.user.drop('super')
box.space._user.index.name:delete{'super'}
box.space._user:delete{box.schema.SUPER_ROLE_ID}
#box.schema.role.info('super') > 0

-- gh-944 name is too long
name = string.rep('a', box.schema.NAME_MAX - 1)
box.schema.func.create(name..'aa')

box.schema.func.create(name..'a')
box.schema.func.drop(name..'a')

box.schema.func.create(name)
box.schema.func.drop(name)

-- A test case for: http://bugs.launchpad.net/bugs/712456
-- Verify that when trying to access a non-existing or
-- very large space id, no crash occurs.
LISTEN = require('uri').parse(box.cfg.listen)
c = net.connect(LISTEN.host, LISTEN.service)
c:_request(net._method.select, nil, nil, nil, 1, box.index.EQ, 0, 0, 0xFFFFFFFF, {})
c:_request(net._method.select, nil, nil, nil, 65537, box.index.EQ, 0, 0, 0xFFFFFFFF, {})
c:_request(net._method.select, nil, nil, nil, 4294967295, box.index.EQ, 0, 0, 0xFFFFFFFF, {})
c:close()

session = box.session
box.schema.user.create('test')
box.schema.user.grant('test', 'read', 'space', '_collation')
--box.schema.user.grant('test', 'write', 'space', '_collation')
-- FIXME: granting create on 'collation' only doesn't work
box.schema.user.grant('test', 'create', 'universe')
session.su('test')
box.internal.collation.create('test', 'ICU', 'ru_RU')
session.su('admin')
box.internal.collation.drop('test') -- success
box.internal.collation.create('test', 'ICU', 'ru_RU')
session.su('test')
box.internal.collation.drop('test') -- fail
session.su('admin')
box.internal.collation.drop('test') -- success
box.schema.user.drop('test')

--
-- gh-2710 object drop revokes all associated privileges
--
_ = box.schema.space.create('test_space')
_ = box.schema.sequence.create('test_sequence')
box.schema.func.create('test_function')

box.schema.user.create('test_user')
box.schema.user.grant('test_user', 'read', 'space', 'test_space')
box.schema.user.grant('test_user', 'write', 'sequence', 'test_sequence')
box.schema.user.grant('test_user', 'execute', 'function', 'test_function')

box.schema.role.create('test_role')
box.schema.role.grant('test_role', 'read', 'space', 'test_space')
box.schema.role.grant('test_role', 'write', 'sequence', 'test_sequence')
box.schema.role.grant('test_role', 'execute', 'function', 'test_function')

box.schema.user.info('test_user')
box.schema.role.info('test_role')

box.space.test_space:drop()
box.sequence.test_sequence:drop()
box.schema.func.drop('test_function')

box.schema.user.info('test_user')
box.schema.role.info('test_role')

box.schema.user.drop('test_user')
box.schema.role.drop('test_role')

-- gh-3023: box.session.su() changes both authenticated and effective
-- user, while should only change the effective user
--
function uids() return { uid = box.session.uid(), euid = box.session.euid() } end
box.session.su('guest')
uids()
box.session.su('admin')
box.session.su('guest', uids)

--
-- gh-2898 System privileges
--
s = box.schema.create_space("tweed")
_ = s:create_index('primary', {type = 'hash', parts = {1, 'unsigned'}})

box.schema.user.create('test', {password="pass"})
box.schema.user.grant('test', 'read,write', 'universe')

-- other users can't disable
box.schema.user.create('test1')
session.su("test1")
box.schema.user.disable("test")

session.su("admin")
box.schema.user.disable("test")
-- test double disable is a no op
box.schema.user.disable("test")
session.su("test")
c = net.connect(LISTEN.host, LISTEN.service, {user="test", password="pass"})
c.state
c.error

session.su("test1")
box.schema.user.grant("test", "usage", "universe")
session.su('admin')
box.schema.user.grant("test", "session", "universe")
session.su("test")
s:select{}

session.su('admin')
box.schema.user.enable("test")
-- check enable not fails on double enabling
box.schema.user.enable("test")
session.su("test")
s:select{}

session.su("admin")
box.schema.user.drop('test')
box.schema.user.drop('test1')
s:drop()

--
-- gh-3022 role 'super'
--
s = box.schema.space.create("admin_space")
box.schema.user.grant('guest', 'super')
box.session.su('guest')
_ = box.schema.space.create('test')
box.space.test:drop()
_ = box.schema.user.create('test')
box.schema.user.drop('test')
_ = box.schema.func.create('test')
box.schema.func.drop('test')
-- gh-3088 bug: super role lacks drop privileges on other users' spaces
s:drop()

box.session.su('admin')
box.schema.user.revoke('guest', 'super')
box.session.su('guest')
box.schema.space.create('test')
box.schema.user.create('test')
box.schema.func.create('test')
box.session.su('admin')

--
-- gh-2911 on_access_denied trigger
--
obj_type = nil
obj_name = nil
op_type = nil
euid = nil
auid = nil
function access_denied_trigger(op, type, name) obj_type = type; obj_name = name; op_type = op end
function uid() euid = box.session.euid(); auid = box.session.uid() end
_ = box.session.on_access_denied(access_denied_trigger)
_ = box.session.on_access_denied(uid)
s = box.schema.space.create('admin_space', {engine="vinyl"})
seq = box.schema.sequence.create('test_sequence')
index = s:create_index('primary', {type = 'tree', parts = {1, 'unsigned'}})
box.schema.user.create('test_user', {password="pass"})
box.session.su("test_user")
s:select{}
obj_type, obj_name, op_type
euid, auid
seq:set(1)
obj_type, obj_name, op_type
euid, auid
box.session.su("admin")
c = net.connect(LISTEN.host, LISTEN.service, {user="test_user", password="pass"})
function func() end
st, e = pcall(c.call, c, func)
obj_type, op_type
euid, auid
obj_name:match("function")
box.schema.user.revoke("test_user", "usage", "universe")
box.session.su("test_user")
st, e = pcall(s.select, s, {})
e = e:unpack()
e.type, e.access_type, e.object_type, e.message
obj_type, obj_name, op_type
euid, auid
box.session.su("admin")
box.schema.user.revoke("test_user", "session", "universe")
c = net.connect(LISTEN.host, LISTEN.service, {user="test_user", password="pass"})
obj_type, obj_name, op_type
euid, auid
box.session.on_access_denied(nil, access_denied_trigger)
box.session.on_access_denied(nil, uid)
box.schema.user.drop("test_user")
seq:drop()
s:drop()

--
-- gh-945 create, drop, alter privileges
--
box.schema.user.create("tester")
s = box.schema.space.create("test")
_ = s:create_index("primary")
seq = box.schema.sequence.create("test")
u = box.schema.user.create("test")
f = box.schema.func.create("test")

-- failed create, auto_increment requires read.
box.session.su("tester")
box.schema.space.create("test_space")
box.schema.user.create('test_user')
box.schema.func.create('test_func')
box.session.su("admin")
box.schema.user.grant("tester", "read", "space", "_user")
box.schema.user.grant("tester", "read", "space", "_func")
-- failed create
box.session.su("tester")
box.schema.space.create("test_space")
box.schema.user.create('test_user')
box.schema.func.create('test_func')
box.session.su("admin")

--
-- FIXME 2.0: we still need to grant 'write' on universe
-- explicitly since we still use process_rw to write to system
-- tables from ddl
--
box.schema.user.grant('tester', 'write', 'universe')
box.schema.user.grant('tester', 'create', 'user')
box.schema.user.grant('tester', 'create', 'space')
box.schema.user.grant('tester', 'create', 'function')
box.schema.user.grant('tester', 'create' , 'sequence')
box.schema.user.grant('tester', 'read', 'space', '_sequence')
box.schema.user.grant('tester', 'read', 'space', '_space_sequence')
box.schema.user.grant('tester', 'read', 'space', '_trigger')
box.schema.user.grant('tester', 'read', 'space', '_fk_constraint')
box.schema.user.grant('tester', 'read', 'space', '_ck_constraint')
box.schema.user.grant('tester', 'read', 'space', '_func_index')
box.session.su("tester")
-- successful create
s1 = box.schema.space.create("test_space")
_ = s1:create_index("primary")
_ = box.schema.user.create('test_user')
_ = box.schema.func.create('test_func')
seq1 = box.schema.sequence.create('test_seq')

-- successful drop of owned objects
s1:drop()
seq1:drop()
box.schema.user.drop('test_user')
box.schema.func.drop('test_func')

-- failed alter
-- box.session.su("tester", s.format, s, {name="id", type="unsigned"})

-- box.schema.user.grant("tester", "alter", "universe")
-- successful alter
-- box.session.su("tester", s.format, s, {name="id", type="unsigned"})

-- failed drop
-- box.session.su("tester", s.drop, s)

-- gh-3090: nested su unexpected behaviour.
-- gh-3492: su doesn't grant effective privileges

-- should print guest
test_run:cmd("setopt delimiter ';'")
function f()
    box.session.su("admin", function() end)
    return box.session.effective_user()
end;
box.session.su("guest", f);

-- the call of test_admin below shouldn't fail
function test_admin()
    box.schema.user.create('storage', {password = 'storage', if_not_exists=true})

    box.schema.user.grant('storage', 'replication', nil, nil, {if_not_exists=true})
end;
test_run:cmd("setopt delimiter ''");

box.session.su("guest")
box.session.user()
box.session.effective_user()
box.session.su("admin", test_admin)
box.session.user()
box.session.effective_user()
box.session.su("admin")
box.session.user()
box.session.effective_user()

box.schema.user.drop("storage")
test_admin = nil
f = nil

-- now sudo can be used here since nested sudo is fixed
box.session.su("tester", box.schema.user.drop, "test")
box.session.su("tester", box.schema.func.drop, "test")

box.session.su("admin")
box.schema.user.grant("tester", "drop", "universe")
-- successful drop
box.session.su("tester", s.drop, s)
box.session.su("tester", seq.drop, seq)
box.session.su("tester", box.schema.user.drop, "test")
box.session.su("tester", box.schema.func.drop, "test")

box.session.su("admin")
box.schema.user.drop("tester")

-- gh-3146 gotcha for granting universe with options
box.schema.user.grant("guest", "read", "universe", {if_not_exists = true})
box.schema.user.grant("guest", "read", "universe", "useless name")
box.schema.user.grant("guest", "read", "universe", "useless name", {if_not_exists = true})
box.schema.user.grant("guest", "read", "universe", 0, {if_not_exists = true})
box.schema.user.grant("guest", "read", "universe", nil, {if_not_exists = true})
box.schema.user.grant("guest", "read", "universe", {}, {if_not_exists = true})
box.schema.user.revoke("guest", "read", "universe", {if_exists = true})
box.schema.user.revoke("guest", "read", "universe", "useless name")
box.schema.user.revoke("guest", "read", "universe", "useless name", {if_exists = true})
box.schema.user.revoke("guest", "read", "universe", 0, {if_exists = true})
box.schema.user.revoke("guest", "read", "universe", nil, {if_exists = true})
box.schema.user.revoke("guest", "read", "universe", {}, {if_exists = true})

--
-- Check that box.schema.* api is available to non-super user
-- In scope of gh-3250 "make sure grant/revoke does not require
-- read access to universe"
--
box.session.su('guest')
--
--
-- 1a. func.exists() works
--
-- No function: function not found but returns nothing if the user
-- has no access to the function
--
box.schema.func.exists('test')
box.space._vspace.index.name:get{"test"} ~= nil
box.space._vsequence.index.name:get{"test"} ~= nil
--
-- create an objects, but 'guest' still has no access to them
--
box.session.su('admin', box.schema.func.create, 'test')
s = box.session.su('admin', box.schema.space.create, 'test')
_ = box.session.su('admin', box.schema.sequence.create, 'test')
box.schema.func.exists('test')
box.space._vspace.index.name:get{"test"} ~= nil
box.space._vsequence.index.name:get{"test"} ~= nil
--
-- grant access, the object should become visible to guest
--
box.session.su('admin', box.schema.user.grant, 'guest', 'execute', 'function', 'test')
box.session.su('admin', box.schema.user.grant, 'guest', 'read', 'space', 'test')
box.session.su('admin', box.schema.user.grant, 'guest', 'read', 'sequence', 'test')
box.schema.func.exists('test')
box.space._vspace.index.name:get{"test"} ~= nil
box.space._vsequence.index.name:get{"test"} ~= nil
--
-- drop objects
--
box.session.su('admin', box.schema.func.drop, 'test')
box.session.su('admin', s.drop, s)
box.session.su('admin', box.schema.sequence.drop, 'test')
box.schema.func.exists('test')
box.space._vspace.index.name:get{"test"} ~= nil
box.space._vsequence.index.name:get{"test"} ~= nil
--
-- restore
--
box.session.su('admin')

-- prerequisite gh-945
box.schema.user.grant("guest", "alter", "function")
box.schema.user.grant("guest", "execute", "sequence")
box.schema.user.grant("guest", "read,execute", "sequence")
box.schema.user.grant("guest", "read,write,execute", "role")

-- Check entities DML
box.schema.user.create("tester", { password  = '123' })
s = box.schema.space.create("test")
_ = s:create_index("primary", {parts={1, "unsigned"}})
seq = box.schema.sequence.create("test")
box.schema.func.create("func")
c = net.connect(LISTEN.host, LISTEN.service, {user='tester', password = '123'})

box.session.su("tester", s.select, s)
box.session.su("tester", seq.set, seq, 1)
c:call("func")
box.schema.user.grant("tester", "read", "space")
box.schema.user.grant("tester", "write", "sequence")
box.schema.user.grant("tester", "execute", "function")
box.session.su("tester", s.select, s)
box.session.su("tester", seq.next, seq)
c:call("func")

box.session.su("tester", s.insert, s, {1})
box.schema.user.grant("tester", "write", "space")
box.session.su("tester", s.insert, s, {1})

box.schema.user.drop("tester")
s:drop()
seq:drop()
box.schema.func.drop("func")
c:close()

--
-- A user with read/write access to sequence shouldn't
-- be able to create a sequence. It also needs a create privilege
-- on universe.
--
box.schema.user.create('tester')
box.schema.user.grant('tester', 'read,write', 'space', '_sequence')
box.session.su('tester')
_  = box.schema.sequence.create('test_sequence')
box.session.su('admin')
box.schema.user.grant('tester', 'create', 'sequence')
box.session.su('tester')
_ = box.schema.sequence.create('test_sequence')
box.session.su('admin')
box.schema.user.drop('tester')


--
-- test case for 3530: do not ignore single object privileges
--
box.schema.user.create("test")
_ = box.schema.space.create("space1")
box.schema.user.grant("test", "read", "space", "space1")
box.schema.user.grant("test", "write", "space", "_index")
box.schema.user.grant("test", "read", "space", "_space_sequence")
box.session.su("test")
box.space.space1:create_index("pk")
box.session.su("admin")
box.space.space1.index[0] == nil
box.schema.user.grant("test", "create", "space", "space1")
box.session.su("test")
_ = box.space.space1:create_index("pk")
box.space.space1:insert{5}
box.session.su("admin")
box.space.space1.index[0] ~= nil
box.space.space1:select{}
box.schema.user.grant("test", "write", "space", "space1")
box.session.su("test")
box.space.space1:insert{5}
box.session.su("admin")
box.space.space1:select{}
box.schema.user.drop("test")
box.space.space1:drop()

--
-- test that it is possible to grant privileges on a single user.
box.schema.user.create("user1")
box.schema.user.create("user2")
box.schema.user.create("user3")
box.schema.user.grant("user1", "write", "space", "_user")
box.schema.user.grant("user1", "read", "space", "_user")
box.space._user:select{}
box.session.su("user1")
-- can alter itself, but can't alter others without privileges.
box.schema.user.passwd("user1", "abcd")
box.schema.user.passwd("user2", "abcd")
box.session.su("admin")
box.space._user:select{}
box.schema.user.grant("user1", "alter", "user", "user2")
box.session.su("user1")
box.schema.user.passwd("user2", "abcd")
-- still fails
box.schema.user.passwd("user3", "qewr")
box.session.su("admin")
box.space._user:select{}
box.schema.user.drop("user1")
box.schema.user.drop("user2")
box.schema.user.drop("user3")

--
-- Check that admin user privileges can't be removed. Otherwise an
-- instance could not bootstrap nor recovery.
--
box.space._priv:delete{1, 'universe', 0}

--
-- gh-714: box.schema.user.grant error should be versatile,
-- i.e. error on universally granted privileges shouldn't
-- include any redundant details and/or symbols.
--
box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'read,write,execute', 'universe')

-- Expected behavior of grant() error shouldn't change otherwise.
sp = box.schema.create_space('not_universe')
box.schema.user.grant('guest', 'read,write,execute', 'space', 'not_universe')
box.schema.user.grant('guest', 'read,write,execute', 'space', 'not_universe')

-- Clean up.
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
box.schema.user.revoke('guest', 'read,write,execute', 'space', 'not_universe')
sp:drop()

--
-- Make sure that the functions "LUA" and "box.schema.user.info" do not have
-- excess rights.
--
_ = box.schema.func.call("LUA", "return 1")
_ = box.schema.func.call("LUA", "return box.space._space:count()")
_ = box.schema.func.call("box.schema.user.info", 0)
_ = box.schema.func.call("box.schema.user.info", 1)
session.su('guest')
_ = box.schema.func.call("LUA", "return 1")
_ = box.schema.func.call("LUA", "return box.space._space:count()")
_ = box.schema.func.call("box.schema.user.info", 0)
_ = box.schema.func.call("box.schema.user.info", 1)
session.su('admin')

-- gh-5389: make sure that granting privileges from guest does not lead
-- to crash or assertion fault.
--
box.schema.user.grant('guest','read,write,create','universe')
box.session.su('guest')
box.schema.user.create("optimizer")
box.schema.user.grant("optimizer", "alter", "space")
box.schema.user.grant("optimizer", "alter", "sequence")
box.schema.user.grant("optimizer", "create", "function")
box.schema.user.grant("optimizer", "drop", "user")
box.schema.user.grant("optimizer", "create", "role")
box.schema.user.grant("optimizer", "create", "function")
session.su('admin')
box.schema.user.revoke('guest', 'read,write,create', 'universe')
box.schema.user.drop("optimizer")
