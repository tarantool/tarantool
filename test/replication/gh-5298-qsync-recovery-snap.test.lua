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

-- Could hang if the limbo would incorrectly handle the snapshot end.
box.space.sync:replace{11}

old_synchro_quorum = box.cfg.replication_synchro_quorum
old_synchro_timeout = box.cfg.replication_synchro_timeout

box.cfg{                                                                        \
    replication_synchro_timeout = 0.001,                                        \
    replication_synchro_quorum = 2,                                             \
}
box.space.sync:replace{12}

box.cfg{                                                                        \
    replication_synchro_timeout = 1000,                                         \
    replication_synchro_quorum = 1,                                             \
}
box.space.sync:replace{13}
box.space.sync:get({11})
box.space.sync:get({12})
box.space.sync:get({13})

box.cfg{                                                                        \
    replication_synchro_timeout = old_synchro_timeout,                          \
    replication_synchro_quorum = old_synchro_quorum,                            \
}
box.space.sync:drop()
box.space.loc:drop()
box.ctl.demote()
