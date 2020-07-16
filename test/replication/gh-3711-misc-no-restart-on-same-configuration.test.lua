test_run = require('test_run').new()
test_run:cmd("restart server default")

--
-- gh-3711 Do not restart replication on box.cfg in case the
-- configuration didn't change.
--
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")

-- Access rights are checked only during reconnect. If the new
-- and old configurations are equivalent, no reconnect will be
-- issued and replication should continue working.
box.schema.user.revoke('guest', 'replication')
test_run:cmd("switch replica")
replication = box.cfg.replication[1]
box.cfg{replication = {replication}}
box.info.status == 'running'
box.cfg{replication = replication}
box.info.status == 'running'

-- Check that comparison of tables works as expected as well.
test_run:cmd("switch default")
box.schema.user.grant('guest', 'replication')
test_run:cmd("switch replica")
replication = box.cfg.replication
table.insert(replication, box.cfg.listen)
test_run:cmd("switch default")
box.schema.user.revoke('guest', 'replication')
test_run:cmd("switch replica")
box.cfg{replication = replication}
box.info.status == 'running'

test_run:cmd("switch default")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()
