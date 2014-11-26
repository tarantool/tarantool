
space = box.schema.create_space('test', { engine = 'sophia', id = 100 })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })

-- begin/rollback

box.begin()
for key = 1, 10 do space:insert({key}) end
t = {}
for key = 1, 10 do table.insert(t, space:get({key})) end
t
box.rollback()
t = {}
for key = 1, 10 do assert(space:get({key}) == nil) end
t

-- begin/commit delete

box.begin()
for key = 1, 10 do space:delete({key}) end
box.commit()

-- begin/commit insert

box.begin()
t = {}
for key = 1, 10 do space:insert({key}) end
t = {}
box.commit()
t = {}
for key = 1, 10 do table.insert(t, space:get({key})) end
t

-- cross-engine constraint

space_tmp = box.schema.create_space('test_tmp')
index = space_tmp:create_index('primary', { type = 'tree', parts = {1, 'num'} })
box.begin()
space:insert({123})
space_tmp:insert({123}) -- exception
box.rollback()
space_tmp:drop()

-- cross-space constraint

space_tmp = box.schema.create_space('test_tmp', { engine = 'sophia', id = 101 })
index = space_tmp:create_index('primary', { type = 'tree', parts = {1, 'num'} })
box.begin()
space:insert({123})
space_tmp:insert({123}) -- exception
box.rollback()
space_tmp:drop()

space:drop()

--
