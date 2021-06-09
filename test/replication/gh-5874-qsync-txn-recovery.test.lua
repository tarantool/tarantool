test_run = require('test_run').new()
--
-- gh-5874: synchronous transactions should be recovered as whole units, not row
-- by row. So as to be able to roll them back when ROLLBACK is recovered
-- afterwards.
--
old_synchro_quorum = box.cfg.replication_synchro_quorum
old_synchro_timeout = box.cfg.replication_synchro_timeout
box.cfg{replication_synchro_quorum = 2, replication_synchro_timeout = 0.001}
engine = test_run:get_cfg('engine')
async = box.schema.create_space('async', {engine = engine})
_ = async:create_index('pk')
sync = box.schema.create_space('sync', {is_sync = true, engine = engine})
_ = sync:create_index('pk')
box.ctl.promote()

-- The transaction fails, but is written to the log anyway.
box.begin() async:insert{1} sync:insert{1} box.commit()
-- Ok, the previous txn is rolled back.
_ = async:insert{1}
box.cfg{replication_synchro_quorum = 1, replication_synchro_timeout = 1000}
_ = sync:insert{1}
-- Try multi-statement sync txn to see how it recovers.
box.begin() sync:insert{2} sync:insert{3} box.commit()

-- See if NOP multi-statement recovery works fine.
--
-- Start with NOP.
do_skip = false
_ = async:before_replace(function()                                             \
    if do_skip then                                                             \
        return nil                                                              \
    end                                                                         \
end)
box.begin()                                                                     \
do_skip = true                                                                  \
async:replace{2}                                                                \
do_skip = false                                                                 \
async:replace{3}                                                                \
box.commit()

-- NOP in the middle.
box.begin()                                                                     \
async:replace{4}                                                                \
do_skip = true                                                                  \
async:replace{5}                                                                \
do_skip = false                                                                 \
async:replace{6}                                                                \
box.commit()

-- All NOP.
box.begin()                                                                     \
do_skip = true                                                                  \
async:replace{7}                                                                \
async:replace{8}                                                                \
do_skip = false                                                                 \
box.commit()

--
-- First row might be for a local space and its LSN won't match TSN. Need to be
-- ok with that.
--
loc = box.schema.create_space('loc', {is_local = true, engine = engine})
_ = loc:create_index('pk')
box.begin()                                                                     \
loc:replace{1}                                                                  \
async:replace{9}                                                                \
box.commit()

-- All local.
box.begin()                                                                     \
loc:replace{2}                                                                  \
loc:replace{3}                                                                  \
box.commit()

test_run:cmd('restart server default')
async = box.space.async
sync = box.space.sync
loc = box.space.loc
async:select()
sync:select()
loc:select()
async:drop()
sync:drop()
loc:drop()
box.ctl.demote()
