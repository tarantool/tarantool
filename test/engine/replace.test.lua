
-- replace (str)
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
for key = 1, 100 do space:replace({tostring(key)}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
space:replace({tostring(7)})
space:drop()


-- replace (num)
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
for key = 1, 100 do space:replace({key}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
space:replace({7})
space:drop()


-- insert multi-part (num, num)
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'num'} })
for key = 1, 100 do space:replace({key, key}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
space:replace({7, 7})
space:drop()
