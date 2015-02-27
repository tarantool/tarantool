
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })

-- begin/rollback

box.begin()
for key = 1, 10 do space:insert({key}) end
t = {}
for key = 1, 10 do table.insert(t, space:select({key})[1]) end
t
box.rollback()

t = {}
for key = 1, 10 do assert(#space:select({key}) == 0) end
t

-- begin/commit insert

box.begin()
t = {}
for key = 1, 10 do space:insert({key}) end
t = {}
box.commit()
t = {}
for key = 1, 10 do table.insert(t, space:select({key})[1]) end
t

-- begin/commit delete

box.begin()
for key = 1, 10 do space:delete({key}) end
box.commit()

t = {}
for key = 1, 10 do assert(#space:select({key}) == 0) end
t

space:drop()
--
