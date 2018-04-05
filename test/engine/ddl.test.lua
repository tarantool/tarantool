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
i1:select()
i2:select()
i3:select()

i2:alter{parts = {2, 'integer'}}
i3:alter{parts = {3, 'integer'}}
_ = s:replace{-1, -1, -1}
i1:select()
i2:select()
i3:select()

box.snapshot()
_ = s:replace{-1, -2, -3}
_ = s:replace{-3, -2, -1}
i1:select()
i2:select()
i3:select()

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
s = box.schema.space.create('test', {engine = engine, format = format})
pk = s:create_index('pk')
t = s:replace{1, {2}, 3, '4', 5.5, -6, true, -8, {9, 9}, {val = 10}}

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

-- string -----> any
ok_format_change(4, 'any')
-- string -----> scalar
ok_format_change(4, 'scalar')
-- string --X--> boolean
fail_format_change(4, 'boolean')

-- number -----> any
ok_format_change(5, 'any')
-- number -----> scalar
ok_format_change(5, 'scalar')
-- number --X--> integer
fail_format_change(5, 'integer')

-- integer -----> any
ok_format_change(6, 'any')
-- integer -----> number
ok_format_change(6, 'number')
-- integer -----> scalar
ok_format_change(6, 'scalar')
-- integer --X--> unsigned
fail_format_change(6, 'unsigned')

-- boolean -----> any
ok_format_change(7, 'any')
-- boolean -----> scalar
ok_format_change(7, 'scalar')
-- boolean --X--> string
fail_format_change(7, 'string')

-- scalar -----> any
ok_format_change(8, 'any')
-- scalar --X--> unsigned
fail_format_change(8, 'unsigned')

-- array -----> any
ok_format_change(9, 'any')
-- array --X--> scalar
fail_format_change(9, 'scalar')

-- map -----> any
ok_format_change(10, 'any')
-- map --X--> scalar
fail_format_change(10, 'scalar')

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
s:select()
s:drop()
