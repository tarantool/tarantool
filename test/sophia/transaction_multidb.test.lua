
a = box.schema.create_space('test', { engine = 'sophia', id = 100 })
index = a:create_index('primary', { type = 'tree', parts = {1, 'num'} })

b = box.schema.create_space('test_tmp', { engine = 'sophia', id = 101 })
index = b:create_index('primary', { type = 'tree', parts = {1, 'num'} })

-- begin/rollback

box.begin()
for key = 1, 10 do a:insert({key}) end
t = {}
for key = 1, 10 do table.insert(t, a:get({key})) end
t
for key = 1, 10 do b:insert({key}) end
t = {}
for key = 1, 10 do table.insert(t, b:get({key})) end
t
box.rollback()

t = {}
for key = 1, 10 do assert(a:get({key}) == nil) end
t
for key = 1, 10 do assert(b:get({key}) == nil) end
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
for key = 1, 10 do table.insert(t, a:get({key})) end
t
t = {}
for key = 1, 10 do table.insert(t, b:get({key})) end
t

-- begin/commit delete

box.begin()
for key = 1, 10 do a:delete({key}) end
for key = 1, 10 do b:delete({key}) end
box.commit()

t = {}
for key = 1, 10 do assert(a:get({key}) == nil) end
t
for key = 1, 10 do assert(b:get({key}) == nil) end
t

a:drop()
b:drop()
--
