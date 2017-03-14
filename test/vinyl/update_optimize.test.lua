test_run = require('test_run').new()
-- Restart the server to finish all snaphsots from prior tests.
test_run:cmd('restart server default')

-- optimize one index

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
index2 = space:create_index('secondary', { parts = {5, 'unsigned'} })
box.snapshot()
test_run:cmd("setopt delimiter ';'")
function wait_for_dump(old_count)
	while box.info.vinyl().db[space.id..'/0'] == old_count or
	      box.info.vinyl().db[space.id..'/1'] == old_count do
		fiber.sleep(0.01)
	end
	return box.info.vinyl().db[space.id..'/0']
end;
test_run:cmd("setopt delimiter ''");

run_count = box.info.vinyl().db[space.id..'/0']
old_stmt_count = box.info.vinyl().performance.dumped_statements
space:insert({1, 2, 3, 4, 5})
space:insert({2, 3, 4, 5, 6})
space:insert({3, 4, 5, 6, 7})
space:insert({4, 5, 6, 7, 8})
box.snapshot()
run_count = wait_for_dump(run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 8
old_stmt_count = new_stmt_count
-- not optimized updates
space:update({1}, {{'=', 5, 10}}) -- change secondary index field
--
-- Need a snapshot after each operation to avoid purging some
-- statements in vy_write_iterator during dump.
--
box.snapshot()
run_count = wait_for_dump(run_count)
space:update({1}, {{'!', 4, 20}}) -- move range containing index field
box.snapshot()
run_count = wait_for_dump(run_count)
space:update({1}, {{'#', 3, 1}}) -- same
box.snapshot()
run_count = wait_for_dump(run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 9
old_stmt_count = new_stmt_count
space:select{}
index2:select{}

-- optimized updates
space:update({2}, {{'=', 6, 10}}) -- change not indexed field
box.snapshot()
run_count = wait_for_dump(run_count)
space:update({2}, {{'!', 7, 20}}) -- move range that doesn't contain 
box.snapshot()
run_count = wait_for_dump(run_count)
space:update({2}, {{'#', 6, 1}}) -- same
box.snapshot()
run_count = wait_for_dump(run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 3
old_stmt_count = new_stmt_count
space:select{}
index2:select{}
space:drop()

-- optimize two indexes

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', { parts = {2, 'unsigned'} } )
index2 = space:create_index('secondary', { parts = {4, 'unsigned', 3, 'unsigned'} })
index3 = space:create_index('third', { parts = {5, 'unsigned'} })
test_run:cmd("setopt delimiter ';'")
function wait_for_dump(old_count)
	while box.info.vinyl().db[space.id..'/0'] == old_count or
	      box.info.vinyl().db[space.id..'/1'] == old_count or
	      box.info.vinyl().db[space.id..'/2'] == old_count do
		fiber.sleep(0.01)
	end
	return box.info.vinyl().db[space.id..'/0']
end;
test_run:cmd("setopt delimiter ''");
box.snapshot()
old_stmt_count = box.info.vinyl().performance.dumped_statements
space:insert({1, 2, 3, 4, 5})
space:insert({2, 3, 4, 5, 6})
space:insert({3, 4, 5, 6, 7})
space:insert({4, 5, 6, 7, 8})
box.snapshot()
run_count = wait_for_dump(run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 12
old_stmt_count = new_stmt_count

-- not optimizes updates
index:update({2}, {{'+', 1, 10}, {'+', 3, 10}, {'+', 4, 10}, {'+', 5, 10}}) -- change all fields
box.snapshot()
run_count = wait_for_dump(run_count)
index:update({2}, {{'!', 3, 20}}) -- move range containing all indexes
box.snapshot()
run_count = wait_for_dump(run_count)
index:update({2}, {{'=', 7, 100}, {'+', 5, 10}, {'#', 3, 1}}) -- change two cols but then move range with all indexed fields
box.snapshot()
run_count = wait_for_dump(run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 15
old_stmt_count = new_stmt_count
space:select{}
index2:select{}
index3:select{}

-- optimize one 'secondary' index update
index:update({3}, {{'+', 1, 10}, {'-', 5, 2}, {'!', 6, 100}}) -- change only index 'third'
box.snapshot()
run_count = wait_for_dump(run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 3
old_stmt_count = new_stmt_count
-- optimize one 'third' index update
index:update({3}, {{'=', 1, 20}, {'+', 3, 5}, {'=', 4, 30}, {'!', 6, 110}}) -- change only index 'secondary'
box.snapshot()
run_count = wait_for_dump(run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 3
old_stmt_count = new_stmt_count
-- optimize both indexes
index:update({3}, {{'+', 1, 10}, {'#', 6, 1}}) -- don't change any indexed fields
box.snapshot()
run_count = wait_for_dump(run_count)
new_stmt_count = box.info.vinyl().performance.dumped_statements
new_stmt_count - old_stmt_count == 1
old_stmt_count = new_stmt_count
space:select{}
index2:select{}
index3:select{}

space:drop()

