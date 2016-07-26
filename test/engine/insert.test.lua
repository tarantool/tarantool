test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- insert (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
for key = 1, 100 do space:insert({tostring(key)}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
space:insert({tostring(7)})
space:drop()


-- insert (num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
for key = 1, 100 do space:insert({key}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
space:insert({7})
space:drop()


-- insert multi-part (num, num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'num'} })
for key = 1, 100 do space:insert({key, key}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
space:insert({7, 7})
space:drop()

-- insert with tuple.new
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
for key = 1, 100 do space:insert({tostring(key)}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
space:insert(box.tuple.new{tostring(7)})
space:drop()

-- insert in space with multiple indices
space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'number', 2, 'scalar'}})
index2 = space:create_index('secondary', { type = 'tree', parts = {3, 'num', 1, 'number'}})
index3 = space:create_index('third', { type = 'tree', parts = {2, 'scalar', 4, 'str'}, unique = false})
space:insert({50, 'fere', 3, 'rgrtht'})
space:insert({-14.645, true, 562, 'jknew'})
space:insert({533, 1293.352, 2132, 'hyorj'})
space:insert({4824, 1293.352, 684, 'hyorj'})
index1:select{}
index2:select{}
index3:select{}
space:drop()

space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'num'} })
index3 = space:create_index('third', { type = 'tree', parts = {3, 'num'}, unique = false })
space:insert({1, 1, 1})
space:insert({2, 2, 2})
space:insert({3, 3, 3})
space:select{}
space:insert({1, 2, 3})
index1:select{}
index2:select{}
index3:select{}
space:drop()

