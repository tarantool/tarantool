env = require('test_run')
test_run = env.new()

SERVERS = {'rebootstrap1', 'rebootstrap2'}
test_run:create_cluster(SERVERS, "replication")
test_run:wait_fullmesh(SERVERS)

test_run:cmd('switch rebootstrap1')
fiber = require('fiber')
-- Stop updating replicaset vclock to simulate a situation, when
-- a row is already relayed to the remote master, but the local
-- vclock update hasn't happened yet.
box.error.injection.set('ERRINJ_RELAY_FASTER_THAN_TX', true)
lsn = box.info.lsn
f = fiber.create(function() box.space._schema:replace{'something'} end)
f:status()
-- Vclock isn't updated.
box.info.lsn == lsn

-- Wait until the remote instance gets the row.
test_run:wait_cond(function()\
    return test_run:get_vclock('rebootstrap2')[box.info.id] > lsn\
end, 10)

-- Restart the remote instance. This will make the first instance
-- resubscribe without entering orphan mode.
test_run:cmd('restart server rebootstrap2 with wait=False')
test_run:cmd('switch rebootstrap1')
-- Wait until resubscribe is sent
test_run:wait_cond(function()\
    return box.info.replication[2].upstream.status == 'sync'\
end, 10)
box.error.injection.set('ERRINJ_RELAY_FASTER_THAN_TX', false)
box.space._schema:get{'something'}
test_run:cmd('switch default')
test_run:drop_cluster(SERVERS)
