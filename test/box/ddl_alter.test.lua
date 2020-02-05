-- index should not crash after alter
space = box.schema.space.create('test_swap')
index = space:create_index('pk')
space:replace({1, 2, 3})
index:rename('primary')
index2 = space:create_index('sec')
space:replace({2, 3, 1})
space:select()
space:drop()
