test_run = require('test_run').new()

errinj = box.error.injection

-- Make each checkpoint trigger garbage collection.
default_checkpoint_count = box.cfg.checkpoint_count
box.cfg{checkpoint_count = 1}

-- Temporarily block compaction execution.
errinj.set('ERRINJ_VY_COMPACTION_DELAY', true)

-- Trigger compaction of a space.
s = box.schema.create_space('test', {engine = 'vinyl'})
_ = s:create_index('primary', {parts = {1, 'unsigned'}, run_count_per_level = 1})
s:insert{1, 'some data'}
box.snapshot()
s:replace{1, 'some other data'}
box.snapshot()

-- Wait for compaction to start.
test_run:wait_cond(function() return box.stat.vinyl().scheduler.tasks_inprogress > 0 end)

-- Drop the space and trigger garbage collection.
s:drop()
box.snapshot()

-- Resume compaction and wait for it to finish.
errinj.set('ERRINJ_VY_COMPACTION_DELAY', false)
test_run:wait_cond(function() return box.stat.vinyl().scheduler.tasks_inprogress == 0 end)

-- Bump lsn and rotate vylog - should work fine.
box.space._schema:delete('no_such_key')
box.snapshot()

box.cfg{checkpoint_count = default_checkpoint_count}
