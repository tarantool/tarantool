test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

orig_synchro_quorum = box.cfg.replication_synchro_quorum
orig_synchro_timeout = box.cfg.replication_synchro_timeout
box.schema.user.grant('guest', 'super')

test_run:cmd('create server replica with rpl_master=default,\
              script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

--
-- gh-5167:
--
fiber = require('fiber')
box.cfg{replication_synchro_quorum = 2, replication_synchro_timeout = 1000}
_ = box.schema.space.create('sync', {is_sync = true, engine = engine})
_ = box.space.sync:create_index('pk')
box.ctl.promote()
-- Write something to flush the current master's state to replica.
_ = box.space.sync:insert{1}
_ = box.space.sync:delete{1}

box.cfg{replication_synchro_quorum = 3}
ok, err = nil
f = fiber.create(function()                                                     \
    ok, err = pcall(box.space.sync.insert, box.space.sync, {1})                 \
end)

test_run:switch('replica')
fiber = require('fiber')
test_run:wait_cond(function() return box.space.sync:count() == 1 end)
-- Snapshot will stuck in WAL thread on rotation before starting wait on the
-- limbo.
box.error.injection.set("ERRINJ_WAL_DELAY", true)
wal_write_count = box.error.injection.get("ERRINJ_WAL_WRITE_COUNT")
ok, err = nil
f = fiber.create(function() ok, err = pcall(box.snapshot) end)

test_run:switch('default')
box.cfg{replication_synchro_timeout = 0.0001}
test_run:wait_cond(function() return f:status() == 'dead' end)
ok, err
box.space.sync:select{}

test_run:switch('replica')
-- Rollback was received. Note, it is not legit to look for space:count() == 0.
-- Because ideally ROLLBACK should not be applied before written to WAL. That
-- means count() will be > 0 until WAL write succeeds.
test_run:wait_cond(function()                                                   \
    return box.error.injection.get("ERRINJ_WAL_WRITE_COUNT") > wal_write_count  \
end)
-- Now WAL rotation is done. Snapshot will fail, because will see that a
-- rollback happened during that. Meaning that the rotated WAL contains
-- not confirmed data, and it can't be used as a checkpoint.
box.error.injection.set("ERRINJ_WAL_DELAY", false)
test_run:wait_cond(function() return f:status() == 'dead' end)
ok, err

test_run:switch('default')
box.space.sync:drop()
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.schema.user.revoke('guest', 'super')
box.cfg{                                                                        \
    replication_synchro_quorum = orig_synchro_quorum,                           \
    replication_synchro_timeout = orig_synchro_timeout,                         \
}
box.ctl.demote()
