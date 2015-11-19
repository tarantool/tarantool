
test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- upsert (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
for key = 1, 100 do space:upsert({tostring(key), 0}, {{'+', 2, 1}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
for key = 1, 100 do space:upsert({tostring(key), 0}, {{'+', 2, 10}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
for key = 1, 100 do space:delete({tostring(key)}) end
for key = 1, 100 do space:upsert({tostring(key), 0}, {{'+', 2, 1}, {'=', 3, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
space:drop()


-- upsert (num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
for key = 1, 100 do space:upsert({key, 0}, {{'+', 2, 1}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
for key = 1, 100 do space:upsert({key, 0}, {{'+', 2, 10}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
for key = 1, 100 do space:delete({key}) end
for key = 1, 100 do space:upsert({key, 0}, {{'+', 2, 1}, {'=', 3, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
space:drop()


-- upsert multi-part (num, num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'num'} })
for key = 1, 100 do space:upsert({key, key, 0}, {{'+', 3, 1}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
for key = 1, 100 do space:upsert({key, key, 0}, {{'+', 3, 10}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
for key = 1, 100 do space:delete({key, key}) end
for key = 1, 100 do space:upsert({key, key, 0}, {{'+', 3, 1}, {'=', 4, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
space:drop()


-- upsert default tuple constraint
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'num'} })
space:upsert({0, 'key', 0}, {{'+', 3, 1}})
space:drop()


-- upsert primary key modify (skipped)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
space:upsert({0, 0}, {{'+', 1, 1}, {'+', 2, 1}})
space:get({0})
space:drop()
