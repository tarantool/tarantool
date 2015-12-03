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
_ = space:update({tostring(101)}, {{'=', 2, 101}})
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
_ = space:update({101}, {{'=', 2, 101}})
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
_ = space:update({101, 101}, {{'=', 3, 101}})
space:get({101, 101})
space:drop()
