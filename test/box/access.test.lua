
session = box.session
-- user id for a Lua session is admin - 1
session.uid()
-- extra arguments are ignored
session.uid(nil)
-- admin
session.user()
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

--# setopt delimiter ';'
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
--# setopt delimiter ''
box.schema.user.create('rich')
box.schema.user.grant('rich', 'read,write', 'universe')
session.su('rich')
uid = session.uid()
box.schema.func.create('dummy')
session.su('admin')
box.space['_user']:delete{uid}
box.schema.func.drop('dummy')
box.space['_user']:delete{uid}
box.schema.user.revoke('rich', 'read,write', 'universe')
box.schema.user.revoke('rich', 'public')
box.space['_user']:delete{uid}
box.schema.user.drop('test')

-- sudo
box.schema.user.create('tester')
-- admin -> user
session.user()
session.su('tester', function() return session.user() end)
session.user()

-- user -> admin
session.su('tester')
session.user()
session.su('admin', function() return session.user() end)
session.user()

-- drop current user
session.su('admin', function() return box.schema.user.drop('tester') end)
session.user()
session.su('admin')
session.user()

--------------------------------------------------------------------------------
-- #198: names like '' and 'x.y' and 5 and 'primary ' are legal
--------------------------------------------------------------------------------
-- invalid identifiers
box.schema.user.create('invalid.identifier')
box.schema.user.create('invalid identifier')
box.schema.user.create('user ')
box.schema.user.create('5')
box.schema.user.create(' ')

-- valid identifiers
box.schema.user.create('Петя_Иванов')
box.schema.user.drop('Петя_Иванов')

-- gh-300: misleading error message if a function does not exist
LISTEN = require('uri').parse(box.cfg.listen)
LISTEN ~= nil
c = (require 'net.box'):new(LISTEN.host, LISTEN.service)

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
index = s:create_index('primary', {type = 'hash', parts = {1, 'NUM'}})
box.schema.user.grant('testus', 'write', 'space', 'admin_space')
s:drop()
box.snapshot()
--# stop server default
--# start server default
box.schema.user.drop('testus')
-- ------------------------------------------------------------
-- a test case for gh-289
-- box.schema.user.drop() with cascade doesn't work
-- ------------------------------------------------------------
session = box.session
box.schema.user.create('uniuser')
box.schema.user.grant('uniuser', 'read, write, execute', 'universe')
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
box.schema.user.grant('grantor', 'read, write, execute', 'universe')  
session.su('grantor')
box.schema.user.create('grantee')
box.schema.user.grant('grantee', 'read, write, execute', 'universe')  
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
box.space._user:insert{10, 1, 'name'}
box.space._user:insert{10, 1, 'name', 'strange-object-type'}
box.space._user:insert{10, 1, 'name', 'user', 'password'}
box.space._user:insert{10, 1, 'name', 'role', 'password'}
session = nil
-- -----------------------------------------------------------
-- admin can't manage grants on not owned objects
-- -----------------------------------------------------------
box.schema.user.create('twostep')
box.schema.user.grant('twostep', 'read,write,execute', 'universe')
box.session.su('twostep')
twostep = box.schema.space.create('twostep')
index2 = twostep:create_index('primary')
box.schema.func.create('test')
box.session.su('admin')
box.schema.user.revoke('twostep', 'execute,read,write', 'universe')
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
box.schema.func.exists(2)
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
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
box.schema.user.revoke('guest', 'read,write,execute', 'universe', '', { if_exists = true })
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
