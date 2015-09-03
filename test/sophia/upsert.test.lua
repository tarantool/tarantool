
-- upsert (str)
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
for key = 1, 100 do space:upsert({tostring(key)}, {{'+', 2, 1}}, {tostring(key), 0}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
for key = 1, 100 do space:upsert({tostring(key)}, {{'+', 2, 10}}, {tostring(key), 0}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
for key = 1, 100 do space:delete({tostring(key)}) end
for key = 1, 100 do space:upsert({tostring(key)}, {{'+', 2, 1}, {'=', 3, key}}, {tostring(key), 0}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
space:drop()


-- upsert (num)
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
for key = 1, 100 do space:upsert({key}, {{'+', 2, 1}}, {key, 0}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
for key = 1, 100 do space:upsert({key}, {{'+', 2, 10}}, {key, 0}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
for key = 1, 100 do space:delete({key}) end
for key = 1, 100 do space:upsert({key}, {{'+', 2, 1}, {'=', 3, key}}, {key, 0}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
space:drop()


-- upsert multi-part (num, num)
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'num'} })
for key = 1, 100 do space:upsert({key, key}, {{'+', 3, 1}}, {key, key, 0}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
for key = 1, 100 do space:upsert({key, key}, {{'+', 3, 10}}, {key, key, 0}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
for key = 1, 100 do space:delete({key, key}) end
for key = 1, 100 do space:upsert({key, key}, {{'+', 3, 1}, {'=', 4, key}}, {key, key, 0}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
space:drop()


-- upsert default tuple constraint
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'num'} })
space:upsert({0, 0}, {{'+', 3, 1}}, {0, 'key', 0})
space:drop()


-- upsert primary key modify (skipped)
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
space:upsert({0}, {{'+', 1, 1}, {'+', 2, 1}}, {0, 0})
space:get({0})
space:drop()
