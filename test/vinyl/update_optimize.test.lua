test_run = require('test_run').new()
-- Restart the server to finish all snaphsots from prior tests.
test_run:cmd('restart server default')
fiber = require('fiber')

-- optimize one index

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', { run_count_per_level = 20 })
index2 = space:create_index('secondary', { parts = {5, 'unsigned'}, run_count_per_level = 20 })
box.snapshot()
test_run:cmd("setopt delimiter ';'")
function wait_for_dump(index, old_count)
	while index:info().run_count == old_count do
		fiber.sleep(0)
	end
	return index:info().run_count
end;
test_run:cmd("setopt delimiter ''");

index_run_count = index:info().run_count
index2_run_count = index2:info().run_count
old_stmt_count = box.info.vinyl().performance.dumped_statements
space:insert({1, 2, 3, 4, 5})
space:insert({2, 3, 4, 5, 6})
space:insert({3, 4, 5, 6, 7})
space:insert({4, 5, 6, 7, 8})
box.snapshot()
-- Wait for dump both indexes.
index_run_count = wait_for_dump(index, index_run_count)
index2_run_count = wait_for_dump(index2, index2_run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 8
old_stmt_count = new_stmt_count
-- not optimized updates
space:update({1}, {{'=', 5, 10}}) -- change secondary index field

-- Need a snapshot after each operation to avoid purging some
-- statements in vy_write_iterator during dump.

box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
index2_run_count = wait_for_dump(index2, index2_run_count)
space:update({1}, {{'!', 4, 20}}) -- move range containing index field
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
index2_run_count = wait_for_dump(index2, index2_run_count)
space:update({1}, {{'#', 3, 1}}) -- same
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
index2_run_count = wait_for_dump(index2, index2_run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 9
old_stmt_count = new_stmt_count
space:select{}
index2:select{}

-- optimized updates
space:update({2}, {{'=', 6, 10}}) -- change not indexed field
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
-- Move range that doesn't contain indexed fields.
space:update({2}, {{'!', 7, 20}})
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
space:update({2}, {{'#', 6, 1}}) -- same
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 3
old_stmt_count = new_stmt_count
space:select{}
index2:select{}
space:drop()

-- optimize two indexes

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', { parts = {2, 'unsigned'}, run_count_per_level = 20 } )
index2 = space:create_index('secondary', { parts = {4, 'unsigned', 3, 'unsigned'}, run_count_per_level = 20 })
index3 = space:create_index('third', { parts = {5, 'unsigned'}, run_count_per_level = 20 })
box.snapshot()
index_run_count = index:info().run_count
index2_run_count = index2:info().run_count
index3_run_count = index3:info().run_count
old_stmt_count = box.info.vinyl().performance.dumped_statements
space:insert({1, 2, 3, 4, 5})
space:insert({2, 3, 4, 5, 6})
space:insert({3, 4, 5, 6, 7})
space:insert({4, 5, 6, 7, 8})
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
index2_run_count = wait_for_dump(index2, index2_run_count)
index3_run_count = wait_for_dump(index3, index3_run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 12
old_stmt_count = new_stmt_count

-- not optimizes updates
index:update({2}, {{'+', 1, 10}, {'+', 3, 10}, {'+', 4, 10}, {'+', 5, 10}}) -- change all fields
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
index2_run_count = wait_for_dump(index2, index2_run_count)
index3_run_count = wait_for_dump(index3, index3_run_count)
index:update({2}, {{'!', 3, 20}}) -- move range containing all indexes
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
index2_run_count = wait_for_dump(index2, index2_run_count)
index3_run_count = wait_for_dump(index3, index3_run_count)
index:update({2}, {{'=', 7, 100}, {'+', 5, 10}, {'#', 3, 1}}) -- change two cols but then move range with all indexed fields
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
index2_run_count = wait_for_dump(index2, index2_run_count)
index3_run_count = wait_for_dump(index3, index3_run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 15
old_stmt_count = new_stmt_count
space:select{}
index2:select{}
index3:select{}

-- optimize one 'secondary' index update
index:update({3}, {{'+', 1, 10}, {'-', 5, 2}, {'!', 6, 100}}) -- change only index 'third'
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
index3_run_count = wait_for_dump(index3, index3_run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 3
old_stmt_count = new_stmt_count
-- optimize one 'third' index update
index:update({3}, {{'=', 1, 20}, {'+', 3, 5}, {'=', 4, 30}, {'!', 6, 110}}) -- change only index 'secondary'
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
index2_run_count = wait_for_dump(index3, index2_run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 3
old_stmt_count = new_stmt_count
-- optimize both indexes
index:update({3}, {{'+', 1, 10}, {'#', 6, 1}}) -- don't change any indexed fields
box.snapshot()
index_run_count = wait_for_dump(index, index_run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 1
old_stmt_count = new_stmt_count
space:select{}
index2:select{}
index3:select{}

space:drop()
