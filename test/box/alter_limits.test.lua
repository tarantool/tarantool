env = require('test_run')
test_run = env.new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua...\"]:<line>: '")

-- ----------------------------------------------------------------
-- LIMITS
-- ----------------------------------------------------------------
box.schema.SYSTEM_ID_MIN
box.schema.FIELD_MAX
box.schema.INDEX_FIELD_MAX
box.schema.NAME_MAX
box.schema.INDEX_ID
box.schema.SPACE_ID
box.schema.INDEX_MAX
box.schema.SPACE_MAX
box.schema.SYSTEM_ID_MAX
box.schema.SCHEMA_ID
box.schema.FORMAT_ID_MAX
-- ----------------------------------------------------------------
-- CREATE SPACE
-- ----------------------------------------------------------------

s = box.schema.space.create('tweedledum')
-- space already exists
box.schema.space.create('tweedledum')
-- create if not exists
s = box.schema.space.create('tweedledum', { if_not_exists = true })

s:drop()
-- no such space
s:drop()
-- no such engine
box.schema.space.create('tweedleedee', { engine = 'unknown' })
-- explicit space id
s = box.schema.space.create('tweedledum', { id = 3000 })
s.id
-- duplicate id
err, res = pcall(function() return box.schema.space.create('tweedledee', { id = 3000 }) end)
assert(res.code == box.error.TUPLE_FOUND)
-- stupid space id
box.schema.space.create('tweedledee', { id = 'tweedledee' })
s:drop()
-- too long space name
box.schema.space.create(string.rep('t', box.schema.NAME_MAX + 1))
-- too long space engine name
box.schema.space.create('tweedleedee', { engine = string.rep('too-long', 100) })
-- space name limit
box.schema.space.create(string.rep('t', box.schema.NAME_MAX)..'_')
s = box.schema.space.create(string.rep('t', box.schema.NAME_MAX - 1)..'_')
s.name:len()
s:drop()
s = box.schema.space.create(string.rep('t', box.schema.NAME_MAX - 2)..'_')
s.name:len()
s:drop()
-- space with no indexes - test update, delete, select, truncate
s = box.schema.space.create('tweedledum')
s:insert{0}
s:select{}
s:delete{0}
s:update(0, {{"=", 1, 0}})
s:insert{0}
s.index[0]
s:truncate()
s.enabled
-- enabled/disabled transition
index = s:create_index('primary', { type = 'hash' })
s.enabled
-- rename space - same name
s:rename('tweedledum')
s.name
-- rename space - different name
s:rename('tweedledee')
s.name
-- the reference from box.space[] to the space by old name should be gone
box.space['tweedledum']
-- rename space - bad name
s:rename(string.rep('t', box.schema.NAME_MAX * 2))
s.name
-- access to a renamed space
s:insert{0}
s:delete{0}
-- cleanup
s:drop()
-- check DDL on invalid space object
s:create_index('primary')
s:rename('xxx')
s:drop()
-- create a space with reserved id (ok, but warns in the log)
s = box.schema.space.create('test', { id = 256 })
s.id
s:drop()
s = box.schema.space.create('test', { field_count = 2 })
s.field_count
index = s:create_index('primary')
-- field_count actually works
s:insert{1}
s:insert{1, 2}
s:insert{1, 2, 3}
s:select{}
FIELD_COUNT = 4
-- increase field_count -- error

box.space['_space']:update(s.id, {{"=", FIELD_COUNT + 1, 3}})
s:select{}
-- decrease field_count - error
box.space['_space']:update(s.id, {{"=", FIELD_COUNT + 1, 1}})
-- remove field_count - ok
_ = box.space['_space']:update(s.id, {{"=", FIELD_COUNT + 1, 0}})
s:select{}
-- increase field_count - error
box.space['_space']:update(s.id, {{"=", FIELD_COUNT + 1, 3}})
s:truncate()
s:select{}
-- set field_count of an empty space
_ = box.space['_space']:update(s.id, {{"=", FIELD_COUNT + 1, 3}})
s:select{}
-- field_count actually works
s:insert{3, 4}
s:insert{3, 4, 5}
s:insert{3, 4, 5, 6}
s:insert{7, 8, 9}
s:select{}
-- check transition of space from enabled to disabled on
-- deletion of the primary key
s.enabled
s.index[0]:drop()
s.enabled
s.index[0]

