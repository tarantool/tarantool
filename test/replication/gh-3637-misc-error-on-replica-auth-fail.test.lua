test_run = require('test_run').new()
test_run:cmd("restart server default")

--
-- Test case for gh-3637, gh-4550. Before the fix replica would
-- exit with an error if a user does not exist or a password is
-- incorrect. Now check that we don't hang/panic and successfully
-- connect.
--
fiber = require('fiber')
test_run:cmd("create server replica_auth with rpl_master=default, script='replication/replica_auth.lua'")
test_run:cmd("start server replica_auth with wait=False, wait_load=False, args='cluster:pass 0.05'")
-- Wait a bit to make sure replica waits till user is created.
fiber.sleep(0.1)
box.schema.user.create('cluster')
-- The user is created. Let the replica fail auth request due to
-- a wrong password.
fiber.sleep(0.1)
box.schema.user.passwd('cluster', 'pass')
box.schema.user.grant('cluster', 'replication')

while box.info.replication[2] == nil do fiber.sleep(0.01) end
vclock = test_run:get_vclock('default')
vclock[0] = nil
_ = test_run:wait_vclock('replica_auth', vclock)

test_run:cmd("stop server replica_auth")
test_run:cmd("cleanup server replica_auth")
test_run:cmd("delete server replica_auth")
test_run:cleanup_cluster()

box.schema.user.drop('cluster')
