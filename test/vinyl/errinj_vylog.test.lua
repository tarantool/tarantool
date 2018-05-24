test_run = require('test_run').new()
fiber = require('fiber')

--
-- Check that an error to commit a new run to vylog does not
-- break vinyl permanently.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
_ = s:insert{1, 'x'}

SCHED_TIMEOUT = 0.05
box.error.injection.set('ERRINJ_VY_SCHED_TIMEOUT', SCHED_TIMEOUT)
box.error.injection.set('ERRINJ_VY_LOG_FLUSH', true);

box.snapshot()

box.error.injection.set('ERRINJ_VY_LOG_FLUSH', false);
fiber.sleep(2 * SCHED_TIMEOUT)
box.error.injection.set('ERRINJ_VY_SCHED_TIMEOUT', 0)

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
s2:create_index('pk') -- error

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

test_run:cmd('restart server default')

s1 = box.space.test1
s2 = box.space.test2

_ = s1:insert{444, 'ddd'}
_ = s2:create_index('pk')
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
