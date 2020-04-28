test_run = require('test_run').new()
fiber = require('fiber')

--
-- Check that DDL operations are logged in vylog only after successful
-- WAL write.
--
-- If we logged an index creation in the metadata log before WAL write,
-- WAL failure would result in leaving the index record in vylog forever.
-- Since we use LSN to identify indexes in vylog, retrying index creation
-- would then lead to a duplicate index id in vylog and hence inability
-- to make a snapshot or recover.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
box.error.injection.set('ERRINJ_WAL_IO', true)
_ = s:create_index('pk')
box.error.injection.set('ERRINJ_WAL_IO', false)
_ = s:create_index('pk')
box.snapshot()
s:drop()

--
-- Check that an error to commit a new run to vylog does not
-- break vinyl permanently.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
_ = s:insert{1, 'x'}

box.error.injection.set('ERRINJ_VY_LOG_FLUSH', true);

box.snapshot()

box.error.injection.set('ERRINJ_VY_LOG_FLUSH', false);

_ = s:insert{2, 'y'}

box.snapshot()

_ = s:insert{3, 'z'}

test_run:cmd('restart server default')

s = box.space.test
s:select()
s:drop()

--
-- Check that an index drop/create record we failed to
-- write to vylog is flushed along with the next record.
--
fiber = require 'fiber'

s1 = box.schema.space.create('test1', {engine = 'vinyl'})
s2 = box.schema.space.create('test2', {engine = 'vinyl'})
_ = s2:create_index('pk')
_ = s2:insert{1, 'a'}
box.snapshot()
_ = s2:insert{2, 'b'}

box.error.injection.set('ERRINJ_WAL_DELAY', true)

-- VY_LOG_PREPARE_LSM written, but VY_LOG_CREATE_LSM missing
ch = fiber.channel(1)
_ = fiber.create(function() s1:create_index('pk') ch:put(true) end)
fiber.sleep(0.001)

box.error.injection.set('ERRINJ_VY_LOG_FLUSH', true)
box.error.injection.set('ERRINJ_WAL_DELAY', false)

ch:get()
_ = s1:insert{3, 'c'}

-- VY_LOG_DROP_LSM missing
s2.index.pk:drop()

-- pending records must not be rolled back on error
box.snapshot() -- error

box.error.injection.set('ERRINJ_VY_LOG_FLUSH', false);

_ = s1:insert{4, 'd'}
_ = s2:create_index('pk')
_ = s2:insert{5, 'e'}

test_run:cmd('restart server default')

s1 = box.space.test1
s2 = box.space.test2
s1:select()
s2:select()
s1:drop()
s2:drop()

--
-- Check that if a buffered index drop/create record does not
-- make it to the vylog before restart, it will be replayed on
-- recovery.
--
fiber = require 'fiber'

s1 = box.schema.space.create('test1', {engine = 'vinyl'})
s2 = box.schema.space.create('test2', {engine = 'vinyl'})
_ = s2:create_index('pk')
_ = s2:insert{111, 'aaa'}
box.snapshot()
_ = s2:insert{222, 'bbb'}

box.error.injection.set('ERRINJ_WAL_DELAY', true)

-- VY_LOG_PREPARE_LSM written, but VY_LOG_CREATE_LSM missing
ch = fiber.channel(1)
_ = fiber.create(function() s1:create_index('pk') ch:put(true) end)
fiber.sleep(0.001)

box.error.injection.set('ERRINJ_VY_LOG_FLUSH', true)
box.error.injection.set('ERRINJ_WAL_DELAY', false)

ch:get()
_ = s1:insert{333, 'ccc'}

-- VY_LOG_DROP_LSM missing
s2.index.pk:drop()

-- VY_LOG_PREPARE_LSM missing
_ = s2:create_index('pk')

test_run:cmd('restart server default')

s1 = box.space.test1
s2 = box.space.test2

_ = s1:insert{444, 'ddd'}
_ = s2:insert{555, 'eee'}

s1:select()
s2:select()

box.snapshot()

test_run:cmd('restart server default')

s1 = box.space.test1
s2 = box.space.test2
s1:select()
s2:select()
s1:drop()
s2:drop()

--
-- Check that an index that was prepared, but not committed,
-- is recovered properly.
--
fiber = require('fiber')

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
_ = s:insert{1, 1}

box.error.injection.set('ERRINJ_WAL_DELAY', true)
ch = fiber.channel(1)
_ = fiber.create(function() s:create_index('sk', {parts = {2, 'unsigned'}}) ch:put(true) end)

-- wait for ALTER to stall on WAL after preparing the new index
while s.index.pk:stat().disk.dump.count == 0 do fiber.sleep(0.001) end

box.error.injection.set('ERRINJ_VY_LOG_FLUSH', true);
box.error.injection.set('ERRINJ_WAL_DELAY', false)
ch:get()

test_run:cmd('restart server default')

s = box.space.test
s.index.pk:select()
s.index.sk:select()

s:drop()

--
-- gh-4066: recovery error if an instance is restarted while
-- building an index and there's an index with the same id in
-- the snapshot.
--
fiber = require('fiber')

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {2, 'unsigned'}})
s.index[1] ~= nil
s:replace{1, 2}
box.snapshot()

s.index.sk:drop()

-- Log index creation, but never finish building it due to an error injection.
box.error.injection.set('ERRINJ_VY_READ_PAGE_TIMEOUT', 9000)
_ = fiber.create(function() s:create_index('sk', {parts = {2, 'unsigned'}}) end)
fiber.sleep(0.01)

-- Should ignore the incomplete index on recovery.
test_run:cmd('restart server default')

s = box.space.test
s.index[1] == nil
s:drop()
