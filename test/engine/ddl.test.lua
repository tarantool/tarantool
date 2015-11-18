
-- space create/drop
space = box.schema.space.create('test', { engine = 'sophia' })
space:drop()


-- space index create/drop
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary')
space:drop()


-- space index create/drop alter
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary')
_index = box.space[box.schema.INDEX_ID]
_index:delete{102, 0}
space:drop()


-- space index create/drop tree string
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'tree', parts = {1, 'STR'}})
space:insert({'test'})
space:drop()


-- space index create/drop tree num
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'tree', parts = {1, 'num'}})
space:insert({13})
space:drop()


-- space index create/drop tree multi-part num
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'tree', parts = {1, 'num', 2, 'num'}})
space:insert({13})
space:drop()


-- space index create/drop tree incorrect key pos
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'tree', parts = {3, 'num'}})
space:drop()


-- space index create/drop tree sparse
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'tree', parts = {1, 'num', 3, 'num'}})
space:drop()


-- space index create hash
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'hash'})
space:drop()


-- space secondary index create
space = box.schema.space.create('test', { engine = 'sophia' })
index1 = space:create_index('primary')
index2 = space:create_index('secondary')
space:drop()


-- space index size
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary')
primary = space.index[0]
primary:len()
space:insert({13})
space:insert({14})
space:insert({15})
primary:len()
space:drop()


-- ensure alter is not supported
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary')
index:alter({parts={1,'NUM'}})
space:drop()
