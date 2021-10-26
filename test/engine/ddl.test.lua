test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- space create/drop
space = box.schema.space.create('test', { engine = engine })
space:drop()


-- space index create/drop
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
space:drop()


-- space index create/drop alter
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
_index = box.space[box.schema.INDEX_ID]
_index:delete{102, 0}
space:drop()


-- space index create/drop tree string
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', {type = 'tree', parts = {1, 'string'}})
space:insert({'test'})
space:drop()


-- space index create/drop tree num
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', {type = 'tree', parts = {1, 'unsigned'}})
space:insert({13})
space:drop()


-- space index create/drop tree multi-part num
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', {type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'}})
space:insert({13})
space:drop()


-- space index size
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
primary = space.index[0]
primary:count()
space:insert({13})
space:insert({14})
space:insert({15})
primary:count()
space:drop()

-- Key part max
parts = {}
for i=1,box.schema.INDEX_PART_MAX,1 do parts[2 * i - 1] = i; parts[2 * i] = 'unsigned' end
space = box.schema.space.create('test', { engine = engine })
_ = space:create_index('primary', { type = 'tree', parts = parts })

tuple = {}
for i=1,box.schema.INDEX_PART_MAX,1 do tuple[i] = i; end
space:replace(tuple)
-- https://github.com/tarantool/tarantool/issues/1651 and https://github.com/tarantool/tarantool/issues/1671
-- space:upsert(tuple, {{'=', box.schema.INDEX_PART_MAX + 1, 100500}})
space:get(tuple)
space:select(tuple)
_ = space:delete(tuple)

space:drop()

-- Too many key parts
parts = {}
for i=1,box.schema.INDEX_PART_MAX + 1,1 do parts[2 * i - 1] = i; parts[2 * i] = 'unsigned' end
space = box.schema.space.create('test', { engine = engine })
_ = space:create_index('primary', { type = 'tree', parts = parts })
space:drop()

--
-- vy_mem of primary index contains statements with two formats.
--
space = box.schema.space.create('test1', { engine = engine })
pk = space:create_index('primary1')
idx2 = space:create_index('idx2', { parts = {2, 'unsigned'} })
space:replace({3, 8, 1})
idx2:select{}
space:get{3}
iter_obj = space:pairs(2, {iterator = 'GT'})
idx2:drop()
space:replace({4, 5, 6})
space:select{}
space:drop()

-- Change index name
space = box.schema.space.create('test', {engine = engine})
pk = space:create_index('pk')
space:replace{1}
space:replace{2}
space:replace{3}
box.space._index:select{space.id}[1][3]
pk:alter({name = 'altered_pk'})
box.space._index:select{space.id}[1][3]
space:drop()

--new index format
space = box.schema.space.create('test', {engine = engine})
pk = space:create_index('pk', {parts={{field1 = 1, type = 'unsigned'}}})
pk = space:create_index('pk', {parts={{field = 0, type = 'unsigned'}}})
pk = space:create_index('pk', {parts={{field = 1, type = 'const char *'}}})
pk = space:create_index('pk', {parts={{field = 1, type = 'unsigned'}}})
pk.parts
pk:drop()
pk = space:create_index('pk', {parts={{1, 'unsigned'}}})
pk.parts
pk:drop()
pk = space:create_index('pk', {parts={{1, type='unsigned'}}})
pk.parts
space:insert{1, 2, 3}
pk:drop()
space:drop()

--
-- gh-2893: inherit index part type from a format, if a parts array
-- is omited.
--
format = {{'field1', 'scalar'}}
s = box.schema.space.create('test', {engine = engine, format = format})
pk = s:create_index('pk')
pk.parts[1].type
s:drop()

-- Ensure type 'any' to be not inherited.
format = {{'field1'}}
s = box.schema.space.create('test', {engine = engine, format = format})
pk = s:create_index('pk')
pk.parts[1].type
s:drop()

