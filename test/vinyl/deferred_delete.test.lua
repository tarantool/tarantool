test_run = require('test_run').new()

--
-- Create a space with secondary indexes and check that REPLACE and
-- DELETE requests do not look up the old tuple in the primary index
-- to generate the DELETE statements for secondary indexes. Instead
-- DELETEs are generated when the primary index is compacted (gh-2129).
-- The optimization should work for both non-unique and unique indexes
-- so mark one of the indexes unique.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {run_count_per_level = 10})
i1 = s:create_index('i1', {run_count_per_level = 10, parts = {2, 'unsigned'}, unique = false})
i2 = s:create_index('i2', {run_count_per_level = 10, parts = {3, 'unsigned'}, unique = true})
-- space.create_index() does a lookup in the primary index
-- so reset the stats before filling up the space.
box.stat.reset()
for i = 1, 10 do s:replace{i, i, i} end
box.snapshot()
for i = 1, 10, 2 do s:delete{i} end
for i = 2, 10, 2 do s:replace{i, i * 10, i * 100} end

-- DELETE/REPLACE does not look up the old tuple in the primary index.
pk:stat().lookup -- 0

-- DELETEs are not written to secondary indexes.
pk:stat().rows -- 10 old REPLACEs + 5 new REPLACEs + 5 DELETEs
i1:stat().rows -- 10 old REPLACEs + 5 new REPLACEs
i2:stat().rows -- ditto

-- Although there are only 5 tuples in the space, we have to look up
-- overwritten tuples in the primary index hence 15 lookups per SELECT
-- in a secondary index.
i1:select()
i1:stat().get.rows -- 5
i1:stat().skip.rows -- 10
pk:stat().lookup -- 15
pk:stat().get.rows -- 5
pk:stat().skip.rows -- 5
i2:select()
i2:stat().get.rows -- 5
i2:stat().skip.rows -- 10
pk:stat().lookup -- 30
pk:stat().get.rows -- 10
pk:stat().skip.rows -- 10

-- Overwritten/deleted tuples are not stored in the cache so calling
-- SELECT for a second time does only 5 lookups.
box.stat.reset()
i1:select()
i1:stat().get.rows -- 5
i1:stat().skip.rows -- 0
pk:stat().lookup -- 5
pk:stat().get.rows -- 5
pk:stat().skip.rows -- 0
i2:select()
i2:stat().get.rows -- 5
i2:stat().skip.rows -- 0
pk:stat().lookup -- 10
pk:stat().get.rows -- 10
pk:stat().skip.rows -- 0

-- Cleanup the cache.
vinyl_cache = box.cfg.vinyl_cache
box.cfg{vinyl_cache = 0}
box.cfg{vinyl_cache = vinyl_cache}

-- Compact the primary index to generate deferred DELETEs.
box.snapshot()
pk:compact()
test_run:wait_cond(function() return pk:stat().disk.compaction.count > 0 end)
pk:stat().rows -- 5 new REPLACEs
i1:stat().rows -- 10 old REPLACE + 5 new REPLACEs + 10 deferred DELETEs
i2:stat().rows -- ditto

-- Deferred DELETEs must be ignored by the read iterator, because
-- they may break the read iterator invariant, so they don't reduce
-- the number of lookups.
box.stat.reset()
i1:select()
i1:stat().get.rows -- 5
i1:stat().skip.rows -- 10
pk:stat().lookup -- 15
pk:stat().get.rows -- 5
pk:stat().skip.rows -- 5
i2:select()
i2:stat().get.rows -- 5
i2:stat().skip.rows -- 10
pk:stat().lookup -- 30
pk:stat().get.rows -- 10
pk:stat().skip.rows -- 10

-- Check that deferred DELETEs are not lost after restart.
test_run:cmd("restart server default")
s = box.space.test
pk = s.index.pk
i1 = s.index.i1
i2 = s.index.i2
i1:stat().rows -- 10 old REPLACEs + 5 new REPLACEs + 10 deferred DELETEs
i2:stat().rows -- ditto

-- Dump deferred DELETEs to disk and compact them.
-- Check that they cleanup garbage statements.
box.snapshot()
i1:compact()
test_run:wait_cond(function() return i1:stat().disk.compaction.count > 0 end)
i2:compact()
test_run:wait_cond(function() return i2:stat().disk.compaction.count > 0 end)
i1:stat().rows -- 5 new REPLACEs
i2:stat().rows -- ditto
box.stat.reset()
i1:select()
i1:stat().get.rows -- 5
pk:stat().lookup -- 5
i2:select()
i2:stat().get.rows -- 5
pk:stat().lookup -- 10

