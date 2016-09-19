env = require('test_run')
test_run = env.new()

--
-- Tests on data integrity after dump of memory runs or range
-- compaction.
--
-- The aim is to test vy_write_iterator. There are several combinations
-- of various commands that can occur:
--   1) delete followed by upsert : write iterator should convert
--      upsert to replace (insert)
--   2) upsert followed by delete: the upsert is filtered out,
--      delete can be filtered out or not depending on whether it's
--      compaction (filtered out) or dump (preserved)
--   3) upsert followed by upsert: two upserts are folded together
--      into one
--   4) upsert followed by replace: upsert is replaced
--   5) replace followed by upsert: two commands are folded
--      into a single replace with upsert ops applied
--   6) replace followed by delete:
--      both are eliminated in case of compaction;
--      replace is filtered out if it's dump
--   7) delete followed by replace: delete is filtered out
--   8) replace followed by replace: the first replace is filtered
--      out
--   9) single upsert (for completeness)
--   10) single replace (for completeness)

space = box.schema.space.create('test', { engine = 'vinyl' })
--
--
pk = space:create_index('primary', { page_size = 12 * 1024, range_size = 12 * 1024 })

-- Insert many big tuples and then call snapshot to
-- force dumping and compacting.

big_val = string.rep('1', 2000)

_ = space:insert{1}
_ = space:insert{2, big_val}
_ = space:insert{3, big_val}
_ = space:insert{5, big_val}
_ = space:insert{6, big_val}
_ = space:insert{7, big_val}
_ = space:insert{8, big_val}
_ = space:insert{9, big_val}
_ = space:insert{10, big_val}
_ = space:insert{11, big_val}
space:count()
box.snapshot()

--
-- Create a couple of tiny runs on disk, to increate the "number of runs"
-- heuristic of hte planner and trigger compaction
--

space:insert{12}
box.snapshot()
space:insert{13}
box.snapshot()
#space:select{}

space:drop()

--
-- Create a vinyl index with small page_size parameter, so that
-- big tuples will not fit in a single page.
--

space = box.schema.space.create('test', { engine = 'vinyl' })
pk = space:create_index('primary', { page_size = 256, range_size = 3 * 1024 })
space:insert({1})
box.snapshot()

big_val = string.rep('1', 2000)

_ = space:insert{2, big_val}
_ = space:insert{3, big_val}
_ = space:insert{5, big_val}
_ = space:insert{6, big_val}
_ = space:insert{7, big_val}
_ = space:insert{8, big_val}
_ = space:insert{9, big_val}
_ = space:insert{10, big_val}
_ = space:insert{11, big_val}
-- Increate the number of runs, trigger compaction
space:count()
box.snapshot()
space:insert{12}
box.snapshot()
space:insert{13}
box.snapshot()
#space:select{}

space:drop()

-- Test dumping and compacting a space with more than one index.

space = box.schema.space.create('test', { engine = 'vinyl' })
pk = space:create_index('primary', { page_size = 512, range_size = 1024 * 12 })
index2 = space:create_index('secondary', { parts = {2, 'string'}, page_size = 512, range_size = 1024 * 12 })
for i = 1, 100 do space:insert{i, ''..i} if i % 2 == 0 then box.snapshot() end end
space:delete{1}
space:delete{10}
space:delete{100}
box.snapshot()

index2:delete{'9'}
index2:delete{'99'}
box.snapshot()

space:select{2}

-- Test that not dumped changes are visible.

space:upsert({2, '2'}, {{'=', 3, 22}})
space:select{2}
space:upsert({2, '2'}, {{'!', 3, 222}})
space:select{2}
space:upsert({2, '2'}, {{'!', 3, 2222}})

space:select{2}
box.snapshot()

space:select{2}
space:update({2}, {{'!', 3, 22222}})
box.snapshot()

space:select{2}

space:drop()

space = box.schema.space.create('test', { engine = 'vinyl' })
pk = space:create_index('primary', { page_size = 128, range_size = 1024 })

-- Test that snaphot() inside a transaction doesn't lose data
-- and that upserts are successfully merged.

box.begin()
space:upsert({2}, {{'=', 2, 22}})
space:upsert({2}, {{'!', 2, 222}})
space:upsert({2}, {{'!', 2, 2222}})
space:select{}

box.snapshot()

box.commit()

space:select{}
space:insert({3})

box.snapshot()

space:select{}

--
-- Verify that deletion of tuples with key 2 and 3 is
-- successfully dumped and compacted.
--

box.begin()

space:delete{2}
space:delete{3}

box.commit()

space:upsert({10}, {{'!', 2, 10}})
box.snapshot()
space:select{}

-- Test that deletion is successfully dumped and compacted.

space:delete{10}

space:upsert({10}, {{'!', 2, 10}})
space:upsert({10}, {{'!', 2, 10}})
box.snapshot()
space:select{}
space:delete{10}

space:upsert({10}, {{'!', 2, 10}})
space:delete({10})
box.snapshot()
space:select{}

-- Test that if replace is met then previous upsert is ignored.

space:upsert({10}, {{'!', 2, 10}})
space:replace({10, 100})
box.snapshot()
space:select{}
space:delete{10}

-- Test that dumping and compacting didn't lose single upsert.

space:upsert({100}, {{'!', 2, 100}})
box.snapshot()
space:select{}
space:delete{100}

-- Verify that if upsert goes after replace then they will be merged.
space:replace({200})
space:upsert({200}, {{'!', 2, 200}})
box.snapshot()
space:select{}
space:delete{200}

-- Insert more tuples than can fit in range_size

big_val = string.rep('1', 400)
_ = space:replace({1, big_val})
_ = space:replace({2, big_val})
_ = space:replace({3, big_val})
_ = space:replace({4, big_val})
_ = space:replace({5, big_val})
_ = space:replace({6, big_val})
_ = space:replace({7, big_val})
space:count()
box.snapshot()
space:count()
space:delete({1})
space:delete({2})
space:delete({3})
space:delete({4})
space:delete({5})
space:delete({6})
space:delete({7})
space:select{}
box.snapshot()
space:select{}

-- Test that update successfully merged with replace and other updates
space:insert({1})
space:update({1}, {{'=', 2, 111}})
space:update({1}, {{'!', 2, 11}})
space:update({1}, {{'+', 3, 1}, {'!', 4, 444}})
space:select{}
box.snapshot()
space:select{}
space:delete{1}
box.snapshot()
space:select{}

-- Test upsert after deletion

space:insert({1})
box.snapshot()
space:select{}
space:delete({1})
space:upsert({1}, {{'!', 2, 111}})
space:select{}
box.snapshot()
space:select{}
space:delete({1})

-- Test upsert before deletion

space:insert({1})
box.snapshot()
space:select{}
space:upsert({1}, {{'!', 2, 111}})
space:delete({1})
box.snapshot()
space:select{}

-- Test deletion before replace

space:insert({1})
box.snapshot()
space:select{}
space:delete({1})
space:replace({1, 1})
box.snapshot()
space:select{}
space:delete({1})

-- Test replace before deletion

space:replace({5, 5})
space:delete({5})
box.snapshot()
space:select{}

-- Test many replaces

space:replace{6}
space:replace{6, 6, 6}
space:replace{6, 6, 6, 6}
space:replace{6, 6, 6, 6, 6}
space:replace{6, 6, 6, 6, 6, 6}
space:replace{6, 6, 6, 6, 6, 6, 6}
box.snapshot()
space:select{}
space:delete({6})

space:drop()