--
-- gh-3414: do not increase schema_version on space:truncate()
--
-- update schema_version on space.create()
sch_ver = box.internal.schema_version
v = sch_ver()
s = box.schema.create_space('test')
v + 1 == sch_ver()
-- update schema_version on space:create_index()
prim = s:create_index("primary")
v + 2 == sch_ver()
-- do not change schema_version on space.truncate()
s:truncate()
v + 2 == sch_ver()
-- update schema_version on index.alter()
prim:alter{name="new_primary"}
v + 3 == sch_ver()
-- update schema_version on index.drop()
box.schema.index.drop(s.id, 0)
v + 4 == sch_ver()
-- update schema_version on space.drop()
s:drop()
v + 5 == sch_ver()

--
-- gh-3229: update optionality if a space format is changed too,
-- not only when indexes are updated.
--
s = box.schema.space.create('test', {engine = engine})
format = {}
format[1] = {'field1', 'unsigned'}
format[2] = {'field2', 'unsigned', is_nullable = true}
format[3] = {'field3', 'unsigned'}
s:format(format)
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {{2, 'unsigned', is_nullable = true}}})
s:replace{2, 3, 4}
s:format({})
s:insert({1})
s:insert({4, 5})
s:insert({3, 4})
s:insert({0})
_ = s:delete({1})
s:select({})
pk:get({4})
sk:select({box.NULL})
sk:get({5})
s:drop()

--
-- Modify key definition without index rebuild.
--
s = box.schema.space.create('test', {engine = engine})
i1 = s:create_index('i1', {unique = true,  parts = {1, 'unsigned'}})
i2 = s:create_index('i2', {unique = false, parts = {2, 'unsigned'}})
i3 = s:create_index('i3', {unique = true,  parts = {3, 'unsigned'}})

_ = s:insert{1, 2, 3}
box.snapshot()
_ = s:insert{3, 2, 1}

i1:alter{parts = {1, 'integer'}}
_ = s:insert{-1, 2, 2}
i1:select{}
i2:select{}
i3:select{}

i2:alter{parts = {2, 'integer'}}
i3:alter{parts = {3, 'integer'}}
_ = s:replace{-1, -1, -1}
i1:select{}
i2:select{}
i3:select{}

box.snapshot()
_ = s:replace{-1, -2, -3}
_ = s:replace{-3, -2, -1}
i1:select{}
i2:select{}
i3:select{}

s:drop()

--
-- gh-2652: validate space format.
--
s = box.schema.space.create('test', { engine = engine, format = "format" })
format = { { name = 100 } }
s = box.schema.space.create('test', { engine = engine, format = format })
long = string.rep('a', box.schema.NAME_MAX + 1)
format = { { name = long } }
s = box.schema.space.create('test', { engine = engine, format = format })
format = { { name = 'id', type = '100' } }
s = box.schema.space.create('test', { engine = engine, format = format })
format = { setmetatable({}, { __serialize = 'map' }) }
s = box.schema.space.create('test', { engine = engine, format = format })

-- Ensure the format is updated after index drop.
format = { { name = 'id', type = 'unsigned' } }
s = box.schema.space.create('test', { engine = engine, format = format })
pk = s:create_index('pk')
sk = s:create_index('sk', { parts = { 2, 'string' } })
s:replace{1, 1}
sk:drop()
s:replace{1, 1}
s:drop()

-- Check index parts conflicting with space format.
format = { { name='field1', type='unsigned' }, { name='field2', type='string' }, { name='field3', type='scalar' } }
s = box.schema.space.create('test', { engine = engine, format = format })
pk = s:create_index('pk')
sk1 = s:create_index('sk1', { parts = { 2, 'unsigned' } })

-- Check space format conflicting with index parts.
sk3 = s:create_index('sk3', { parts = { 2, 'string' } })
format[2].type = 'unsigned'
s:format(format)
s:format()
s.index.sk3.parts

-- Space format can be updated, if conflicted index is deleted.
sk3:drop()
s:format(format)
s:format()

-- Check deprecated field types.
format[2].type = 'num'
format[3].type = 'str'
format[4] = { name = 'field4', type = '*' }
format
s:format(format)
s:format()
s:replace{1, 2, '3', {4, 4, 4}}

-- Check not indexed fields checking.
s:truncate()
format[2] = {name='field2', type='string'}
format[3] = {name='field3', type='array'}
format[4] = {name='field4', type='number'}
format[5] = {name='field5', type='integer'}
format[6] = {name='field6', type='scalar'}
format[7] = {name='field7', type='map'}
format[8] = {name='field8', type='any'}
format[9] = {name='field9'}
s:format(format)

