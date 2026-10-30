--
-- gh-6035: verify synchronous rows filtration in applier,
-- we need to be sure that filtering synchronous rows is
-- done via transaction initiator not sender (iow via
-- xrow->replica_id).
--
test_run = require('test_run').new()

--
-- Prepare a scheme with transitional node
--
--  master <=> replica1 => replica2
--
-- such as transaction initiated on the master node would
-- be replicated to the replica2 via interim replica1 node.
--

test_run:cmd('create server master with script="replication/gh-6035-master.lua"')
test_run:cmd('create server replica1 with script="replication/gh-6035-replica1.lua"')
test_run:cmd('create server replica2 with script="replication/gh-6035-replica2.lua"')

test_run:cmd('start server master')
test_run:cmd('start server replica1')
test_run:cmd('start server replica2')

test_run:switch('replica2')
box.cfg({replication = {"unix/:./replica1.sock"}})

--
-- Make the master to be RAFT leader.
test_run:switch('master')
box.cfg({                                       \
    replication = {                             \
            "unix/:./master.sock",              \
            "unix/:./replica1.sock",            \
    },                                          \
    replication_synchro_quorum = 2,             \
    election_mode = 'manual',                   \
})

box.ctl.promote()
_ = box.schema.space.create("sync", {is_sync = true})
_ = box.space.sync:create_index("pk")
box.space.sync:insert{1}

--
-- The first hop is replica1.
test_run:switch('replica1')
box.space.sync:select{}

--
-- And the second hop is replica2 where
-- replica1 replicated the data to us.
test_run:switch('replica2')
test_run:wait_lsn('replica2', 'master')
box.space.sync:select{}

test_run:switch('default')
test_run:cmd('stop server master')
test_run:cmd('delete server master')
test_run:cmd('stop server replica1')
test_run:cmd('delete server replica1')
test_run:cmd('stop server replica2')
test_run:cmd('delete server replica2')
