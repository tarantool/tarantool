test_run = require('test_run').new()

-- gh-3510 assertion failure in replica_on_applier_disconnect()
test_run:cmd('create server er_load1 with script="replication/er_load1.lua"')
test_run:cmd('create server er_load2 with script="replication/er_load2.lua"')
test_run:cmd('start server er_load1 with wait=False, wait_load=False')
-- Instance er_load2 will fail with error ER_REPLICASET_UUID_MISMATCH.
-- This is OK since we only test here that er_load1 doesn't assert.
test_run:cmd('start server er_load2 with wait=True, wait_load=True, crash_expected = True')
test_run:cmd('stop server er_load1')
-- er_load2 exits automatically.
test_run:cmd('cleanup server er_load1')
test_run:cmd('cleanup server er_load2')
test_run:cmd('delete server er_load1')
test_run:cmd('delete server er_load2')
test_run:cleanup_cluster()