-- Check incorrect field types.
format[9] = {name='err', type='any'}
s:format(format)

s:replace{1, '2', {3, 3}, 4.4, -5, true, {value=7}, 8, 9}
s:replace{1, 2, {3, 3}, 4.4, -5, true, {value=7}, 8, 9}
s:replace{1, '2', 3, 4.4, -5, true, {value=7}, 8, 9}
s:replace{1, '2', {3, 3}, '4', -5, true, {value=7}, 8, 9}
s:replace{1, '2', {3, 3}, 4.4, -5.5, true, {value=7}, 8, 9}
s:replace{1, '2', {3, 3}, 4.4, -5, {6, 6}, {value=7}, 8, 9}
s:replace{1, '2', {3, 3}, 4.4, -5, true, {7}, 8, 9}
s:replace{1, '2', {3, 3}, 4.4, -5, true, {value=7}}
s:replace{1, '2', {3, 3}, 4.4, -5, true, {value=7}, 8}
s:truncate()

--
-- gh-1014: field names.
--
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2'}
format[3] = {name = 'field1'}
s:format(format)

s:drop()

-- https://github.com/tarantool/tarantool/issues/2815
-- Extend space format definition syntax
format = {{name='key',type='unsigned'}, {name='value',type='string'}}
s = box.schema.space.create('test', { engine = engine, format = format })
s:format()
s:format({'id', 'name'})
s:format()
s:format({'id', {'name1'}})
s:format()
s:format({'id', {'name2', 'string'}})
s:format()
s:format({'id', {'name', type = 'string'}})
s:format()
s:drop()

format = {'key', {'value',type='string'}}
s = box.schema.space.create('test', { engine = engine, format = format })
s:format()
s:drop()

s = box.schema.space.create('test', { engine = engine })
s:create_index('test', {parts = {'test'}})
s:create_index('test', {parts = {{'test'}}})
s:create_index('test', {parts = {{field = 'test'}}})
s:create_index('test', {parts = {1}}).parts
s:drop()

s = box.schema.space.create('test', { engine = engine })
s:format{{'test1', 'integer'}, 'test2', {'test3', 'integer'}, {'test4','scalar'}}
s:create_index('test', {parts = {'test'}})
s:create_index('test', {parts = {{'test'}}})
s:create_index('test', {parts = {{field = 'test'}}})
s:create_index('test1', {parts = {'test1'}}).parts
s:create_index('test2', {parts = {'test2'}}).parts
s:create_index('test3', {parts = {{'test1', 'integer'}}}).parts
s:create_index('test4', {parts = {{'test2', 'integer'}}}).parts
s:create_index('test5', {parts = {{'test2', 'integer'}}}).parts
s:create_index('test6', {parts = {1, 3}}).parts
s:create_index('test7', {parts = {'test1', 4}}).parts
s:create_index('test8', {parts = {{1, 'integer'}, {'test4', 'scalar'}}}).parts
s:drop()

--
-- gh-2800: space formats checking is broken.
--

decimal = require('decimal')
uuid = require('uuid')

-- Ensure that vinyl correctly process field count change.
s = box.schema.space.create('test', {engine = engine, field_count = 2})
pk = s:create_index('pk')
s:replace{1, 2}
t = box.space._space:select{s.id}[1]:totable()
t[5] = 1
box.space._space:replace(t)
s:drop()

-- Check field type changes.
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'any'}
format[3] = {name = 'field3', type = 'unsigned'}
format[4] = {name = 'field4', type = 'string'}
format[5] = {name = 'field5', type = 'number'}
format[6] = {name = 'field6', type = 'integer'}
format[7] = {name = 'field7', type = 'boolean'}
format[8] = {name = 'field8', type = 'scalar'}
format[9] = {name = 'field9', type = 'array'}
format[10] = {name = 'field10', type = 'map'}
format[11] = {name = 'field11', type = 'decimal'}
format[12] = {name = 'field12', type = 'uuid'}

