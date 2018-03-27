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
-- Check that an index drop/truncate/create record we failed to
-- write to vylog is flushed along with the next record.
--
fiber = require 'fiber'

s1 = box.schema.space.create('test1', {engine = 'vinyl'})
_ = s1:create_index('pk')
_ = s1:insert{1, 'a'}

s2 = box.schema.space.create('test2', {engine = 'vinyl'})
_ = s2:create_index('pk')
_ = s2:insert{2, 'b'}

box.snapshot()

_ = s1:insert{3, 'c'}
_ = s2:insert{4, 'd'}

SCHED_TIMEOUT = 0.01
box.error.injection.set('ERRINJ_VY_SCHED_TIMEOUT', SCHED_TIMEOUT)
box.error.injection.set('ERRINJ_VY_LOG_FLUSH', true);

s1:drop()
s2:truncate()
_ = s2:insert{5, 'e'}

s3 = box.schema.space.create('test3', {engine = 'vinyl'})
_ = s3:create_index('pk')
_ = s3:insert{6, 'f'}

-- pending records must not be rolled back on error
box.snapshot()

box.error.injection.set('ERRINJ_VY_LOG_FLUSH', false);
fiber.sleep(2 * SCHED_TIMEOUT) -- wait for scheduler to unthrottle
box.error.injection.set('ERRINJ_VY_SCHED_TIMEOUT', 0)

box.snapshot()

_ = s2:insert{7, 'g'}
_ = s3:insert{8, 'h'}

test_run:cmd('restart server default')

s1 = box.space.test1
s1 == nil

s2 = box.space.test2
s2:select()
s2:drop()

s3 = box.space.test3
s3:select()
s3:drop()

--
-- Check that if a buffered index drop/truncate/create record
-- does not make it to the vylog before restart, it will be
-- replayed on recovery.
--

s1 = box.schema.space.create('test1', {engine = 'vinyl'})
_ = s1:create_index('pk')
_ = s1:insert{111, 'aaa'}

s2 = box.schema.space.create('test2', {engine = 'vinyl'})
_ = s2:create_index('pk')
_ = s2:insert{222, 'bbb'}

box.snapshot()

_ = s1:insert{333, 'ccc'}
_ = s2:insert{444, 'ddd'}

box.error.injection.set('ERRINJ_VY_LOG_FLUSH', true);

s1:drop()
s2:truncate()
_ = s2:insert{555, 'eee'}

s3 = box.schema.space.create('test3', {engine = 'vinyl'})
_ = s3:create_index('pk')
_ = s3:insert{666, 'fff'}

-- gh-2532: replaying create/drop from xlog crashes tarantool
test_run:cmd("setopt delimiter ';'")
for i = 1, 10 do
    s = box.schema.space.create('test', {engine = 'vinyl'})
    s:create_index('primary')
    s:create_index('secondary', {unique = false, parts = {2, 'string'}})
    s:insert{i, 'test' .. i}
    s:truncate()
    s:drop()
end
test_run:cmd("setopt delimiter ''");

test_run:cmd('restart server default')

s1 = box.space.test1
s1 == nil

s2 = box.space.test2
s2:select()
s2:drop()

s3 = box.space.test3
s3:select()
s3:drop()
