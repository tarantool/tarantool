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
i = s:create_index('pk')
errinj.set('ERRINJ_VY_RUN_WRITE_DELAY', true)
s:replace{1}
_ = fiber.create(function() box.snapshot() end)
test_run:wait_cond(function() return box.stat.vinyl().scheduler.tasks_inprogress > 0 end)
stat = box.stat.vinyl().scheduler
stat.tasks_inprogress > 0
stat.tasks_completed == 0
stat.tasks_failed == 0
box.stat.reset() -- doesn't affect tasks_inprogress
box.stat.vinyl().scheduler.tasks_inprogress > 0
errinj.set('ERRINJ_VY_RUN_WRITE_DELAY', false)
test_run:wait_cond(function() return box.stat.vinyl().scheduler.tasks_completed > 0 end)
test_run:wait_cond(function() return not box.info.gc().checkpoint_is_in_progress end)
stat = box.stat.vinyl().scheduler
stat.tasks_inprogress == 0
stat.tasks_completed == 1
stat.tasks_failed == 0
errinj.set('ERRINJ_VY_RUN_WRITE', true)
s:replace{2}
box.snapshot()
stat = box.stat.vinyl().scheduler
stat.tasks_completed == 1
stat.tasks_failed > 0
errinj.set('ERRINJ_VY_RUN_WRITE', false)
test_run:wait_cond(function() return box.stat.vinyl().scheduler.tasks_completed > 1 end)
box.snapshot()
i:compact()
test_run:wait_cond(function() return i:stat().disk.compaction.count > 0 end)
stat = box.stat.vinyl().scheduler
stat.tasks_inprogress == 0
stat.tasks_completed == 3
s:drop()

--
-- Check dump/compaction time accounting.
--
box.stat.reset()
s = box.schema.space.create('test', {engine = 'vinyl'})
i = s:create_index('pk')
i:stat().disk.dump.time == 0
i:stat().disk.compaction.time == 0
box.stat.vinyl().scheduler.dump_time == 0
box.stat.vinyl().scheduler.compaction_time == 0

for i = 1, 100 do s:replace{i} end
errinj.set('ERRINJ_VY_RUN_WRITE_DELAY', true)
start_time = fiber.time()
_ = fiber.create(function() box.snapshot() end)
test_run:wait_cond(function() return box.stat.vinyl().scheduler.tasks_inprogress > 0 end)
fiber.sleep(0.1)
errinj.set('ERRINJ_VY_RUN_WRITE_DELAY', false)
test_run:wait_cond(function() return box.stat.vinyl().scheduler.tasks_completed > 0 end)
test_run:wait_cond(function() return not box.info.gc().checkpoint_is_in_progress end)
i:stat().disk.dump.time >= 0.1
i:stat().disk.dump.time <= fiber.time() - start_time
i:stat().disk.compaction.time == 0
box.stat.vinyl().scheduler.dump_time == i:stat().disk.dump.time
box.stat.vinyl().scheduler.compaction_time == i:stat().disk.compaction.time

for i = 1, 100, 10 do s:replace{i} end
box.snapshot()
errinj.set('ERRINJ_VY_RUN_WRITE_DELAY', true)
start_time = fiber.time()
i:compact()
test_run:wait_cond(function() return box.stat.vinyl().scheduler.tasks_inprogress > 0 end)
fiber.sleep(0.1)
errinj.set('ERRINJ_VY_RUN_WRITE_DELAY', false)
test_run:wait_cond(function() return i:stat().disk.compaction.time > 0 end)
i:stat().disk.compaction.time >= 0.1
i:stat().disk.compaction.time <= fiber.time() - start_time
box.stat.vinyl().scheduler.compaction_time == i:stat().disk.compaction.time

box.stat.reset()
i:stat().disk.dump.time == 0
i:stat().disk.compaction.time == 0
box.stat.vinyl().scheduler.dump_time == 0
box.stat.vinyl().scheduler.compaction_time == 0

s:drop()

test_run:cmd("clear filter")
test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
test_run:cmd('delete server test')

test_run:cmd("create server test with script='vinyl/low_quota.lua'")
test_run:cmd("start server test with args='1048576'")
test_run:cmd('switch test')

fiber = require('fiber')
errinj = box.error.injection

--
-- Check regulator.blocked_writers stat.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
box.stat.vinyl().regulator.blocked_writers == 0

errinj.set('ERRINJ_VY_RUN_WRITE_DELAY', true)
pad = string.rep('x', box.cfg.vinyl_memory * 9 / 10)
_ = s:insert{0, pad}
pad = string.rep('x', box.cfg.vinyl_memory * 2 / 10)
for i = 1, 5 do fiber.create(function() s:insert{i, pad} end) end
box.stat.vinyl().regulator.blocked_writers == 5

errinj.set('ERRINJ_VY_RUN_WRITE_DELAY', false)
test_run:wait_cond(function()                                               \
    return box.stat.vinyl().regulator.blocked_writers == 0                  \
end)

s:drop()

test_run:cmd('switch default')
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
test_run:cmd('delete server test')
