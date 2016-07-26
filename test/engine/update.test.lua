test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- update (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
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
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
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
index = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'num'} })
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
index = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'num'} })
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
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'str'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'str'}, unique = false })
index3 = space:create_index('third', { type = 'tree', parts = {3, 'scalar', 2, 'str', 1, 'num'}, unique = false })
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
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'num'} })
index3 = space:create_index('third', { type = 'tree', parts = {3, 'num'}, unique = false })
space:insert({1, 1, 1})
space:insert({2, 2, 2})
space:insert({3, 3, 3})
space:select{}
space:update({1}, {{'=', 2, 2}, {'=', 3, 3}})
index1:select{}
index2:select{}
index3:select{}
space:drop()

