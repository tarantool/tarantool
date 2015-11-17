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
