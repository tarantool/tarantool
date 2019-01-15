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
i:stat().disk.compaction.queue.bytes == box.stat.vinyl().disk.compaction.queue
errinj.set('ERRINJ_VY_COMPACTION_DELAY', true)
dump()
dump()
i:stat().disk.compaction.queue -- 30 statements
i:stat().disk.compaction.queue.bytes == box.stat.vinyl().disk.compaction.queue
dump()
i:stat().disk.compaction.queue -- 40 statements
i:stat().disk.compaction.queue.bytes == box.stat.vinyl().disk.compaction.queue
dump()
i:stat().disk.compaction.queue -- 50 statements
i:stat().disk.compaction.queue.bytes == box.stat.vinyl().disk.compaction.queue
box.stat.reset() -- doesn't affect queue size
i:stat().disk.compaction.queue -- 50 statements
i:stat().disk.compaction.queue.bytes == box.stat.vinyl().disk.compaction.queue
errinj.set('ERRINJ_VY_COMPACTION_DELAY', false)
while i:stat().disk.compaction.count < 2 do fiber.sleep(0.01) end
i:stat().disk.compaction.queue -- none
s:drop()

test_run:cmd("clear filter")
test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