s = box.schema.space.create('test', {engine = engine, format = format})
pk = s:create_index('pk')
t = s:replace{1, {2}, 3, '4', 5.5, -6, true, -8, {9, 9}, {val = 10}, decimal.new(-11.11), uuid.new()}

inspector:cmd("setopt delimiter ';'")
function fail_format_change(fieldno, new_type)
    local old_type = format[fieldno].type
    format[fieldno].type = new_type
    local ok, msg = pcall(s.format, s, format)
    format[fieldno].type = old_type
    return msg
end;

function ok_format_change(fieldno, new_type)
    local old_type = format[fieldno].type
    format[fieldno].type = new_type
    s:format(format)
    s:delete{1}
    format[fieldno].type = old_type
    s:format(format)
    s:replace(t)
end;
inspector:cmd("setopt delimiter ''");

-- any --X--> unsigned
fail_format_change(2, 'unsigned')

-- unsigned -----> any
ok_format_change(3, 'any')
-- unsigned --X--> string
fail_format_change(3, 'string')
-- unsigned -----> number
ok_format_change(3, 'number')
-- unsigned -----> integer
ok_format_change(3, 'integer')
-- unsigned -----> scalar
ok_format_change(3, 'scalar')
-- unsigned --X--> map
fail_format_change(3, 'map')
-- unsigned --X--> decimal
fail_format_change(3, 'decimal')
-- unsigned --X--> uuid
fail_format_change(3, 'uuid')

-- string -----> any
ok_format_change(4, 'any')
-- string -----> scalar
ok_format_change(4, 'scalar')
-- string --X--> boolean
fail_format_change(4, 'boolean')
-- string --X--> decimal
fail_format_change(4, 'decimal')
-- string --X--> uuid
fail_format_change(4, 'uuid')

-- number -----> any
ok_format_change(5, 'any')
-- number -----> scalar
ok_format_change(5, 'scalar')
-- number --X--> integer
fail_format_change(5, 'integer')
-- number --X--> decimal
fail_format_change(5, 'decimal')
-- number --X--> uuid
fail_format_change(5, 'uuid')

-- integer -----> any
ok_format_change(6, 'any')
-- integer -----> number
ok_format_change(6, 'number')
-- integer -----> scalar
ok_format_change(6, 'scalar')
-- integer --X--> unsigned
fail_format_change(6, 'unsigned')
-- integer --X--> decimal
fail_format_change(6, 'decimal')
-- integer --X--> uuid
fail_format_change(6, 'uuid')

-- boolean -----> any
ok_format_change(7, 'any')
-- boolean -----> scalar
ok_format_change(7, 'scalar')
-- boolean --X--> string
fail_format_change(7, 'string')
-- boolead --X--> decimal
fail_format_change(7, 'decimal')
-- boolean --X--> uuid
fail_format_change(7, 'uuid')

-- scalar -----> any
ok_format_change(8, 'any')
-- scalar --X--> unsigned
fail_format_change(8, 'unsigned')
-- scalar --X--> decimal
fail_format_change(8, 'decimal')
-- scalar --X--> uuid
fail_format_change(8, 'uuid')

-- array -----> any
ok_format_change(9, 'any')
-- array --X--> scalar
fail_format_change(9, 'scalar')
-- arary --X--> decimal
fail_format_change(9, 'decimal')
-- array --X--> uuid
fail_format_change(9, 'uuid')

-- map -----> any
ok_format_change(10, 'any')
-- map --X--> scalar
fail_format_change(10, 'scalar')
-- map --X--> decimal
fail_format_change(10, 'decimal')
-- map --X--> uuid
fail_format_change(10, 'uuid')

-- decimal ----> any
ok_format_change(11, 'any')
-- decimal ----> number
ok_format_change(11, 'number')
-- decimal ----> scalar
ok_format_change(11, 'scalar')
-- decimal --X--> string
fail_format_change(11, 'string')
-- decimal --X--> integer
fail_format_change(11, 'integer')
-- decimal --X--> unsigned
fail_format_change(11, 'unsigned')
-- decimal --X--> map
fail_format_change(11, 'map')
-- decimal --X--> array
fail_format_change(11, 'array')
-- decimal --X--> uuid
fail_format_change(11, 'uuid')

