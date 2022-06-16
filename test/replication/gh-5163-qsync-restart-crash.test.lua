test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

--
-- gh-5163: master during recovery treated local transactions as
-- remote and crashed.
--
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
box.ctl.promote()

box.space.sync:replace{1}
test_run:cmd('restart server default')
box.space.sync:select{}
box.ctl.promote()
box.space.sync:drop()
box.ctl.demote()
