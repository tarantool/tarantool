test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- update (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:replace({tostring(key)}) end
for key = 1, 100 do space:update({tostring(key)}, {{'=', 2, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
space:update({tostring(101)}, {{'=', 2, 101}})
space:get({tostring(101)})
space:drop()


-- update (num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
for key = 1, 100 do space:replace({key}) end
for key = 1, 100 do space:update({key}, {{'=', 2, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
space:update({101}, {{'=', 2, 101}})
space:get({101})
space:drop()


-- update multi-part (num, num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
for key = 1, 100 do space:replace({key, key}) end
for key = 1, 100 do space:update({key, key}, {{'=', 3, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
space:update({101, 101}, {{'=', 3, 101}})
space:get({101, 101})
space:drop()

-- update with box.tuple.new
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
for key = 1, 100 do space:replace({key, key}) end
for key = 1, 100 do space:update(box.tuple.new{key, key}, box.tuple.new{{'=', 3, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
space:update({101, 101}, {{'=', 3, 101}})
space:get({101, 101})
space:drop()

-- update multiple indices
space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'string'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'string'}, unique = false })
index3 = space:create_index('third', { type = 'tree', parts = {3, 'scalar', 2, 'string', 1, 'unsigned'}, unique = false })
space:insert({1, 'fwoen', 324})
space:insert({2, 'fwoen', 123})
space:insert({3, 'fwoen', 324})
space:insert({4, '21qn2', 213})
space:insert({5, 'fgb', '231293'})
space:insert({6, 'nrhjrt', -1231.234})
index1:update({1}, {{'+', 3, 10}})
index1:update({1, 'fwoen'}, {{'+', 3, 10}})
index1:update({0, 'fwoen'}, {{'=', 3, 5}})
index2:update({'fwoen'}, {'=', 3, 1000})
index3:update({324, 'fwoen', 3}, {{'-', 3, 100}})
space:drop()

space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'unsigned'} })
index3 = space:create_index('third', { type = 'tree', parts = {3, 'unsigned'}, unique = false })
space:insert({1, 1, 1})
space:insert({2, 2, 2})
space:insert({3, 3, 3})
space:select{}
space:update({1}, {{'=', 2, 2}, {'=', 3, 3}})
index1:select{}
index2:select{}
index3:select{}
space:drop()

-- https://github.com/tarantool/tarantool/issues/1854
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
space:insert({1, 1, 1})
space:insert({2, 2, 2})
space:insert({3, 3, 3})
space:select{}
space:update({2}, {})
space:select{}
space:drop()

--
-- gh-3051 Lost format while tuple update
--
format = {}
format[1] = {name = 'KEY', type = 'unsigned'}
format[2] = {name = 'VAL', type = 'string'}
s = box.schema.space.create('tst_sample', {engine = engine, format = format})
pk = s:create_index('pk')
s:insert({1, 'sss', '3', '4', '5', '6', '7'})
aa = box.space.tst_sample:get(1)
aa.VAL
aa = aa:update({{'=',2,'ssss'}})
aa.VAL
-- invalid update
aa:update({{'=',2, 666}})
-- test transform integrity
aa:transform(-1, 1)
aa:transform(1, 6)
aa = nil

s:upsert({2, 'wwwww'}, {{'=', 2, 'wwwww'}})
box.space.tst_sample:get(2).VAL
s:upsert({2, 'wwwww2'}, {{'=', 2, 'wwwww2'}})
box.space.tst_sample:get(2).VAL
-- invalid upsert
s:upsert({2, 666}, {{'=', 2, 666}})
s:drop()

--
-- gh-4242 Tuple is missing from secondary index after update.
--
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {2, 'unsigned'}})
s:insert{1, 1, 1}
box.begin() s:update(1, {{'=', 2, 2}}) s:update(1, {{'=', 3, 2}}) box.commit()
pk:select()
sk:select()
s:drop()

--
-- gh-1261: tuple update by JSON.
-- At first, test tuple update by field names.
--
format = {}
format[1] = {'field1', 'unsigned'}
format[2] = {'field2', 'array'}
format[3] = {'field3', 'map'}
format[4] = {'field4', 'string'}
format[5] = {'field5', 'any'}
format[6] = {'field6', 'integer'}
format[7] = {'[1]', 'unsigned'}
s = box.schema.create_space('test', {format = format})
pk = s:create_index('pk')
t = s:replace{1, {10, 11, 12}, {a = 20, b = 21, c = 22}, 'abcdefgh', true, -100, 200}
t:update({{'+', 'field1', 1}})
t:update({{'=', 'field2', {13, 14, 15}}})
t:update({{':', 'field4', 3, 3, 'bbccdd'}, {'+', 'field6', 50}, {'!', 7, 300}})
-- Any path is interpreted as a field name first. And only then
-- as JSON.
t:update({{'+', '[1]', 50}})

s:update({1}, {{'=', 'field3', {d = 30, e = 31, f = 32}}})

s:drop()

--
-- gh-3378: allow update absent nullable fields

-- '!'
s = box.schema.create_space('test', {engine = engine})
pk = s:create_index('pk')
s:replace{1, 2}
s:update({1}, {{'!', 4, 0}})
_ = s:delete({1})

-- '#'
s:replace{1, 2}
s:update({1}, {{'#', 4, 1}})
s:drop()

-- Update respects field_count
s = box.schema.create_space('test', {engine = engine, field_count = 2})
pk = s:create_index('pk')
s:replace{1, 2}
s:update({1}, {{'!', 3, 0}})
s:update({1}, {{'=', 3, 0}})
s:drop()

-- '='
s = box.schema.create_space('test', {engine = engine})
pk = s:create_index('pk')
s:replace{1, 2}
s:update({1}, {{'=', 4, 0}})
s:drop()

-- Negative field number, fixed field_count
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'unsigned'}
format[3] = {name = 'field3', type = 'unsigned', is_nullable = true}
format[4] = {name = 'field4', type = 'unsigned', is_nullable = true}
s = box.schema.create_space('test', {engine = engine, format = format, field_count = 4})
pk = s:create_index('pk')
s:replace{1, 2, box.NULL, box.NULL}
s:update({1}, {{'!', -1, 42}})
s:update({1}, {{'=', -1, 128}})
s:drop()

-- Negative field number, no field_count
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'unsigned'}
format[3] = {name = 'field3', type = 'unsigned', is_nullable = true}
format[4] = {name = 'field4', type = 'unsigned', is_nullable = true}
s = box.schema.create_space('test', {engine = engine, format = format})
pk = s:create_index('pk')
s:replace{1, 2}
s:update({1}, {{'!', -1, 42}})
s:update({1}, {{'=', -1, 128}})
s:drop()

-- '#' doesn't trim nulls
s = box.schema.create_space('test', {engine = engine})
pk = s:create_index('pk')
s:replace{1, 2}
s:update({1}, {{'!', 4, 0}})
s:update({1}, {{'#', 4, 1}})
s:update({1}, {{'#', 3, 1}})
s:drop()

-- Maps (fail if don't exist)
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'map'}
s = box.schema.create_space('test', {engine = engine, format = format})
pk = s:create_index('pk')
map = {key1 = 1, key2 = 2}
s:replace{1, map}
s:update({1}, {{'!', 'field42', 0}})
s:update({1}, {{'!', '[3].key1', 1}})
s:update({1}, {{'!', 3, 3}})
s:drop()

-- Arrays (fail if don't exist)
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'array'}
s = box.schema.create_space('test', {engine = engine, format = format})
pk = s:create_index('pk')
arr = {11, 22, {111, 222}}
s:replace{1, arr}
s:update({1}, {{'!', '[2][42]', 0}})
s:update({1}, {{'!', '[2][3][42]', 0}})
s:drop()

-- JSON (fail if don't exists)
format = {}
format[1] = {'field1', 'unsigned'};
format[2] = {'field2', 'map'};
format[3] = {'field3', 'array'};
s = box.schema.create_space('test', {engine = engine, format = format})
pk = s:create_index('pk')
s:replace({1, {key1 = 'value'}, {1, 2}})
-- it's okey, create new {key2, value} pair in map
s:update({1}, {{'!', 'field2.key2', 0}})
-- error: field3[5] was not found in the tuple
s:update({1}, {{'!', 'field3[5]', 0}})
-- error: field4.key1 was not found in the tuple
s:update({1}, {{'!', 'field4.key1', 0}})
s:drop()

--
-- Autofill of nils is baned for nested arrays.
--
s = box.schema.create_space('test', {engine = engine})
pk = s:create_index('pk')
s:insert({1, 2, {11, 22}})
-- When two operations are used for one array, internally it looks very similar
-- to how the root array is represented. Still the ban should work.
op1 = {'=', '[3][1]', 11}
op2 = {'=', '[3][4]', 44}
s:update({1}, {op1, op2})
s:update({1}, {op1})
s:update({1}, {op2})
s:drop()

format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'unsigned', is_nullable = true}
format[3] = {name = 'field3', type = 'unsigned', is_nullable = true}
s = box.schema.create_space('test', {format = format})
_ = s:create_index('pk')
t = s:replace({1})
t:update({{'=', 3, 3}})
t:update({{'=', '[3]', 3}})
t:update({{'=', 'field3', 3}})
s:drop()
