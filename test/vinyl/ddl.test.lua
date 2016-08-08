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
index:alter({parts={1,'unsigned'}})
space:drop()

-- new indexes on not empty space are unsupported
space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1})
-- fail because of wrong tuple format {1}, but need {1, ...}
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
#box.space._index:select({space.id})
space:drop()

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1, 2})
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
#box.space._index:select({space.id})
space:drop()

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1, 2})
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
#box.space._index:select({space.id})
space:delete({1})
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
#box.space._index:select({space.id})
space:insert({1, 2})
index:select{}
index2:select{}
space:drop()
