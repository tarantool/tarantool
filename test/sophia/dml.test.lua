
sophia_rmdir()

-- space create/drop

space = box.schema.create_space('test', { id = 100, engine = 'sophia' })
sophia_printdir()
space:drop()
sophia_printdir()

-- index create/drop

space = box.schema.create_space('test', { id = 101, engine = 'sophia' })
primary = space:create_index('primary')
sophia_printdir()
space:drop()
sophia_printdir()

-- index create/drop alter

space = box.schema.create_space('test', { id = 102, engine = 'sophia' })
primary = space:create_index('primary')
sophia_printdir()
_index = box.space[box.schema.INDEX_ID]
_index:delete{102, 0}
sophia_printdir()
space:drop()

-- index create/drop tree string

space = box.schema.create_space('test', { id = 103, engine = 'sophia' })
primary = space:create_index('primary', {type = 'tree', parts = {1, 'STR'}})
space:insert({'test'})
sophia_printdir()
space:drop()

-- index create/drop tree num

space = box.schema.create_space('test', { id = 104, engine = 'sophia' })
primary = space:create_index('primary', {type = 'tree', parts = {1, 'num'}})
space:insert({13})
sophia_printdir()
space:drop()

-- index create hash 

space = box.schema.create_space('test', { id = 105, engine = 'sophia' })
primary = space:create_index('primary', {type = 'hash'})
space:drop()

-- secondary index create

space = box.schema.create_space('test', { id = 106, engine = 'sophia' })
space:create_index('primary')
space:create_index('secondary')
space:drop()
sophia_printdir()

sophia_rmdir()
