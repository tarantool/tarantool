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
