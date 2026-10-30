-- gh-1467: invalid iterator type

space = box.schema.space.create('test')
index = space:create_index('primary', { type = 'hash' })
space:select({1}, {iterator = 'BITS_ALL_SET' } )
space:drop()
