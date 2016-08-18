
-- optimize one index

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
index2 = space:create_index('secondary', { parts = {5, 'unsigned'} })
old_count = box.info.vinyl().performance.write_count
space:insert({1, 2, 3, 4, 5})
space:insert({2, 3, 4, 5, 6})
space:insert({3, 4, 5, 6, 7})
space:insert({4, 5, 6, 7, 8})
new_count = box.info.vinyl().performance.write_count
new_count - old_count == 8
old_count = new_count
-- not optimized updates
space:update({1}, {{'=', 5, 10}}) -- change secondary index field
space:update({1}, {{'!', 4, 20}}) -- move range containing index field
space:update({1}, {{'#', 3, 1}}) -- same
new_count = box.info.vinyl().performance.write_count
new_count - old_count == 6
old_count = new_count
space:select{}
index2:select{}

-- optimized updates
space:update({2}, {{'=', 6, 10}}) -- change not indexed field
space:update({2}, {{'!', 7, 20}}) -- move range that doesn't contain 
space:update({2}, {{'#', 6, 1}}) -- same
new_count = box.info.vinyl().performance.write_count
new_count - old_count == 3
old_count = new_count
space:select{}
index2:select{}
space:drop()

-- optimize two indexes

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', { parts = {2, 'unsigned'} } )
index2 = space:create_index('secondary', { parts = {4, 'unsigned', 3, 'unsigned'} })
index3 = space:create_index('third', { parts = {5, 'unsigned'} })
old_count = box.info.vinyl().performance.write_count
space:insert({1, 2, 3, 4, 5})
space:insert({2, 3, 4, 5, 6})
space:insert({3, 4, 5, 6, 7})
space:insert({4, 5, 6, 7, 8})
new_count = box.info.vinyl().performance.write_count
new_count - old_count == 12
old_count = new_count

-- not optimizes updates
index:update({2}, {{'+', 1, 10}, {'+', 3, 10}, {'+', 4, 10}, {'+', 5, 10}}) -- change all fields
index:update({2}, {{'!', 3, 20}}) -- move range containing all indexes
index:update({2}, {{'=', 7, 100}, {'+', 5, 10}, {'#', 3, 1}}) -- change two cols but then move range with all indexed fields
new_count = box.info.vinyl().performance.write_count
new_count - old_count == 9
old_count = new_count
space:select{}
index2:select{}
index3:select{}

-- optimize one 'secondary' index update
index:update({3}, {{'+', 1, 10}, {'-', 5, 2}, {'!', 6, 100}}) -- change only index 'third'
new_count = box.info.vinyl().performance.write_count
new_count - old_count == 2
old_count = new_count
-- optimize one 'third' index update
index:update({3}, {{'=', 1, 20}, {'+', 3, 5}, {'=', 4, 30}, {'!', 6, 110}}) -- change only index 'secondary'
new_count = box.info.vinyl().performance.write_count
new_count - old_count == 2
old_count = new_count
-- optimize both indexes
index:update({3}, {{'+', 1, 10}, {'#', 6, 1}}) -- don't change any indexed fields
new_count = box.info.vinyl().performance.write_count
new_count - old_count == 1
old_count = new_count
space:select{}
index2:select{}
index3:select{}

space:drop()

