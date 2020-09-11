test_run = require('test_run').new()
test_run:cmd("restart server default")
uuid = require('uuid')

--
-- gh-3704 move cluster id check to replica
--
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
box.schema.user.grant("guest", "replication")
test_run:cmd("start server replica")
test_run:grep_log("replica", "REPLICASET_UUID_MISMATCH")
test_run:wait_downstream(2, {status = 'follow'})
-- change master's cluster uuid and check that replica doesn't connect.
test_run:cmd("stop server replica")
_ = box.space._schema:replace{'cluster', tostring(uuid.new())}
-- master believes replica is in cluster, but their cluster UUIDs differ.
test_run:cmd("start server replica")
test_run:wait_log("replica", "REPLICASET_UUID_MISMATCH", nil, 1.0)
test_run:wait_downstream(2, {status = 'stopped'})

test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
