session = box.session

--
-- Basic tests
--

#box.space._vspace:select{} == #box.space._space:select{}
#box.space._vindex:select{} == #box.space._index:select{}
#box.space._vuser:select{} == #box.space._user:select{}
#box.space._vpriv:select{} == #box.space._priv:select{}
#box.space._vfunc:select{} == #box.space._func:select{}

-- gh-1042: bad error message for _vspace, _vuser, _vindex, etc.
-- Space '_vspace' (sysview) does not support replace
box.space._vspace:replace({1, 1, 'test'})
box.space._vspace:delete(1)
box.space._vspace:update(1, {{'=', 2, 48}})

-- error: Index 'primary' of space '_vspace' (sysview) does not support xxx()
box.space._vspace.index.primary:len()
box.space._vspace.index.primary:random(48)

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
box.schema.user.revoke('guest', 'public')
box.session.su('guest')

#box.space._vspace:select{}
#box.space._vindex:select{}
#box.space._vuser:select{}
#box.space._vpriv:select{}
#box.space._vfunc:select{}
#box.space._vsequence:select{}

box.session.su('admin')
box.schema.user.grant('guest', 'public')
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

#box.space._vindex:select{}
#box.space._vuser:select{}
#box.space._vpriv:select{}
#box.space._vfunc:select{}
#box.space._vsequence:select{}

box.session.su('admin')
box.schema.user.revoke('guest', 'write', 'universe')
box.space.test:drop()
box.session.su('guest')

-- read access to original space also allow to read a view
box.session.su('admin')
space_cnt = #box.space._space:select{}
index_cnt = #box.space._index:select{}
box.schema.user.grant('guest', 'read', 'space', '_space')
box.schema.user.grant('guest', 'read', 'space', '_index')
box.session.su('guest')
#box.space._vspace:select{} == space_cnt
#box.space._vindex:select{} == index_cnt
box.session.su('admin')
box.schema.user.revoke('guest', 'read', 'space', '_space')
box.schema.user.revoke('guest', 'read', 'space', '_index')
box.session.su('guest')
#box.space._vspace:select{} < space_cnt
#box.space._vindex:select{} < index_cnt

--
-- _vuser
--

-- a guest user can read information about itself
t = box.space._vuser:select(); for i = 1, #t do if t[i][3] == 'guest' then return true end end return false

-- read access to original space also allow to read a view
box.session.su('admin')
user_cnt = #box.space._user:select{}
box.schema.user.grant('guest', 'read', 'space', '_user')
box.session.su('guest')
#box.space._vuser:select{} == user_cnt
box.session.su('admin')
box.schema.user.revoke('guest', 'read', 'space', '_user')
box.session.su('guest')
#box.space._vuser:select{} < user_cnt

box.session.su('admin')
box.schema.user.grant('guest', 'read,write,create', 'universe')
box.session.su('guest')

box.schema.user.create('tester')

box.session.su('admin')
box.schema.user.revoke('guest', 'read,write,create', 'universe')
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

-- read access to original space also allow to read a view
box.session.su('admin')
box.schema.user.grant('guest', 'read', 'space', '_priv')
priv_cnt = #box.space._priv:select{}
box.session.su('guest')
#box.space._vpriv:select{} == priv_cnt
box.session.su('admin')
box.schema.user.revoke('guest', 'read', 'space', '_priv')
box.session.su('guest')
cnt = #box.space._vpriv:select{}

cnt < priv_cnt

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

box.session.su('admin')
box.schema.func.create('test')

-- read access to original space also allow to read a view
func_cnt = #box.space._func:select{}
box.schema.user.grant('guest', 'read', 'space', '_func')
box.session.su('guest')
#box.space._vfunc:select{} == func_cnt
box.session.su('admin')
box.schema.user.revoke('guest', 'read', 'space', '_func')
box.session.su('guest')
cnt = #box.space._vfunc:select{}

cnt < func_cnt

box.session.su('admin')
box.schema.user.grant('guest', 'execute', 'function', 'test')
box.session.su('guest')

#box.space._vfunc:select{} == func_cnt

box.session.su('admin')
box.schema.user.revoke('guest', 'execute', 'function', 'test')
box.session.su('guest')

#box.space._vfunc:select{} == cnt

box.session.su('admin')
box.schema.user.grant('guest', 'execute', 'universe')
box.session.su('guest')

#box.space._vfunc:select{} == func_cnt

box.session.su('admin')
box.schema.user.revoke('guest', 'execute', 'universe')
box.schema.func.drop('test')
box.session.su('guest')

#box.space._vfunc:select{} == cnt

--
-- _vsequence
--

box.session.su('admin')
seq = box.schema.sequence.create('test')

-- read access to original sequence also allow to read a view
seq_cnt = #box.space._sequence:select{}
box.schema.user.grant("guest", "read", "sequence", "test")
box.session.su("guest")
#box.space._vsequence:select{} == seq_cnt
box.session.su('admin')

box.schema.user.revoke("guest", "read", "sequence", "test")
box.session.su("guest")
cnt = #box.space._vsequence:select{}
cnt < seq_cnt
session.su('admin')
box.schema.user.grant("guest", "write", "sequence", "test")
box.session.su("guest")
#box.space._vsequence:select{} == cnt + 1
session.su('admin')
seq:drop()

--
-- view:alter() tests
--

box.space._vspace.index[1]:alter({parts = { 2, 'string' }})
box.space._vspace.index[1]:select('xxx')
box.space._vspace.index[1]:select(1)
box.space._vspace.index[1]:alter({parts = { 2, 'unsigned' }})
box.space._space.index[1]:drop()
box.space._vspace.index[1]:select(1)
s = box.space._space:create_index('owner', {parts = { 2, 'unsigned' }, id = 1, unique = false})
#box.space._vspace.index[1]:select(1) > 0

session = nil
