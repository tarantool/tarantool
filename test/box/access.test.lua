
session = require('session')
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
box.schema.create_space('test')
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
box.space['_user']:delete{uid}
box.schema.user.drop('test')

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
c = box.net.box.new("localhost", box.cfg.primary_port)
c:call('nosuchfunction')
function nosuchfunction() end
c:call('nosuchfunction')
nosuchfunction = nil
c:call('nosuchfunction')
c:close()
-- Dropping a space recursively drops all grants - it's possible to 
-- restore from a snapshot
box.schema.user.create('testus')
s = box.schema.create_space('admin_space')
s:create_index('primary', {type = 'hash', parts = {1, 'NUM'}})
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
session = require('session')
box.schema.user.create('uniuser')
box.schema.user.grant('uniuser', 'read, write, execute', 'universe')
session.su('uniuser')
us = box.schema.create_space('uniuser_space')
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
box.schema.user.drop('user1')
box.space._user.index.name:select{'user1'}
session = nil
