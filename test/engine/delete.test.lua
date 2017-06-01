test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- delete (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:replace({tostring(key)}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
for key = 1, 100 do space:delete({tostring(key)}) end
for key = 1, 100 do assert(space:get({tostring(key)}) == nil) end

space:delete({tostring(7)})
space:drop()


-- delete (num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
for key = 1, 100 do space:replace({key}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
for key = 1, 100 do space:delete({key}) end
for key = 1, 100 do assert(space:get({key}) == nil) end
space:delete({7})
space:drop()


-- delete multi-part (num, num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
for key = 1, 100 do space:replace({key, key}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
for key = 1, 100 do space:delete({key, key}) end
for key = 1, 100 do assert(space:get({key, key}) == nil) end
space:delete({7, 7})
space:drop()

-- delete (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:replace({tostring(key)}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
for key = 1, 100 do space:delete(box.tuple.new{tostring(key)}) end
for key = 1, 100 do assert(space:get({tostring(key)}) == nil) end

space:delete(box.tuple.new{tostring(7)})
space:drop()

-- delete with multiple indices
space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'string', 3, 'scalar'}})
index3 = space:create_index('third', { type = 'tree', parts = {1, 'unsigned', 3, 'scalar'}})
space:insert({1, 'abc', 100})
space:insert({3, 'weif', 345})
space:insert({2, 'gbot', '023'})
space:insert({10, 'dflgner', 532.123})
space:insert({0, 'igkkm', 4902})
index1:select{}
index2:select{}
index3:select{}
tmp = index1:delete({1})
tmp = index2:delete({'weif'}) -- must fail
tmp = index2:delete({'weif', 345})
tmp = index2:delete({'weif', 345})
tmp = index3:delete({2, '023'})
index1:select{}
index2:select{}
index3:select{}
space:drop()
