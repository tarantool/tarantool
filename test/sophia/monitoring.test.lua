
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
box.sophia['sophia.version']
space:drop()