s:drop()

--
-- Check that if the old tuple is found in cache or in memory, then
-- the DELETE for secondary indexes is generated when the statement
-- is committed.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {run_count_per_level = 10})
sk = s:create_index('sk', {run_count_per_level = 10, parts = {2, 'unsigned'}, unique = false})

for i = 1, 10 do s:replace{i, i} end
box.snapshot()
s:count() -- add tuples to the cache

box.stat.reset()
for i = 1, 10, 2 do s:delete{i} end
for i = 2, 10, 2 do s:replace{i, i * 10} end
pk:stat().lookup -- 0
pk:stat().cache.lookup -- 10
pk:stat().cache.get.rows -- 10
pk:stat().memory.iterator.lookup -- 0
sk:stat().rows -- 10 old REPLACEs + 10 DELETEs + 5 new REPLACEs

box.stat.reset()
for i = 1, 10 do s:replace{i, i * 100} end
pk:stat().lookup -- 0
pk:stat().cache.lookup -- 10
pk:stat().cache.get.rows -- 0
pk:stat().memory.iterator.lookup -- 10
pk:stat().memory.iterator.get.rows -- 10
sk:stat().rows -- 15 old REPLACEs + 15 DELETEs + 10 new REPLACEs

box.stat.reset()
for i = 1, 10 do s:delete{i} end
pk:stat().lookup -- 0
pk:stat().cache.lookup -- 10
pk:stat().cache.get.rows -- 0
pk:stat().memory.iterator.lookup -- 10
pk:stat().memory.iterator.get.rows -- 10
sk:stat().rows -- 25 old REPLACEs + 25 DELETEs

sk:select()
pk:stat().lookup -- 0

-- Check that the global tx memory counter doesn't underflow when
-- a deferred DELETE statement is generated on commit (gh-3897).
box.stat.vinyl().memory.tx

box.snapshot()
sk:compact()
test_run:wait_cond(function() return sk:stat().disk.compaction.count > 0 end)
sk:stat().run_count -- 0

s:drop()

--
-- Check that a transaction is aborted if it read a tuple from
-- a secondary index that was overwritten in the primary index.
--
fiber = require('fiber')

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {2, 'unsigned'}, unique = false})
s:replace{1, 1}
box.snapshot()

box.begin()
sk:select{1}
c = fiber.channel(1)
_ = fiber.create(function() s:replace{1, 10} c:put(true) end)
c:get()
sk:select{1}
s:replace{10, 10}
box.commit() -- error

s:drop()

--
-- Check that if a tuple was overwritten in the transaction write set,
-- it won't be committed to secondary indexes.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {run_count_per_level = 10})
sk = s:create_index('sk', {run_count_per_level = 10, parts = {2, 'unsigned'}, unique = false})

-- Write a run big enough to prevent major compaction from kicking in
-- (run_count_per_level is ignored on the last level - see gh-3657).
dummy_rows = 100
for i = 1001, 1000 + dummy_rows do s:replace{i, i} end

for i = 1, 10 do s:replace{i, i} end
box.snapshot()

box.begin()
for i = 1, 10 do s:replace{i, i * 10} end
for i = 1, 10, 2 do s:delete{i} end
for i = 2, 10, 2 do s:replace{i, i * 100} end
box.commit()

sk:select({1000}, {iterator = 'le'})

pk:stat().rows - dummy_rows -- 10 old REPLACEs + 5 DELETEs + 5 new REPLACEs
sk:stat().rows - dummy_rows -- 10 old REPLACEs + 5 new REPLACEs

-- Compact the primary index to generate deferred DELETEs.
box.snapshot()
pk:compact()
test_run:wait_cond(function() return pk:stat().disk.compaction.count > 0 end)

-- Compact the secondary index to cleanup garbage.
box.snapshot()
sk:compact()
test_run:wait_cond(function() return sk:stat().disk.compaction.count > 0 end)

sk:select({1000}, {iterator = 'le'})

pk:stat().rows - dummy_rows -- 5 new REPLACEs
sk:stat().rows - dummy_rows -- ditto

s:drop()

--
-- Check that deferred DELETEs don't overwrite newer statements.
--
vinyl_cache = box.cfg.vinyl_cache
box.cfg{vinyl_cache = 0}

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('primary', {run_count_per_level = 10})
sk = s:create_index('secondary', {unique = false, parts = {2, 'unsigned'}, run_count_per_level = 10})

