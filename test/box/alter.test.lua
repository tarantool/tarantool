_space = box.space[box.schema.SPACE_ID]
_index = box.space[box.schema.INDEX_ID]
ADMIN = 1
--
-- Test insertion into a system space - verify that
-- mandatory fields are required.
--
_space:insert{_space.id, ADMIN, 'test', 5 }
--
-- Bad space id
--
_space:insert{'hello', 'world', 'test'}
--
-- Can't create a space which has wrong field count - field_count must be NUM
--
_space:insert{_space.id, ADMIN, 'test', 'world'}
--
-- There is already a tuple for the system space
--
_space:insert{_space.id, ADMIN, '_space', 'memtx', 0}
_space:replace{_space.id, ADMIN, '_space', 'memtx', 0}
_space:insert{_index.id, ADMIN, '_index', 'memtx', 0}
_space:replace{_index.id, ADMIN, '_index', 'memtx', 0}
--
-- Can't change properties of a space
--
_space:replace{_space.id, ADMIN, '_space', 'memtx', 0}
--
-- Can't drop a system space
--
_space:delete{_space.id}
_space:delete{_index.id}
--
-- Can't change properties of a space
--
_space:update({_space.id}, {{'+', 1, 1}})
_space:update({_space.id}, {{'+', 1, 2}})
--
-- Create a space
--
t = _space:auto_increment{ADMIN, 'hello', 'memtx', 0}
-- Check that a space exists
space = box.space[t[1]]
space.id
space.field_count
space.index[0]
--
-- check dml - the space has no indexes yet, but must not crash on DML
--
space:select{0}
space:insert{0, 0}
space:replace{0, 0}
space:update({0}, {{'+', 1, 1}})
space:delete{0}
t = _space:delete{space.id}
space_deleted = box.space[t[1]]
space_deleted
space:replace{0}
_index:insert{_space.id, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:replace{_space.id, 0, 'primary', 'tree', 1, 1, 0, 'num'}
_index:insert{_index.id, 0, 'primary', 'tree', 1, 2, 0, 'num', 1, 'num'}
_index:replace{_index.id, 0, 'primary', 'tree', 1, 2, 0, 'num', 1, 'num'}
_index:select{}
-- modify indexes of a system space
_index:delete{_index.id, 0}
_space:insert{1000, ADMIN, 'hello', 'memtx', 0}
_index:insert{1000, 0, 'primary', 'tree', 1, 1, 0, 'num'}
box.space[1000]:insert{0, 'hello, world'}
box.space[1000]:drop()
box.space[1000]
-- test that after disabling triggers on system spaces we still can
-- get a correct snapshot
_index:run_triggers(false)
_space:run_triggers(false)
box.snapshot()
--# stop server default
--# start server default
ADMIN = 1
box.space['_space']:insert{1000, ADMIN, 'test', 'memtx', 0}
box.space[1000].id
box.space['_space']:delete{1000}
box.space[1000]

--------------------------------------------------------------------------------
-- #197: box.space.space0:len() returns an error if there is no index
--------------------------------------------------------------------------------

space = box.schema.create_space('gh197')
space:len()
space:truncate()
space:pairs():totable()
space:drop()

--------------------------------------------------------------------------------
-- #198: names like '' and 'x.y' and 5 and 'primary ' are legal
--------------------------------------------------------------------------------

-- invalid identifiers
box.schema.create_space('invalid.identifier')
box.schema.create_space('invalid identifier')
box.schema.create_space('primary ')
box.schema.create_space('5')
box.schema.create_space('')

-- valid identifiers
box.schema.create_space('_Abcde'):drop()
box.schema.create_space('_5'):drop()
box.schema.create_space('valid_identifier'):drop()
box.schema.create_space('ынтыпрайзный_空間'):drop() -- unicode
box.schema.create_space('utf8_наше_Фсё'):drop() -- unicode

space = box.schema.create_space('test')

-- invalid identifiers
space:create_index('invalid.identifier')
space:create_index('invalid identifier')
space:create_index('primary ')
space:create_index('5')
space:create_index('')

space:drop()
-- gh-57 Confusing error message when trying to create space with a
-- duplicate id
auto = box.schema.create_space('auto_original')
auto2 = box.schema.create_space('auto', {id = auto.id})
box.schema.space.drop('auto')
auto2
box.schema.create_space('auto_original', {id = auto.id})
auto:drop()
