test_run = require('test_run').new()

test_run:cmd("create server test with script='vinyl/low_quota.lua'")
test_run:cmd("start server test")
test_run:cmd('switch test')

box.cfg{vinyl_timeout=0.01}

box.error.injection.set('ERRINJ_VY_RUN_WRITE', true)

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')

pad = string.rep('x', 2 * box.cfg.vinyl_memory / 3)
_ = s:auto_increment{pad}
s:count()
box.info.vinyl().memory.used

-- Since the following operation requires more memory than configured
-- and dump is disabled, it should fail with ER_VY_QUOTA_TIMEOUT.
_ = s:auto_increment{pad}
s:count()
box.info.vinyl().memory.used

test_run:cmd('switch default')
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
