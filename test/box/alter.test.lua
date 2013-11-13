_space = box.space[box.schema.SPACE_ID]
_index = box.space[box.schema.INDEX_ID]
--
-- Test insertion into a system space - verify that
-- mandatory fields are required.
--
_space:insert(_space.n, 5, 'test')
--
-- Bad space id
--
_space:insert('hello', 'world', 'test')
--
-- Can't create a space which has wrong arity - arity must be NUM
--
_space:insert(_space.n, 'world', 'test')
--
-- There is already a tuple for the system space
--
_space:insert(_space.n, 0, '_space')
_space:replace(_space.n, 0, '_space')
_space:insert(_index.n, 0, '_index')
_space:replace(_index.n, 0, '_index')
--
-- Can't change properties of a space
--
_space:replace(_space.n, 0, '_space')
--
-- Can't drop a system space
--
_space:delete(_space.n)
_space:delete(_index.n)
--
-- Can't change properties of a space
--
_space:update(_space.n, '+p', 0, 1)
_space:update(_space.n, '+p', 0, 2)
--
-- Create a space
--
t = box.auto_increment(_space.n, 0, 'hello')
-- Check that a space exists
space = box.space[t[0]]
space.n
space.arity
space.index[0]
--
-- check dml - the space has no indexes yet, but must not crash on DML
--
space:select(0, 0)
space:insert(0, 0)
space:replace(0, 0)
space:update(0, '+p', 0, 1)
space:delete(0)
t = _space:delete(space.n)
space_deleted = box.space[t[0]]
space_deleted
space:replace(0)
_index:insert(_space.n, 0, 'primary', 'tree', 1, 1, 0, 'num')
_index:replace(_space.n, 0, 'primary', 'tree', 1, 1, 0, 'num')
_index:insert(_index.n, 0, 'primary', 'tree', 1, 2, 0, 'num', 1, 'num')
_index:replace(_index.n, 0, 'primary', 'tree', 1, 2, 0, 'num', 1, 'num')
_index:select(0)
-- modify indexes of a system space
_index:delete(_index.n, 0)
_space:insert(1000, 0, 'hello')
_index:insert(1000, 0, 'primary', 'tree', 1, 1, 0, 'num')
box.space[1000]:insert(0, 'hello, world')
box.space[1000]:drop()
box.space[1000]
-- test that after disabling triggers on system spaces we still can
-- get a correct snapshot
_index:run_triggers(false)
_space:run_triggers(false)
box.snapshot()
--# stop server default
--# start server default
box.space['_space']:insert(1000, 0, 'test')
box.space[1000].n
box.space['_space']:delete(1000)
box.space[1000]
