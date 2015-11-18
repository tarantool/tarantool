-- space index create/drop tree incorrect key pos
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'tree', parts = {3, 'num'}})
space:drop()

-- space index create/drop tree sparse
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'tree', parts = {1, 'num', 3, 'num'}})
space:drop()

-- space secondary index create
space = box.schema.space.create('test', { engine = 'sophia' })
index1 = space:create_index('primary')
index2 = space:create_index('secondary')
space:drop()

-- space index create hash
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', {type = 'hash'})
space:drop()

-- ensure alter is not supported
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary')
index:alter({parts={1,'NUM'}})
space:drop()
