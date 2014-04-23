--
-- Check a double create space
--
s = box.schema.create_space('test')
s = box.schema.create_space('test')
--
-- Check double create user
--
box.schema.user.create('testus')
box.schema.user.create('testus')
--
-- Check double grant
--
box.schema.user.grant('testus', 'read', 'space', 'test')
box.schema.user.grant('testus', 'read', 'space', 'test')

--
-- Check double drop user
--
box.schema.user.drop('testus')
box.schema.user.drop('testus')
--
-- Check double drop space
--
s:drop()
s:drop()
--
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
--Check create and drop function
--
box.schema.func.create('bar')
box.schema.func.drop('bar')
--
-- Check create and drop space
--
s = box.schema.create_space('test')
s:drop()
--
-- Check create and drop user
--
box.schema.user.create('testus')
box.schema.user.drop('testus')
