test_run = require('test_run').new()

test_run:cmd("create server test with script='vinyl/low_quota.lua'")
test_run:cmd("start server test with args='1048576'")
test_run:cmd('switch test')

fiber = require 'fiber'

box.cfg{vinyl_timeout=0.01}
box.error.injection.set('ERRINJ_VY_SCHED_TIMEOUT', 0.01)

--
-- Check that a transaction is aborted on timeout if it exceeds
-- quota and the scheduler doesn't manage to free memory.
--
box.error.injection.set('ERRINJ_VY_RUN_WRITE', true)

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')

pad = string.rep('x', 2 * box.cfg.vinyl_memory / 3)
_ = s:auto_increment{pad}
s:count()
box.info.vinyl().quota.used

-- Since the following operation requires more memory than configured
-- and dump is disabled, it should fail with ER_VY_QUOTA_TIMEOUT.
_ = s:auto_increment{pad}
s:count()
box.info.vinyl().quota.used

s:drop()

box.error.injection.set('ERRINJ_VY_RUN_WRITE', false)
fiber.sleep(0.01) -- wait for scheduler to unthrottle

--
-- Check that exceeding quota triggers dump of all spaces.
--
s1 = box.schema.space.create('test1', {engine = 'vinyl'})
_ = s1:create_index('pk')
s2 = box.schema.space.create('test2', {engine = 'vinyl'})
_ = s2:create_index('pk')

_ = s1:auto_increment{}
box.info.vinyl().quota.used

pad = string.rep('x', box.cfg.vinyl_memory)
_ = s2:auto_increment{pad}

while box.info.vinyl().quota.used > 0 do fiber.sleep(0.01) end
box.info.vinyl().quota.used

--
-- Check that exceeding quota doesn't hang the scheduler
-- in case there's nothing to dump.
--
s2:auto_increment{pad}

test_run:cmd('switch default')
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