-- uuid ----> any
ok_format_change(12, 'any')
-- uuid --X--> number
fail_format_change(12, 'number')
-- uuid ----> scalar
ok_format_change(12, 'scalar')
-- uuid --X--> string
fail_format_change(12, 'string')
-- uuid --X--> integer
fail_format_change(12, 'integer')
-- uuid --X--> unsigned
fail_format_change(12, 'unsigned')
-- uuid --X--> map
fail_format_change(12, 'map')
-- uuid --X--> array
fail_format_change(12, 'array')
-- uuid --X--> decimal
fail_format_change(12, 'decimal')

s:drop()

-- Check new fields adding.
format = {}
s = box.schema.space.create('test', {engine = engine})
format[1] = {name = 'field1', type = 'unsigned'}
s:format(format) -- Ok, no indexes.
pk = s:create_index('pk')
format[2] = {name = 'field2', type = 'unsigned'}
s:format(format) -- Ok, empty space.
s:replace{1, 1}
format[2] = nil
s:format(format) -- Ok, can delete fields with no checks.
s:drop()

s = box.schema.space.create('test', {engine = engine, format = format})
pk = s:create_index('pk')
sk1 = s:create_index('sk1', {parts = {2, 'unsigned'}})
sk2 = s:create_index('sk2', {parts = {3, 'unsigned'}})
sk5 = s:create_index('sk5', {parts = {5, 'unsigned'}})
s:replace{1, 1, 1, 1, 1}
format[2] = {name = 'field2', type = 'unsigned'}
format[3] = {name = 'field3', type = 'unsigned'}
format[4] = {name = 'field4', type = 'any'}
format[5] = {name = 'field5', type = 'unsigned'}
-- Ok, all new fields are indexed or have type ANY, and new
-- field_count <= old field_count.
s:format(format)

s:replace{1, 1, 1, 1, 1, 1}
format[6] = {name = 'field6', type = 'unsigned'}
-- Ok, but check existing tuples for a new field[6].
s:format(format)

-- Fail, not enough fields.
s:replace{2, 2, 2, 2, 2}

s:replace{2, 2, 2, 2, 2, 2, 2}
format[7] = {name = 'field7', type = 'unsigned'}
-- Fail, the tuple {1, ... 1} is invalid for a new format.
s:format(format)
s:drop()

--
-- Allow to restrict space format, if corresponding restrictions
-- already are defined in indexes.
--
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
format = {}
format[1] = {name = 'field1'}
s:replace{1}
s:replace{100}
s:replace{0}
s:format(format)
s:format()
format[1].type = 'unsigned'
s:format(format)
s:format()
s:select{}
s:drop()

--
-- gh-1557: NULL in indexes.
--

NULL = require('msgpack').NULL

format = {}
format[1] = { name = 'field1', type = 'unsigned', is_nullable = true }
format[2] = { name = 'field2', type = 'unsigned', is_nullable = true }
s = box.schema.space.create('test', {engine = engine, format = format})
s:create_index('primary', { parts = { 'field1' } })
-- This is allowed, but the actual part is_nullable stays false.
pk = s:create_index('primary', { parts = {{'field1', is_nullable = false}} })
pk:drop()
format[1].is_nullable = false
s:format(format)
s:create_index('primary', { parts = {{'field1', is_nullable = true}} })

i = s:create_index('primary', { parts = {'field1'} })
i.parts

-- Check that is_nullable can't be set to false on non-empty space
s:insert({1, NULL})
format[1].is_nullable = true
-- The format is allowed since in primary index parts
-- is_nullable is still set to false.
s:format(format)
format[1].is_nullable = false
format[2].is_nullable = false
s:format(format)
_ = s:delete(1)
-- Disable is_nullable on empty space
s:format(format)
-- Disable is_nullable on a non-empty space.
format[2].is_nullable = true
s:format(format)
s:replace{1, 1}
format[2].is_nullable = false
s:format(format)
-- Enable is_nullable on a non-empty space.
format[2].is_nullable = true
s:format(format)
s:replace{1, box.NULL}
_ = s:delete{1}
s:format({})

i = s:create_index('secondary', { parts = {{2, 'string', is_nullable = true}} })
i.parts