s:replace{1, 10, 'a'}
box.snapshot()
s:replace{1, 20, 'b'} -- will generate deferred DELETE for [1, 10, 'a']
box.snapshot()
s:replace{1, 10, 'c'} -- must not be overwritten by the deferred DELETE
box.snapshot()

-- Generate deferred DELETEs.
pk:compact()
test_run:wait_cond(function() return pk:stat().disk.compaction.count > 0 end)

sk:select() -- [1, 10, 'c']
box.snapshot()
sk:select() -- ditto
s:drop()

--
-- gh-3693 If a REPLACE doesn't update secondary index key parts,
-- we must not generate a deferred DELETE for it on commit.
-- Moreover, we must skip it as it has no effect.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {2, 'unsigned'}})
s:insert{1, 1, 1}
s:insert{2, 2, 2}
s:replace{2, 2, 3}
pk:stat().rows -- 3: INSERT{1,1,1} + INSERT{2,2,2} + REPLACE{2,2,3}
sk:stat().rows -- 2: INSERT{1,1} + INSERT{2,2}
box.begin()
s:replace{1, 1, 2}
s:delete{1}
box.commit()
pk:stat().rows -- 4: INSERT{1,1,1} + INSERT{2,2,2} + REPLACE{2,2,3} + DELETE{1}
sk:stat().rows -- 3: INSERT{1,1} + INSERT{2,2} + DELETE{1,1}
box.snapshot()
pk:stat().rows -- 1: INSERT{2,2,3}
sk:stat().rows -- 1: INSERT{2,2}
pk:select()
sk:select()
s:drop()

--
-- gh-4248 Deferred DELETE isn't produced on transaction commit.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {2, 'unsigned'}})
s:insert{1, 10}
s:insert{2, 20}
box.begin()
s:replace{1, 11}
s:update(1, {{'=', 2, 12}})
s:update(2, {{'=', 2, 21}})
s:replace{2, 22}
box.commit()
box.snapshot()
pk:stat().rows -- 2: REPLACE{1, 12} + REPLACE{2, 22}
sk:stat().rows -- ditto
pk:select()
sk:select()
s:drop()

box.cfg{vinyl_cache = vinyl_cache}

--
-- Check that on recovery we do not apply deferred DELETEs that
-- have been dumped to disk.
--
test_run:cmd("create server test with script='vinyl/low_quota.lua'")
test_run:cmd("start server test with args='1048576'")
test_run:cmd("switch test")

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {run_count_per_level = 10, run_size_ratio = 2})
sk = s:create_index('sk', {run_count_per_level = 10, run_size_ratio = 2, parts = {2, 'unsigned', 3, 'string'}, unique = false})

-- Write a run big enough to prevent major compaction from kicking in
-- (run_count_per_level is ignored on the last level - see gh-3657).
dummy_rows = 100
pad = string.rep('z', 50 * 1024)
for i = 1, dummy_rows do s:replace{i + 1000, i + 1000, pad} end
box.snapshot()
pk:compact()
sk:compact()
test_run:wait_cond(function() return box.stat.vinyl().scheduler.compaction_queue == 0 end)

pad = string.rep('x', 10 * 1024)
for i = 1, 120 do s:replace{i, i, pad} end
box.snapshot()

pad = string.rep('y', 10 * 1024)
for i = 1, 120 do s:replace{i, i, pad} end
box.snapshot()

sk:stat().rows - dummy_rows -- 120 old REPLACEs + 120 new REPLACEs

box.stat.reset()

-- Compact the primary index to generate deferred DELETEs.
-- Deferred DELETEs won't fit in memory and trigger dump
-- of the secondary index.
pk:compact()
test_run:wait_cond(function() return pk:stat().disk.compaction.count > 0 end)
test_run:wait_cond(function() return sk:stat().disk.dump.count > 0 end)

sk:stat().rows - dummy_rows -- 120 old REPLACEs + 120 new REPLACEs + 120 deferred DELETEs

test_run:cmd("restart server test with args='1048576'")
s = box.space.test
pk = s.index.pk
sk = s.index.sk
dummy_rows = 100

-- Should be 360, the same amount of statements as before restart.
-- If we applied all deferred DELETEs, including the dumped ones,
-- then there would be more.
sk:stat().rows - dummy_rows

s:drop()

test_run:cmd("switch default")
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
