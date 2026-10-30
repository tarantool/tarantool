errinj = box.error.injection
fiber = require('fiber')

-- During this test we verify that if dump process is throttled
-- due to error, DDL operations in window between dumps won't
-- break anything.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')

errinj.set('ERRINJ_VY_RUN_WRITE', true)
errinj.set('ERRINJ_VY_SCHED_TIMEOUT', 0.01)
s:replace{2}
box.snapshot()
errinj.set('ERRINJ_VY_RUN_WRITE', false)
s:drop()

s = box.schema.space.create('test1', {engine = 'vinyl'})
i = s:create_index('pk1')
s:replace{1}
-- Unthrottle scheduler.
errinj.set('ERRINJ_VY_SCHED_TIMEOUT', 0)
fiber.sleep(0.1)
s:drop()