-- "disabled" on
-- deletion of primary key
s:drop()
-- ----------------------------------------------------------------
-- CREATE INDEX
-- ----------------------------------------------------------------
--
s = box.schema.space.create('test')
test_run:cmd("setopt delimiter ';'")
for k=1, box.schema.INDEX_MAX, 1 do
    index = s:create_index('i'..k, { type = 'hash' })
end;
-- cleanup
for k=2, box.schema.INDEX_MAX, 1 do
    s.index['i'..k]:drop()
end;
test_run:cmd("setopt delimiter ''");
-- test limits enforced in key_def_check:
-- unknown index type
index = s:create_index('test', { type = 'nosuchtype' })
-- hash index is not unique
index = s:create_index('test', { type = 'hash', unique = false })
-- bitset index is unique
index = s:create_index('test', { type = 'bitset', unique = true })
-- bitset index is multipart
index = s:create_index('test', { type = 'bitset', parts = {1, 'unsigned', 2, 'unsigned'}})
-- part count must be positive
index = s:create_index('test', { type = 'hash', parts = {}})
-- unknown field type
index = s:create_index('test', { type = 'hash', parts = { 2, 'nosuchtype' }})
index = s:create_index('test', { type = 'hash', parts = { 2, 'any' }})
index = s:create_index('test', { type = 'hash', parts = { 2, 'array' }})
index = s:create_index('test', { type = 'hash', parts = { 2, 'map' }})
index = s:create_index('test', { type = 'rtree', parts = { 2, 'nosuchtype' }})
index = s:create_index('test', { type = 'rtree', parts = { 2, 'any' }})
index = s:create_index('test', { type = 'rtree', parts = { 2, 'map' }})
-- bad field no
index = s:create_index('test', { type = 'hash', parts = { 'qq', 'nosuchtype' }})
-- big field no
index = s:create_index('test', { type = 'hash', parts = { box.schema.FIELD_MAX, 'unsigned' }})
index = s:create_index('test', { type = 'hash', parts = { box.schema.FIELD_MAX - 1, 'unsigned' }})
index = s:create_index('test', { type = 'hash', parts = { box.schema.FIELD_MAX + 90, 'unsigned' }})
index = s:create_index('test', { type = 'hash', parts = { box.schema.INDEX_FIELD_MAX + 1, 'unsigned' }})
index = s:create_index('t1', { type = 'hash', parts = { box.schema.INDEX_FIELD_MAX, 'unsigned' }})
index = s:create_index('t2', { type = 'hash', parts = { box.schema.INDEX_FIELD_MAX - 1, 'unsigned' }})
-- cleanup
s:drop()
s = box.schema.space.create('test')
-- same part can't be indexed twice
index = s:create_index('t1', { type = 'hash', parts = { 1, 'unsigned', 1, 'string' }})
-- a lot of key parts
parts = {}
test_run:cmd("setopt delimiter ';'")
for k=1, box.schema.INDEX_PART_MAX + 1, 1 do
    table.insert(parts, k)
    table.insert(parts, 'unsigned')
end;
#parts;
index = s:create_index('t1', { type = 'hash', parts = parts});
parts = {};
for k=1, box.schema.INDEX_PART_MAX, 1 do
    table.insert(parts, k + 1)
    table.insert(parts, 'unsigned')