s:insert({1, NULL})
s.index.secondary:alter({ parts = {{2, 'string', is_nullable = false} }})
_ = s:delete({1})
s.index.secondary:alter({ parts = {{2, 'string', is_nullable = false} }})
s:insert({1, NULL})
s:insert({2, 'xxx'})
s.index.secondary:alter({ parts = {{2, 'string', is_nullable = true} }})
s:insert({1, NULL})

s:drop()

s = box.schema.space.create('test', {engine = engine})
inspector:cmd("setopt delimiter ';'")
s:format({
    [1] = { name = 'id1', type = 'unsigned'},
    [2] = { name = 'id2', type = 'unsigned'},
    [3] = { name = 'id3', type = 'string'},
    [4] = { name = 'id4', type = 'string'},
    [5] = { name = 'id5', type = 'string'},
    [6] = { name = 'id6', type = 'string'},
});
inspector:cmd("setopt delimiter ''");
s:format()
_ = s:create_index('primary')
s:insert({1, 1, 'a', 'b', 'c', 'd'})
s:drop()

s = box.schema.space.create('test', {engine = engine})
idx = s:create_index('idx')
box.space.test == s
s:drop()

--
-- gh-3000: index modifying must change key_def parts and
-- comparators. They can be changed, if there was compatible index
-- parts change. For example, a part type was changed from
-- unsigned to number. In such a case comparators must be reset
-- and part types updated.
--
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk')
s:replace{1}
pk:alter{parts = {{1, 'integer'}}}
s:replace{-2}
s:select{}
s:drop()

--
-- Allow to change is_nullable in index definition on non-empty
-- space.
--
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk')
sk1 = s:create_index('sk1', {parts = {{2, 'unsigned', is_nullable = true}}})
sk2 = s:create_index('sk2', {parts = {{3, 'unsigned', is_nullable = false}}})
s:replace{1, box.NULL, 1}
sk1:alter({parts = {{2, 'unsigned', is_nullable = false}}})
s:replace{1, 1, 1}
sk1:alter({parts = {{2, 'unsigned', is_nullable = false}}})
s:replace{1, 1, box.NULL}
sk2:alter({parts = {{3, 'unsigned', is_nullable = true}}})
s:replace{1, 1, box.NULL}
s:replace{2, 10, 100}
s:replace{3, 0, 20}
s:replace{4, 15, 150}
s:replace{5, 9, box.NULL}
sk1:select{}
sk2:select{}
s:drop()

--
-- gh-3008: allow multiple types on the same field.
--
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'scalar'}
format[3] = {name = 'field3', type = 'integer'}
s = box.schema.space.create('test', {engine = engine, format = format})
pk = s:create_index('pk')
sk1 = s:create_index('sk1', {parts = {{2, 'number'}}})
sk2 = s:create_index('sk2', {parts = {{2, 'integer'}}})
sk3 = s:create_index('sk3', {parts = {{2, 'unsigned'}}})
sk4 = s:create_index('sk4', {parts = {{3, 'number'}}})
s:format()
s:replace{1, '100', -20.2}
s:replace{1, 100, -20.2}
s:replace{1, 100, -20}
s:replace{2, 50, 0}
s:replace{3, 150, -60}
s:replace{4, 0, 120}
pk:select{}
sk1:select{}
sk2:select{}
sk3:select{}
sk4:select{}

sk1:alter{parts = {{2, 'unsigned'}}}
sk2:alter{parts = {{2, 'unsigned'}}}
sk4:alter{parts = {{3, 'integer'}}}
s:replace{1, 50.5, 1.5}
s:replace{1, 50, 1.5}
s:replace{5, 5, 5}
sk1:select{}
sk2:select{}
sk3:select{}
sk4:select{}

sk1:drop()
sk2:drop()
sk3:drop()
-- Remove 'unsigned' constraints from indexes, and 'scalar' now
-- can be inserted in the second field.
s:replace{1, true, 100}
s:select{}
sk4:select{}
s:drop()

--
-- gh-3578: false-positive unique constraint violation check failure.
--
fiber = require('fiber')

s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
s:replace{1, 1, 1}

