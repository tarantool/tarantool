session = box.session

--
-- Basic tests
--

#box.space._vspace:select{} == #box.space._space:select{}
#box.space._vindex:select{} == #box.space._index:select{}
#box.space._vuser:select{} == #box.space._user:select{}
#box.space._vpriv:select{} == #box.space._priv:select{}
#box.space._vfunc:select{} == #box.space._func:select{}

-- error: sysview does not support replace()
box.space._vspace:replace({1, 1, 'test'})

session.su('guest')

--
-- _vspace + _vindex
--
-- _vXXXX views are visible for 'public' role
#box.space._vspace.index[2]:select('_vspace') ~= 0
#box.space._vspace.index[2]:select('_vindex') ~= 0
#box.space._vspace.index[2]:select('_vuser') ~= 0
#box.space._vspace.index[2]:select('_vfunc') ~= 0
#box.space._vspace.index[2]:select('_vpriv') ~= 0

#box.space._vindex:select(box.space._vspace.id) > 0
#box.space._vindex:select(box.space._vindex.id) > 0
#box.space._vindex:select(box.space._vuser.id) > 0
#box.space._vindex:select(box.space._vfunc.id) > 0
#box.space._vindex:select(box.space._vpriv.id) > 0

box.session.su('admin')
box.schema.role.revoke('guest', 'public')
box.session.su('guest')

#box.space._vspace:select{}
#box.space._vindex:select{}
#box.space._vuser:select{}
#box.space._vpriv:select{}
#box.space._vfunc:select{}

box.session.su('admin')
box.schema.role.grant('guest', 'public')
box.session.su('guest')

#box.space._vspace:select{}
#box.space._vindex:select{}

box.session.su('admin')
s = box.schema.space.create('test')
s = box.space.test:create_index('primary')
box.schema.role.grant('public', 'read', 'space', 'test')
box.session.su('guest')

box.space._vspace.index[2]:get('test') ~= nil
#box.space._vindex:select(box.space.test.id) == 1

box.session.su('admin')
box.schema.role.revoke('public', 'read', 'space', 'test')
box.session.su('guest')

box.space._vspace.index[2]:get('test') == nil
#box.space._vindex:select(box.space.test.id) == 0

box.session.su('admin')
box.schema.user.grant('guest', 'read', 'space', 'test')
box.session.su('guest')

box.space._vspace.index[2]:get('test') ~= nil
#box.space._vindex:select(box.space.test.id) == 1

box.session.su('admin')
box.schema.user.revoke('guest', 'read', 'space', 'test')
box.session.su('guest')

box.space._vspace.index[2]:get('test') == nil
#box.space._vindex:select(box.space.test.id) == 0

-- check universe permissions
box.session.su('admin')
box.schema.user.grant('guest', 'read', 'universe')
box.session.su('guest')

#box.space._vspace:select{}
#box.space._vindex:select{}
#box.space._vuser:select{}
#box.space._vpriv:select{}
#box.space._vfunc:select{}

box.session.su('admin')
box.schema.user.revoke('guest', 'read', 'universe')
box.schema.user.grant('guest', 'write', 'universe')
box.session.su('guest')

#box.space._vspace:select{}
#box.space._vindex:select{}
#box.space._vuser:select{}
#box.space._vpriv:select{}
#box.space._vfunc:select{}

box.session.su('admin')
box.schema.user.revoke('guest', 'write', 'universe')
box.space.test:drop()
box.session.su('guest')

--
-- _vuser
--

-- a guest user can read information about itself
t = box.space._vuser:select(); return #t == 1 and t[1][3] == 'guest'

#box.space._vuser:select{}

box.session.su('admin')
box.schema.user.grant('guest', 'read,write', 'universe')
box.session.su('guest')

box.schema.user.create('tester')

box.session.su('admin')
box.schema.user.revoke('guest', 'read,write', 'universe')
box.session.su('guest')

#box.space._vuser.index[2]:select('tester') > 0

box.session.su('admin')
box.schema.user.drop('tester')
box.session.su('guest')

--
-- _vpriv
--

-- a guest user can see granted 'public' role
box.space._vpriv.index[2]:select('role')[1][2] == session.uid()

cnt = #box.space._vpriv:select{}

box.session.su('admin')
box.schema.user.grant('guest', 'read,write', 'space', '_schema')
box.session.su('guest')

#box.space._vpriv:select{} == cnt + 1

box.session.su('admin')
box.schema.user.revoke('guest', 'read,write', 'space', '_schema')
box.session.su('guest')

#box.space._vpriv:select{} == cnt

--
-- _vfunc
--

cnt = #box.space._vfunc:select{}

box.session.su('admin')
box.schema.func.create('test')
box.schema.user.grant('guest', 'execute', 'function', 'test')
box.session.su('guest')

#box.space._vfunc:select{} = cnt + 1

box.session.su('admin')
box.schema.user.revoke('guest', 'execute', 'function', 'test')
box.session.su('guest')

#box.space._vfunc:select{} == cnt

box.session.su('admin')
box.schema.user.grant('guest', 'execute', 'universe')
box.session.su('guest')

#box.space._vfunc:select{} == cnt + 1

box.session.su('admin')
box.schema.user.revoke('guest', 'execute', 'universe')
box.schema.func.drop('test')
box.session.su('guest')

#box.space._vfunc:select{} == cnt

--
-- view:alter() tests
--

session.su('admin')
box.space._vspace.index[1]:alter({parts = { 2, 'str' }})
box.space._vspace.index[1]:select('xxx')
box.space._vspace.index[1]:select(1)
box.space._vspace.index[1]:alter({parts = { 2, 'num' }})
box.space._space.index[1]:drop()
box.space._vspace.index[1]:select(1)
s = box.space._space:create_index('owner', {parts = { 2, 'num' }, id = 1, unique = false})
#box.space._vspace.index[1]:select(1) > 0

session = nil
