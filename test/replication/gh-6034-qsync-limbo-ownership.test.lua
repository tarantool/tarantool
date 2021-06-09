test_run = require('test_run').new()

--
-- gh-6034: test that transactional limbo isn't accessible without a promotion.
--
synchro_quorum = box.cfg.replication_synchro_quorum
election_mode = box.cfg.election_mode
box.cfg{replication_synchro_quorum = 1, election_mode='off'}

_ = box.schema.space.create('async'):create_index('pk')
_ = box.schema.space.create('sync', {is_sync=true}):create_index('pk')

-- Limbo is initially unclaimed, everyone is writeable.
assert(not box.info.ro)
assert(box.info.synchro.queue.owner == 0)
box.space.async:insert{1} -- success.
-- Synchro spaces aren't writeable
box.space.sync:insert{1} -- error.

box.ctl.promote()
assert(not box.info.ro)
assert(box.info.synchro.queue.owner == box.info.id)
box.space.sync:insert{1} -- success.

-- Everyone but the limbo owner is read-only.
box.schema.user.grant('guest', 'replication')
test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')
test_run:cmd('set variable rpl_listen to "replica.listen"')
orig_replication = box.cfg.replication
box.cfg{replication=rpl_listen}

test_run:switch('replica')
assert(box.info.ro)
assert(box.info.synchro.queue.owner == test_run:get_server_id('default'))
box.space.async:insert{2} -- failure.

-- Promotion on the other node. Default should become ro.
box.ctl.promote()
assert(not box.info.ro)
assert(box.info.synchro.queue.owner == box.info.id)
box.space.sync:insert{2} -- success.

test_run:switch('default')
test_run:wait_lsn('default', 'replica')
assert(box.info.ro)
assert(box.info.synchro.queue.owner == test_run:get_server_id('replica'))
box.space.sync:insert{3} -- failure.

box.ctl.promote()
box.ctl.demote()
assert(not box.info.ro)
box.space.sync:insert{3} -- still fails.
assert(box.info.synchro.queue.owner == 0)
box.space.async:insert{3} -- success.

-- Cleanup.
box.ctl.demote()
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.schema.user.revoke('guest', 'replication')
box.space.sync:drop()
box.space.async:drop()
box.cfg{\
    replication_synchro_quorum = synchro_quorum,\
    election_mode = election_mode,\
    replication = orig_replication,\
}