end;
#parts;
index = s:create_index('t1', { type = 'hash', parts = parts});
test_run:cmd("setopt delimiter ''");
-- this is actually incorrect since parts is a lua table
-- and length of a lua table which has index 0 set is not correct
#s.index[0].parts
-- cleanup
s:drop()
-- check costraints in tuple_format_new()
s = box.schema.space.create('test')
index = s:create_index('t1', { type = 'hash' })
-- field type contradicts field type of another index
index = s:create_index('t2', { type = 'hash', parts = { 1, 'string' }})
-- ok
index = s:create_index('t2', { type = 'hash', parts = { 2, 'string' }})
-- don't allow drop of the primary key in presence of other keys
s.index[0]:drop()
-- cleanup
s:drop()
-- index name, name manipulation
s = box.schema.space.create('test')
index = s:create_index('primary', { type = 'hash' })
-- space cache is updated correctly
s.index[0].name
s.index[0].id
s.index[0].type
s.index['primary'].name
s.index['primary'].id
s.index['primary'].type
s.index.primary.name
s.index.primary.id
-- other properties are preserved
s.index.primary.type
s.index.primary.unique
s.index.primary:rename('new')
s.index[0].name
s.index.primary
s.index.new.name
-- too long name
s.index[0]:rename(string.rep('t', box.schema.NAME_MAX)..'_')
s.index[0].name
s.index[0]:rename(string.rep('t', box.schema.NAME_MAX - 1)..'_')
s.index[0].name:len()
s.index[0]:rename(string.rep('t', box.schema.NAME_MAX - 2)..'_')
s.index[0].name:len()
s.index[0]:rename('primary')
s.index.primary.name
-- cleanup
s:drop()
-- modify index
s = box.schema.space.create('test')
index = s:create_index('primary', { type = 'hash' })
-- correct error on misuse of alter
s.index.primary.alter({unique=false})
s.index.primary:alter({unique=false})
-- unique -> non-unique, index type
s.index.primary:alter({type='tree', unique=false, name='pk'})
s.index.primary.name
s.index.primary.id
s.index.pk.type
s.index.pk.unique
s.index.pk:rename('primary')
index = s:create_index('second', { type = 'tree', parts = {  2, 'string' } })
s.index.second.id
index = s:create_index('third', { type = 'hash', parts = {  3, 'unsigned' } })
err, res = pcall(function() return s.index.third:rename('second') end)
assert(res.code == box.error.TUPLE_FOUND)
s.index.third.id
s.index.second:drop()
s.index.third:alter({name = 'second'})
s.index.third
s.index.second.name
s.index.second.id
s:drop()
-- ----------------------------------------------------------------
-- BUILD INDEX: changes of a non-empty index
-- ----------------------------------------------------------------
s = box.schema.space.create('full')
index = s:create_index('primary', { type = 'tree', parts =  { 1, 'string' }})
s:insert{'No such movie', 999}
s:insert{'Barbara', 2012}
s:insert{'Cloud Atlas', 2012}
s:insert{'Almanya - Willkommen in Deutschland', 2011}
s:insert{'Halt auf freier Strecke', 2011}
s:insert{'Homevideo', 2011}
s:insert{'Die Fremde', 2010}
-- create index with data
index = s:create_index('year', { type = 'tree', unique=false, parts = { 2, 'unsigned'} })
s.index.primary:select{}
-- a duplicate in the created index
index = s:create_index('nodups', { type = 'tree', unique=true, parts = { 2, 'unsigned'} })
-- change of non-unique index to unique: same effect
s.index.year:alter({unique=true})
s.index.primary:select{}
-- ambiguous field type
index = s:create_index('string', { type = 'tree', unique =  false, parts = { 2, 'string'}})
-- create index on a non-existing field
index = s:create_index('nosuchfield', { type = 'tree', unique = true, parts = { 3, 'string'}})
s.index.year:drop()
s:insert{'Der Baader Meinhof Komplex', '2009 '}
-- create an index on a field with a wrong type
index = s:create_index('year', { type = 'tree', unique = false, parts = { 2, 'unsigned'}})
-- a field is missing
s:replace{'Der Baader Meinhof Komplex'}
index = s:create_index('year', { type = 'tree', unique = false, parts = { 2, 'unsigned'}})
s:drop()
-- unique -> non-unique transition
s = box.schema.space.create('test')
-- primary key must be unique
index = s:create_index('primary', { unique = false })
-- create primary key
index = s:create_index('primary', { type = 'hash' })
s:insert{1, 1}
index = s:create_index('secondary', { type = 'tree', unique = false, parts = {2, 'unsigned'}})
s:insert{2, 1}
s.index.secondary:alter{ unique = true }
s:delete{2}
s.index.secondary:alter{ unique = true }
s:insert{2, 1}
s:insert{2, 2}
s.index.secondary:alter{ unique = false}
s:insert{3, 2}
-- changing index id is not allowed
s.index.secondary:alter{ id = 10}
s:drop()
-- ----------------------------------------------------------------
-- SPACE CACHE: what happens to a space cache when an object is gone
-- ----------------------------------------------------------------
s = box.schema.space.create('test')
s1 = s
index = s:create_index('primary')
s1.index.primary.id
primary = s1.index.primary
s.index.primary:drop()
primary.id
primary:select{}
s:drop()
-- @todo: add a test case for dangling iterator (currently no checks
-- for a dangling iterator in the code
-- ----------------------------------------------------------------
-- ----------------------------------------------------------------
-- RECOVERY: check that all indexes are correctly built
-- during recovery regardless of when they are created
-- ----------------------------------------------------------------
-- primary, secondary keys in a snapshot
s_empty = box.schema.space.create('s_empty')
indexe1 = s_empty:create_index('primary')
indexe2 = s_empty:create_index('secondary', { type = 'hash', unique = true, parts = {2, 'unsigned'}})

s_full = box.schema.space.create('s_full')
indexf1 = s_full:create_index('primary')
indexf2 = s_full:create_index('secondary', { type = 'hash', unique = true, parts = {2, 'unsigned'}})

s_full:insert{1, 1, 'a'}
s_full:insert{2, 2, 'b'}
s_full:insert{3, 3, 'c'}
s_full:insert{4, 4, 'd'}
s_full:insert{5, 5, 'e'}

s_nil = box.schema.space.create('s_nil')

s_drop = box.schema.space.create('s_drop')

box.snapshot()

s_drop:drop()

indexn1 = s_nil:create_index('primary', { type = 'hash'})
s_nil:insert{1,2,3,4,5,6}
s_nil:insert{7, 8, 9, 10, 11,12}
indexn2 = s_nil:create_index('secondary', { type = 'tree', unique=false, parts = {2, 'unsigned', 3, 'unsigned', 4, 'unsigned'}})
s_nil:insert{13, 14, 15, 16, 17}

r_empty = box.schema.space.create('r_empty')
indexe1 = r_empty:create_index('primary')
indexe2 = r_empty:create_index('secondary', { type = 'hash', unique = true, parts = {2, 'unsigned'}})

r_full = box.schema.space.create('r_full')
indexf1 = r_full:create_index('primary', { type = 'tree', unique = true, parts = {1, 'unsigned'}})
indexf2 = r_full:create_index('secondary', { type = 'hash', unique = true, parts = {2, 'unsigned'}})

r_full:insert{1, 1, 'a'}
r_full:insert{2, 2, 'b'}
r_full:insert{3, 3, 'c'}
r_full:insert{4, 4, 'd'}
r_full:insert{5, 5, 'e'}

indexf1 = s_full:create_index('multikey', { type = 'tree', unique = true, parts = { 2, 'unsigned', 3, 'string'}})
s_full:insert{6, 6, 'f'}
s_full:insert{7, 7, 'g'}
s_full:insert{8, 8, 'h'}

r_disabled = box.schema.space.create('r_disabled')

test_run:cmd("restart server default")

s_empty = box.space['s_empty']
s_full = box.space['s_full']
s_nil = box.space['s_nil']
s_drop = box.space['s_drop']
r_empty = box.space['r_empty']
r_full = box.space['r_full']
r_disabled = box.space['r_disabled']

s_drop

s_empty.index.primary.type
s_full.index.primary.type
r_empty.index.primary.type
r_full.index.primary.type
s_nil.index.primary.type

s_empty.index.primary.name
s_full.index.primary.name
r_empty.index.primary.name
r_full.index.primary.name
s_nil.index.primary.name

s_empty.enabled
s_full.enabled
r_empty.enabled
r_full.enabled
s_nil.enabled
r_disabled.enabled

s_empty.index.secondary.name
s_full.index.secondary.name
r_empty.index.secondary.name
r_full.index.secondary.name
s_nil.index.secondary.name

s_empty.index.primary:count(1)
s_full.index.primary:count(1)
r_empty.index.primary:count(1)
r_full.index.primary:count(1)
s_nil.index.primary:count(1)

s_empty.index.secondary:count(1)
s_full.index.secondary:count(1)
r_empty.index.secondary:count(1)
r_full.index.secondary:count(1)
s_nil.index.secondary:count(1)

-- gh-503 if_not_exits option in create index
i1 = s_empty:create_index("test")
i1:select{}
i2 = s_empty:create_index("test")
i3 = s_empty:create_index("test", { if_not_exists = true } )
i3:select{}

-- cleanup
s_empty:drop()
s_full:drop()
r_empty:drop()
r_full:drop()
s_nil:drop()
r_disabled:drop()

--
-- @todo usability
-- ---------
-- - space name in all error messages!
--         error: Duplicate key exists in unique index 1 (ugly)
--
-- @todo features
--------
-- - ffi function to enable/disable space
--

test_run:cmd("clear filter")
