
sophia_rmdir()

-- insert

space = box.schema.create_space('test', { engine = 'sophia', id = 100 })
space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
for key = 1, 132 do space:insert({key}) end
t = {}
for key = 1, 132 do table.insert(t, space:get({key})) end
t

-- replace/get

for key = 1, 132 do space:replace({key, key}) end
t = {}
for key = 1, 132 do table.insert(t, space:get({key})) end
t

-- update/get

for key = 1, 132 do space:update({key}, {{'+', 2, key}}) end
t = {}
for key = 1, 132 do table.insert(t, space:get({key})) end
t

-- delete/get

for key = 1, 132 do space:delete({key}) end
for key = 1, 132 do assert(space:get({key}) == nil) end

-- select

for key = 1, 96 do space:insert({key}) end
index = space.index[0]
index:select({}, {iterator = box.index.ALL})
index:select({}, {iterator = box.index.GE})
index:select(4,  {iterator = box.index.GE})
index:select({}, {iterator = box.index.GT})
index:select(4,  {iterator = box.index.GT})
index:select({}, {iterator = box.index.LE})
index:select(7,  {iterator = box.index.LE})
index:select({}, {iterator = box.index.LT})
index:select(7,  {iterator = box.index.LT})

space:drop()

sophia_rmdir()
