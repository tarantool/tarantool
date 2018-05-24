test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

fiber =  require('fiber')

--
-- Check that space truncation is forbidden in a transaction.
--
s = box.schema.create_space('test', {engine = engine})
_ = s:create_index('pk')
_ = s:insert{123}
box.begin()
s:truncate()
box.commit()
s:select()
s:drop()

--
-- Check that space truncation works for spaces created via
-- the internal API.
--
_ = box.space._space:insert{512, 1, 'test', engine, 0, {temporary = false}, {}}
_ = box.space._index:insert{512, 0, 'pk', 'tree', {unique = true}, {{0, 'unsigned'}}}
_ = box.space.test:insert{123}
box.space.test:select()
box.space.test:truncate()
box.space.test:select()
box.space.test:drop()

--
-- Check that a space cannot be dropped if it has a record
-- in _truncate space.
--
s = box.schema.create_space('test', {engine = engine})
s:truncate()
_ = box.space._space:delete{s.id} -- error
_ = box.space._truncate:delete{s.id}
_ = box.space._space:delete{s.id} -- ok

--
-- Check that truncation of system spaces is not permitted.
--
box.space._space:truncate()
box.space._index:truncate()

--
-- Truncate space with no indexes.
--
s = box.schema.create_space('test', {engine = engine})
s:truncate()
s:drop()

--
-- Truncate empty space.
--
s = box.schema.create_space('test', {engine = engine})
_ = s:create_index('pk')
s:truncate()
s:select()
s:drop()

--
-- Truncate non-empty space.
--
s = box.schema.create_space('test', {engine = engine})
_ = s:create_index('i1', {parts = {1, 'unsigned'}})
_ = s:create_index('i2', {parts = {2, 'unsigned'}})
_ = s:create_index('i3', {parts = {3, 'string'}})
_ = s:insert{1, 3, 'a'}
_ = s:insert{2, 2, 'b'}
_ = s:insert{3, 1, 'c'}
s:truncate()
s.index.i1:select()
s.index.i2:select()
s.index.i3:select()
_ = s:insert{10, 30, 'x'}
_ = s:insert{20, 20, 'y'}
_ = s:insert{30, 10, 'z'}
s.index.i1:select()
s.index.i2:select()
s.index.i3:select()
s:drop()

--
-- Calling space.truncate concurrently.
--
s = box.schema.create_space('test', {engine = engine})
_ = s:create_index('pk')
c = fiber.channel(5)
for i = 1, 5 do fiber.create(function() s:truncate() c:put(true) end) end
for i = 1, 5 do c:get() end
s:drop()

--
-- Check that space truncation is persistent.
--
-- The test checks the following cases:
-- - Create and truncate before snapshot
-- - Create before snapshot, truncate after snapshot
-- - Create and truncate after snapshot
--
s1 = box.schema.create_space('test1', {engine = engine})
_ = s1:create_index('i1', {parts = {1, 'unsigned'}})
_ = s1:create_index('i2', {parts = {2, 'unsigned'}})
_ = s1:insert{1, 3}
_ = s1:insert{2, 2}
_ = s1:insert{3, 1}
s1:truncate()
_ = s1:insert{123, 321}
s2 = box.schema.create_space('test2', {engine = engine})
_ = s2:create_index('i1', {parts = {1, 'unsigned'}})
_ = s2:create_index('i2', {parts = {2, 'unsigned'}})
_ = s2:insert{10, 30}
_ = s2:insert{20, 20}
_ = s2:insert{30, 10}
box.snapshot()
_ = s1:insert{321, 123}
s2:truncate()
_ = s2:insert{456, 654}
s3 = box.schema.create_space('test3', {engine = engine})
_ = s3:create_index('i1', {parts = {1, 'unsigned'}})
_ = s3:create_index('i2', {parts = {2, 'unsigned'}})
_ = s3:insert{100, 300}
_ = s3:insert{200, 200}
_ = s3:insert{300, 100}
s3:truncate()
_ = s3:insert{789, 987}
-- Check that index drop, create, and alter called after space
-- truncate do not break recovery (gh-2615)
s4 = box.schema.create_space('test4', {engine = engine})
_ = s4:create_index('i1', {parts = {1, 'string'}})
_ = s4:create_index('i3', {parts = {3, 'string'}})
_ = s4:insert{'zzz', 111, 'yyy'}
s4:truncate()
s4.index.i3:drop()
_ = s4:create_index('i2', {parts = {2, 'string'}})
s4.index.i1:alter({parts = {1, 'string', 2, 'string'}})
_ = s4:insert{'abc', 'cba'}
test_run:cmd('restart server default')
s1 = box.space.test1
s2 = box.space.test2
s3 = box.space.test3
s4 = box.space.test4
s1.index.i1:select()
s1.index.i2:select()
s2.index.i1:select()
s2.index.i2:select()
s3.index.i1:select()
s3.index.i2:select()
s4.index.i1:select()
s4.index.i2:select()
s1:drop()
s2:drop()
s3:drop()
s4:drop()

-- Truncate should fail in no write access for the space
engine = test_run:get_cfg('engine')
s = box.schema.create_space('access_truncate', {engine = engine})
_ = s:create_index('pk')
s:insert({1, 2, 3})
s:insert({3, 2, 1})
box.schema.user.grant('guest', 'execute', 'universe')
box.schema.user.grant('guest', 'read', 'space', 'access_truncate')
con = require('net.box').connect(box.cfg.listen)
con:eval([[box.space.access_truncate:truncate()]])
con.space.access_truncate:select()
box.schema.user.grant('guest', 'write', 'space', 'access_truncate')
con:eval([[box.space.access_truncate:truncate()]])
con.space.access_truncate:select()
con:close()
box.schema.user.revoke('guest', 'execute', 'universe')
box.schema.user.revoke('guest', 'read,write', 'space', 'access_truncate')
s:drop()
