
a = box.schema.space.create('test', { engine = 'sophia' })
index = a:create_index('primary', { type = 'tree', parts = {1, 'num'} })

b = box.schema.space.create('test_tmp', { engine = 'sophia' })
index = b:create_index('primary', { type = 'tree', parts = {1, 'num'} })

-- begin/rollback

box.begin()
for key = 1, 10 do a:insert({key}) end
t = {}
for key = 1, 10 do table.insert(t, a:select({key})[1]) end
t
for key = 1, 10 do b:insert({key}) end
t = {}
for key = 1, 10 do table.insert(t, b:select({key})[1]) end
t
box.rollback()

t = {}
for key = 1, 10 do assert(#a:select({key}) == 0) end
t
for key = 1, 10 do assert(#b:select({key}) == 0) end
t

-- begin/commit insert

box.begin()
t = {}
for key = 1, 10 do a:insert({key}) end
t = {}
for key = 1, 10 do b:insert({key}) end
t = {}
box.commit()

t = {}
for key = 1, 10 do table.insert(t, a:select({key})[1]) end
t
t = {}
for key = 1, 10 do table.insert(t, b:select({key})[1]) end
t

-- begin/commit delete

box.begin()
for key = 1, 10 do a:delete({key}) end
for key = 1, 10 do b:delete({key}) end
box.commit()

t = {}
for key = 1, 10 do assert(#a:select({key}) == 0) end
t
for key = 1, 10 do assert(#b:select({key}) == 0) end
t

a:drop()
sophia_schedule()
b:drop()
sophia_schedule()
