test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- replace (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:replace({tostring(key)}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
_ = space:replace({tostring(7)})
space:drop()

-- replace (num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
for key = 1, 100 do space:replace({key}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
_ = space:replace({7})
space:drop()


-- replace multi-part (num, num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
for key = 1, 100 do space:replace({key, key}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
_ = space:replace({7, 7})
space:drop()

-- replace with box.tuple.new
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:replace({tostring(key)}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
_ = space:replace(box.tuple.new{tostring(7)})
space:drop()

-- replace multiple indices
space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'unsigned'} })
space:replace({1, 1})
space:replace({1, 2})
index1:select{}
index2:select{}
space:drop()

space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'unsigned'} })
index3 = space:create_index('third', { type = 'tree', parts = {3, 'unsigned'}, unique = false })
space:insert({1, 1, 1})
space:insert({2, 2, 2})
space:insert({3, 3, 3})
space:select{}
space:replace({1, 2, 3})
index1:select{}
index2:select{}
index3:select{}
space:drop()


