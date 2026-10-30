test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
--
-- gh-5298: rows from snapshot got their LSN = signature of the instance vclock.
-- All the same LSN. For example, signature at the moment of recovery start
-- was 100. Then all rows from the snap got their LSN = 100. That broke the
-- limbo, because it assumes LSNs grow. In order to skip duplicate ACKs.
--
_ = box.schema.space.create('sync', {is_sync = true, engine = engine})
_ = box.space.sync:create_index('pk')
box.ctl.promote()
for i = 1, 10 do box.space.sync:replace{i} end

-- Local rows could affect this by increasing the signature.
_ = box.schema.space.create('loc', {is_local = true, engine = engine})
_ = box.space.loc:create_index('pk')
for i = 1, 10 do box.space.loc:replace{i} end

box.snapshot()

test_run:cmd("restart server default")

-- Would be non-empty if limbo would incorrectly handle the snapshot end.
box.info.synchro.queue.len

box.ctl.promote()

box.space.sync:drop()
box.space.loc:drop()
box.ctl.demote()