c = fiber.channel(1)
-- Note, in Vinyl DDL aborts writers before proceeding so we
-- use pcall() here. This is OK as we just want to check that
-- space.create_index doesn't fail when there are concurrent
-- updates.
_ = fiber.create(function() for i = 1, 10 do pcall(s.update, s, 1, {{'+', 3, 1}}) end c:put(true) end)

_ = s:create_index('sk', {parts = {2, 'unsigned'}})

c:get()
s:drop()

--
-- Creating/altering a secondary index of a non-empty space.
--
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')

_ = s:insert{1, 'zzz', 'aaa', 999}
_ = s:insert{2, 'yyy', 'bbb', 888}
_ = s:insert{3, 'xxx', 'ccc', 777}

box.snapshot()

_ = s:update(1, {{'!', -1, 'eee'}})
_ = s:upsert({2, '2', '2', -2}, {{'=', 4, -888}})
_ = s:replace(s:get(3):update{{'=', 3, box.NULL}})
_ = s:upsert({4, 'zzz', 'ddd', -666}, {{'!', -1, 'abc'}})

box.snapshot()

_ = s:update(1, {{'=', 5, 'fff'}})
_ = s:upsert({3, '3', '3', -3}, {{'=', 5, 'ggg'}})
_ = s:insert{5, 'xxx', 'eee', 555, 'hhh'}
_ = s:replace{6, 'yyy', box.NULL, -444}

s:select{}

s:create_index('sk', {parts = {2, 'string'}}) -- error: unique constraint
s:create_index('sk', {parts = {3, 'string'}}) -- error: nullability constraint
s:create_index('sk', {parts = {4, 'unsigned'}}) -- error: field type
s:create_index('sk', {parts = {4, 'integer', 5, 'string'}}) -- error: field missing

i1 = s:create_index('i1', {parts = {2, 'string'}, unique = false})
i2 = s:create_index('i2', {parts = {{3, 'string', is_nullable = true}}})
i3 = s:create_index('i3', {parts = {4, 'integer'}})

i1:select{}
i2:select{}
i3:select{}

i1:alter{unique = true} -- error: unique contraint
i2:alter{parts = {3, 'string'}} -- error: nullability contraint
i3:alter{parts = {4, 'unsigned'}} -- error: field type
i3:alter{parts = {4, 'integer', 5, 'string'}} -- error: field missing

i3:alter{parts = {2, 'string', 4, 'integer'}} -- ok
i3:select{}

--
-- gh-4350: crash while trying to drop a multi-index space created
-- transactionally after recovery.
--
inspector:cmd("setopt delimiter ';'")
box.begin()
s = box.schema.space.create('test_crash', {engine = engine})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {2, 'unsigned'}})
box.commit();
inspector:cmd("setopt delimiter ''");

-- Check that recovery works.
inspector:cmd("restart server default")
test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

s = box.space.test
s.index.i1:select{}
s.index.i2:select{}
s.index.i3:select{}

-- gh-4350: see above.
box.space.test_crash:drop()

--
-- gh-3903: index build doesn't work after recovery.
--
s.index.i1:drop()
_ = s:create_index('i1', {parts = {2, 'string'}, unique = false})
s.index.i1:select{}

box.snapshot()

s:drop()

-- test ddl operation within begin/commit/rollback
-- acquire free space id
space = box.schema.space.create('ddl_test', {engine = engine})
id = space.id
space:drop()

inspector:cmd("setopt delimiter ';'")
box.begin()
s = box.schema.space.create('ddl_test', {engine = engine, id = id})
box.rollback();

box.begin()
s = box.schema.space.create('ddl_test', {engine = engine, id = id})
box.commit();

box.begin()
s:create_index('pk')
box.rollback();

box.begin()
s:create_index('pk')
box.commit();

s:replace({1});
s:replace({2});
s:replace({3});

box.begin()
s:truncate()
box.commit();
s:select{};

box.begin()
box.schema.user.grant('guest', 'write', 'space', 'ddl_test')
box.rollback();

box.begin()
box.schema.user.grant('guest', 'write', 'space', 'ddl_test')
box.commit();

box.begin()
box.schema.user.revoke('guest', 'write', 'space', 'ddl_test')
box.rollback();

