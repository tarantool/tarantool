test_run = require('test_run').new()
fio = require('fio')

--
-- Test that box.cfg.force_recovery is ignored by relay threads (gh-3910).
--
_ = box.schema.space.create('test')
_ = box.space.test:create_index('primary')
box.schema.user.grant('guest', 'replication')

-- Deploy a replica.
test_run:cmd("create server force_recovery with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server force_recovery")

-- Stop the replica and wait for the relay thread to exit.
test_run:cmd("stop server force_recovery")
test_run:wait_cond(function() return box.info.replication[2].downstream.status == 'stopped' end) or box.info.replication[2].downstream.status

-- Delete an xlog file that is needed by the replica.
box.snapshot()
xlog = fio.pathjoin(box.cfg.wal_dir, string.format('%020d.xlog', box.info.signature))
box.space.test:replace{1}
box.snapshot()
box.space.test:replace{2}
fio.unlink(xlog)

-- Check that even though box.cfg.force_recovery is set,
-- replication will still fail due to LSN gap.
box.cfg{force_recovery = true}
test_run:cmd("start server force_recovery")
test_run:cmd("switch force_recovery")
box.space.test:select()
test_run:wait_cond(function() return box.info.replication[1].upstream.status == 'stopped' end) or box.info
test_run:cmd("switch default")
box.cfg{force_recovery = false}

-- Cleanup.
test_run:cmd("stop server force_recovery")
test_run:cmd("cleanup server force_recovery")
test_run:cmd("delete server force_recovery")
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
box.space.test:drop()
