test_run = require('test_run').new()
-- Restart the server to finish all snaphsots from prior tests.
test_run:cmd('restart server default')
fiber = require('fiber')

test_run:cmd("push filter 'Invalid VYLOG file: Slice [0-9]+ deleted but not registered'" .. \
             " to 'Invalid VYLOG file: Slice <NUM> deleted but not registered'")

-- optimize one index

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', { run_count_per_level = 20 })
index2 = space:create_index('secondary', { parts = {5, 'unsigned'}, run_count_per_level = 20 })
function dumped_stmt_count() return index:stat().disk.dump.output.rows + index2:stat().disk.dump.output.rows end
box.snapshot()
test_run:cmd("setopt delimiter ';'")
function wait_for_dump(index, old_count)
	while index:stat().disk.dump.count == old_count do
		fiber.sleep(0)
	end
	return index:stat().disk.dump.count
end;
test_run:cmd("setopt delimiter ''");

dump_count = index:stat().disk.dump.count
old_stmt_count = dumped_stmt_count()
space:insert({1, 2, 3, 4, 5})
space:insert({2, 3, 4, 5, 6})
space:insert({3, 4, 5, 6, 7})
space:insert({4, 5, 6, 7, 8})
box.snapshot()
-- Wait for dump both indexes.
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
new_stmt_count - old_stmt_count == 8
old_stmt_count = new_stmt_count
-- not optimized updates
space:update({1}, {{'=', 5, 10}}) -- change secondary index field

-- Need a snapshot after each operation to avoid purging some
-- statements in vy_write_iterator during dump.

box.snapshot()
dump_count = wait_for_dump(index, dump_count)
space:update({1}, {{'!', 4, 20}}) -- move range containing index field
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
space:update({1}, {{'#', 3, 1}}) -- same
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
new_stmt_count - old_stmt_count == 9
old_stmt_count = new_stmt_count
space:select{}
index2:select{}

-- optimized updates
space:update({2}, {{'=', 6, 10}}) -- change not indexed field
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
-- Move range that doesn't contain indexed fields.
space:update({2}, {{'!', 7, 20}})
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
space:update({2}, {{'#', 6, 1}}) -- same
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
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
function dumped_stmt_count() return index:stat().disk.dump.output.rows + index2:stat().disk.dump.output.rows + index3:stat().disk.dump.output.rows end
box.snapshot()
dump_count = index:stat().run_count
old_stmt_count = dumped_stmt_count()
space:insert({1, 2, 3, 4, 5})
space:insert({2, 3, 4, 5, 6})
space:insert({3, 4, 5, 6, 7})
space:insert({4, 5, 6, 7, 8})
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
new_stmt_count - old_stmt_count == 12
old_stmt_count = new_stmt_count

-- not optimizes updates
index:update({2}, {{'+', 1, 10}, {'+', 3, 10}, {'+', 4, 10}, {'+', 5, 10}}) -- change all fields
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
index:update({2}, {{'!', 3, 20}}) -- move range containing all indexes
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
index:update({2}, {{'=', 7, 100}, {'+', 5, 10}, {'#', 3, 1}}) -- change two cols but then move range with all indexed fields
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
new_stmt_count - old_stmt_count == 15
old_stmt_count = new_stmt_count
space:select{}
index2:select{}
index3:select{}

-- optimize one 'secondary' index update
index:update({3}, {{'+', 1, 10}, {'-', 5, 2}, {'!', 6, 100}}) -- change only index 'third'
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
new_stmt_count - old_stmt_count == 3
old_stmt_count = new_stmt_count
-- optimize one 'third' index update
index:update({3}, {{'=', 1, 20}, {'+', 3, 5}, {'=', 4, 30}, {'!', 6, 110}}) -- change only index 'secondary'
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
new_stmt_count - old_stmt_count == 3
old_stmt_count = new_stmt_count
-- optimize both indexes
index:update({3}, {{'+', 1, 10}, {'#', 6, 1}}) -- don't change any indexed fields
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
new_stmt_count - old_stmt_count == 1
old_stmt_count = new_stmt_count
space:select{}
index2:select{}
index3:select{}

--
-- gh-1716: optimize UPDATE with fieldno > 64.
--
-- Create a big tuple.
long_tuple = {}
for i = 1, 70 do long_tuple[i] = i end
_ = space:replace(long_tuple)
box.snapshot()

-- Make update of not indexed field with pos > 64.
dump_count = wait_for_dump(index, dump_count)
old_stmt_count = dumped_stmt_count()
_ = index:update({2}, {{'=', 65, 1000}})
box.snapshot()

-- Check the only primary index to be changed.
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
new_stmt_count - old_stmt_count == 1
old_stmt_count = new_stmt_count
space:get{2}[65]

--
-- Try to optimize update with negative field numbers.
--
index:update({2}, {{'#', -65, 65}})
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
new_stmt_count - old_stmt_count == 1
old_stmt_count = new_stmt_count
index:select{}
index2:select{}
index3:select{}

-- Optimize index2 with negative update op.
space:replace{10, 20, 30, 40, 50}
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
old_stmt_count = dumped_stmt_count()

index:update({20}, {{'=', -1, 500}})
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
new_stmt_count = dumped_stmt_count()
-- 3 = REPLACE in index1 and DELETE + REPLACE in index3.
new_stmt_count - old_stmt_count == 3
old_stmt_count = new_stmt_count
index:select{}
index2:select{}
index3:select{}

-- Check if optimizes update do not skip the entire key during
-- dump.
space:replace{10, 100, 1000, 10000, 100000, 1000000}
index:update({100}, {{'=', 6, 1}})
box.begin()
space:replace{20, 200, 2000, 20000, 200000, 2000000}
index:update({200}, {{'=', 6, 2}})
box.commit()
box.snapshot()
dump_count = wait_for_dump(index, dump_count)
old_stmt_count = dumped_stmt_count()
index:select{}
index2:select{}
index3:select{}

--
-- gh-2980: key uniqueness is not checked if indexed fields
-- are not updated.
--
space:truncate()
space:replace{1, 1, 1, 1, 1}

LOOKUPS_BASE = {0, 0, 0}
test_run:cmd("setopt delimiter ';'")
function lookups()
    local ret = {}
    for i = 1, #LOOKUPS_BASE do
        local info = space.index[i - 1]:stat()
        table.insert(ret, info.lookup - LOOKUPS_BASE[i])
    end
    return ret
end;
test_run:cmd("setopt delimiter ''");
LOOKUPS_BASE = lookups()

-- update of a field that is not indexed
space:update(1, {{'+', 1, 1}})
lookups()

-- update of a field indexed by space.index[1]
space:update(1, {{'+', 3, 1}})
lookups()

-- update of a field indexed by space.index[2]
space:update(1, {{'+', 5, 1}})
lookups()

space:drop()

--
-- gh-3607: phantom tuples in secondary index if UPDATE does not
-- change key fields.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {2, 'unsigned'}, run_count_per_level = 10})

s:insert{1, 10}
-- Some padding to prevent last-level compaction (gh-3657).
for i = 1001, 1010 do s:replace{i, i} end
box.snapshot()

s:update(1, {{'=', 2, 10}})
s:delete(1)
box.snapshot()

-- Should be 12: INSERT{10, 1} and INSERT[1001..1010] in the first run
-- plus DELETE{10, 1} in the second run.
s.index.sk:stat().rows

s:insert{1, 20}
s.index.sk:select()

s:drop()
