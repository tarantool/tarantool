test_run = require('test_run').new()

-- Since we store LSNs in data files, the data size may differ
-- from run to run. Deploy a new server to make sure it will be
-- the same so that we can check it.
test_run:cmd('create server test with script = "vinyl/stat.lua"')
test_run:cmd('start server test')
test_run:cmd('switch test')

-- Compressed data size depends on the zstd version so let's
-- filter it out.
test_run:cmd("push filter 'bytes_compressed: .*' to 'bytes_compressed: <bytes_compressed>'")

fiber = require('fiber')
errinj = box.error.injection

--
-- Check disk.compaction.queue stat.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
i = s:create_index('pk', {run_count_per_level = 2})
function dump() for i = 1, 10 do s:replace{i} end box.snapshot() end
dump()
i:stat().disk.compaction.queue -- none
i:stat().disk.compaction.queue.bytes == box.stat.vinyl().scheduler.compaction_queue
errinj.set('ERRINJ_VY_COMPACTION_DELAY', true)
dump()
dump()
i:stat().disk.compaction.queue -- 30 statements
i:stat().disk.compaction.queue.bytes == box.stat.vinyl().scheduler.compaction_queue
dump()
i:stat().disk.compaction.queue -- 40 statements
i:stat().disk.compaction.queue.bytes == box.stat.vinyl().scheduler.compaction_queue
dump()
i:stat().disk.compaction.queue -- 50 statements
i:stat().disk.compaction.queue.bytes == box.stat.vinyl().scheduler.compaction_queue
box.stat.reset() -- doesn't affect queue size
i:stat().disk.compaction.queue -- 50 statements
i:stat().disk.compaction.queue.bytes == box.stat.vinyl().scheduler.compaction_queue
errinj.set('ERRINJ_VY_COMPACTION_DELAY', false)
while i:stat().disk.compaction.count < 2 do fiber.sleep(0.01) end
i:stat().disk.compaction.queue -- none
s:drop()

--
-- Check task statistics.
--
box.stat.reset()
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
errinj.set('ERRINJ_VY_RUN_WRITE_DELAY', true)
s:replace{1}
c = fiber.channel(1)
_ = fiber.create(function() box.snapshot() c:put(true) end)
fiber.sleep(0.01)
stat = box.stat.vinyl().scheduler
stat.tasks_inprogress > 0
stat.tasks_completed == 0
stat.tasks_failed == 0
box.stat.reset() -- doesn't affect tasks_inprogress
box.stat.vinyl().scheduler.tasks_inprogress > 0
errinj.set('ERRINJ_VY_RUN_WRITE_DELAY', false)
c:get()
stat = box.stat.vinyl().scheduler
stat.tasks_inprogress == 0
stat.tasks_completed == 1
stat.tasks_failed == 0
errinj.set('ERRINJ_VY_RUN_WRITE', true)
errinj.set('ERRINJ_VY_SCHED_TIMEOUT', 0.01)
s:replace{2}
box.snapshot()
stat = box.stat.vinyl().scheduler
stat.tasks_inprogress == 0
stat.tasks_completed == 1
stat.tasks_failed > 0
errinj.set('ERRINJ_VY_RUN_WRITE', false)
errinj.set('ERRINJ_VY_SCHED_TIMEOUT', 0)
fiber.sleep(0.01)
s:drop()

test_run:cmd("clear filter")
test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
