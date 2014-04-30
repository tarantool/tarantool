--
-- Check a double create space
--
s = box.schema.create_space('test')
s = box.schema.create_space('test')
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

s = box.schema.create_space('admin_space')
s:create_index('primary', {type = 'hash', parts = {0, 'NUM'}})
s:insert({1})
--
-- Check double grant
--
box.schema.user.grant('testus', 'read', 'space', 'admin_space')
box.schema.user.grant('testus', 'read', 'space', 'admin_space')

box.session.su('testus')
s:select()
--
-- Check double revoke
--
box.session.su('admin')
box.schema.user.revoke('testus', 'read', 'space', 'admin_space')
box.schema.user.revoke('testus', 'read', 'space', 'admin_space')

box.session.su('testus')
s:select()
box.session.su('admin')
--
-- Check double drop user
--
box.schema.user.drop('testus')
box.schema.user.drop('testus')
--
-- Check 'guest' user
--
box.session.su('guest')
box.session.uid()
box.space._user:select()
s:select()

box.session.su('admin')
-- Create user with universe read&write grants
-- and create this user session
--
box.schema.user.create('uniuser')
box.schema.user.grant('uniuser', 'read, write, execute', 'universe')
box.session.su('uniuser')
box.session.uid()
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
us = box.schema.create_space('uniuser_space')
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
box.space._user:select()
box.space._space:select()

us = box.schema.create_space('uniuser_space')
box.schema.func.create('uniuser_func')
box.schema.user.create('uniuser_testus')

box.session.su('admin')
box.schema.user.create('someuser')
box.schema.user.grant('someuser', 'read, write, execute', 'universe')
box.session.su('someuser')
--
-- Check drop objects of another user
--
s:drop()
us:drop()
box.schema.func.drop('uniuser_func')
box.schema.user.drop('uniuser_testus')

box.session.su('admin')
box.schema.user.drop('someuser')
box.schema.user.drop('uniuser_testus')
box.schema.user.drop('uniuser')
--
-- Check write grant on _user
--
box.schema.user.create('testuser')

box.schema.user.grant('testuser', 'write', 'space', '_user')
box.session.su('testuser')
box.space._user:delete(2)
box.space._user:select()
box.space._user:insert{3,'','testus'i}
box.space._user:delete(3)

box.session.su('admin')
box.space._user:select()

box.schema.user.revoke('testuser', 'write', 'space', '_user')
--
-- Check read grant on _user
--
box.schema.user.grant('testuser', 'read', 'space', '_user')
box.session.su('testuser')
box.space._user:delete(2)
box.space._user:select()
box.space._user:insert{3,'','testus'}

box.session.su('admin')
s:drop()

