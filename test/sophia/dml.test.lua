
-- space create/drop

space = box.schema.space.create('test', { engine = 'sophia' })
sophia_dir()[1]
space:drop()
sophia_schedule()
sophia_dir()[1]

-- index create/drop

space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary')
sophia_dir()[1]
space:drop()
sophia_schedule()
sophia_dir()[1]

-- index create/drop alter

space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary')
sophia_dir()[1]
_index = box.space[box.schema.INDEX_ID]
_index:delete{102, 0}
space:drop()
sophia_schedule()
sophia_dir()[1]

-- index create/drop tree string

space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'tree', parts = {1, 'STR'}})
space:insert({'test'})
space:drop()
sophia_schedule()
sophia_dir()[1]

-- index create/drop tree num

space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'tree', parts = {1, 'num'}})
space:insert({13})
space:drop()
sophia_schedule()
sophia_dir()[1]

-- index create hash 

space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'hash'})
space:drop()
sophia_schedule()
sophia_dir()[1]

-- secondary index create

space = box.schema.space.create('test', { engine = 'sophia' })
index1 = space:create_index('primary')
index2 = space:create_index('secondary')
space:drop()
sophia_schedule()
sophia_dir()[1]

-- index size

space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary')
primary = space.index[0]
primary:len()
space:insert({13})
space:insert({14})
space:insert({15})
primary:len()
space:drop()
sophia_schedule()
