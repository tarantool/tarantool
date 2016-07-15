-- space secondary index create
space = box.schema.space.create('test', { engine = 'vinyl' })
index1 = space:create_index('primary')
index2 = space:create_index('secondary')
space:drop()

-- space index create hash
space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', {type = 'hash'})
space:drop()

-- ensure alter is not supported
space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
index:alter({parts={1,'NUM'}})
space:drop()