box.begin()
box.schema.user.revoke('guest', 'write', 'space', 'ddl_test')
box.commit();

box.begin()
s.index.pk:drop()
s:drop()
box.commit();

--
-- Only the first statement in a transaction is allowed to be
-- a yielding DDL statement (index build, space format check).
--
s = box.schema.space.create('test', {engine = engine});
_ = s:create_index('pk');
s:insert{1, 1};

-- ok
box.begin()
s:create_index('sk', {parts = {2, 'unsigned'}})
box.commit();
s.index.sk:drop();

-- ok
box.begin()
s:format({{'a', 'unsigned'}, {'b', 'unsigned'}})
box.commit();
s:format({});

-- error
box.begin()
s.index.pk:alter{sequence = true}
s:create_index('sk', {parts = {2, 'unsigned'}});
box.rollback();

-- error
box.begin()
s.index.pk:alter{sequence = true}
s:format({{'a', 'unsigned'}, {'b', 'unsigned'}});
box.rollback();

s:drop();

--
-- Check that all modifications done to the space during index build
-- are reflected in the new index.
--
math.randomseed(os.time())

s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')

inspector:cmd("setopt delimiter ';'")

box.begin()
for i = 1, 1000 do
    if (i % 100 == 0) then
        box.commit()
        box.begin()
    end
    if i % 300 == 0 then
        box.snapshot()
    end
    box.space.test:replace{i, i, i}
end
box.commit();

last_val = 1000;

function gen_load()
    local s = box.space.test
    for i = 1, 200 do
        local op = math.random(4)
        local key = math.random(1000)
        local val1 = math.random(1000)
        local val2 = last_val + 1
        last_val = val2
        if op == 1 then
            pcall(s.insert, s, {key, val1, val2})
        elseif op == 2 then
            pcall(s.replace, s, {key, val1, val2})
        elseif op == 3 then
            pcall(s.delete, s, {key})
        elseif op == 4 then
            pcall(s.upsert, s, {key, val1, val2}, {{'=', 2, val1}, {'=', 3, val2}})
        end
    end
end;

function check_equal(check, pk, k)
    if pk ~= k then
        require('log').error("Error on fiber check: failed '" .. check .. 
	                     "' check on equal pk " .. pk .. " and k = " .. k)
        return false
    end
    return true
end;

function check_fiber()
    _ = fiber.create(function() gen_load() ch:put(true) end)
    _ = box.space.test:create_index('sk', {unique = false, parts = {2, 'unsigned'}})

    ch:get(10)

    local index = box.space.test.index
    if not check_equal("1st step secondary keys", index.pk:count(), index.sk:count()) then
        return false
    end

    _ = fiber.create(function() gen_load() ch:put(true) end)
    _ = box.space.test:create_index('tk', {unique = true, parts = {3, 'unsigned'}})

    ch:get(10)

    index = box.space.test.index
    if not check_equal("2nd step secondary keys", index.pk:count(), index.sk:count()) or
            not check_equal("2nd step third keys", index.pk:count(), index.tk:count()) then
        return false
    end
    return true
end;

inspector:cmd("setopt delimiter ''");

fiber = require('fiber')
ch = fiber.channel(1)
check_fiber()

inspector:cmd("restart server default")
inspector = require('test_run').new()

inspector:cmd("setopt delimiter ';'")

function check_equal(check, pk, k)
    if pk ~= k then
        require('log').error("Error on server restart check: failed '" .. check ..
                             "' check on equal pk " .. pk .. " and k = " .. k)
        return false
    end
    return true
end;

function check_server_restart()
    local index = box.space.test.index
    if not check_equal("1rd step secondary keys", index.pk:count(), index.sk:count()) or
            not check_equal("1rd step third keys", index.pk:count(), index.tk:count()) then
        return false
    end
    box.snapshot()
    index = box.space.test.index
    if not check_equal("2th step secondary keys", index.pk:count(), index.sk:count()) or
            not check_equal("2th step third keys", index.pk:count(), index.tk:count()) then
        return false
    end
    return true
end;

inspector:cmd("setopt delimiter ''");

check_server_restart()

box.space.test:drop()
