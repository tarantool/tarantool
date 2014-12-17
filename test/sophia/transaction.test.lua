
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

-- begin/commit insert

box.begin()
t = {}
for key = 1, 10 do space:insert({key}) end
t = {}
box.commit()
t = {}
for key = 1, 10 do table.insert(t, space:get({key})) end
t

-- begin/commit delete

box.begin()
for key = 1, 10 do space:delete({key}) end
box.commit()

t = {}
for key = 1, 10 do assert(space:get({key}) == nil) end
t

space:drop()
--
