test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

old_synchro_quorum = box.cfg.replication_synchro_quorum
old_synchro_timeout = box.cfg.replication_synchro_timeout
old_timeout = box.cfg.replication_timeout
box.schema.user.grant('guest', 'super')

test_run:cmd('create server replica with rpl_master=default,\
             script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

_ = box.schema.space.create('sync', {is_sync = true, engine = engine})
_ = box.space.sync:create_index('pk')

--
-- gh-5100: slow ACK sending shouldn't stun replica for the
-- replication timeout seconds.
--
test_run:cmd('switch default')
box.cfg{replication_timeout = 1000, replication_synchro_quorum = 2, replication_synchro_timeout = 1000}

test_run:switch('replica')
box.cfg{replication_timeout = 1000}
box.error.injection.set('ERRINJ_APPLIER_SLOW_ACK', true)

test_run:cmd('switch default')
for i = 1, 10 do box.space.sync:replace{i} end
box.space.sync:count()

test_run:switch('replica')
box.space.sync:count()
box.error.injection.set('ERRINJ_APPLIER_SLOW_ACK', false)

--
-- gh-5123: replica WAL fail shouldn't crash with quorum 1.
--
test_run:switch('default')
box.cfg{replication_synchro_quorum = 1, replication_synchro_timeout = 5}
box.space.sync:insert{11}

test_run:switch('replica')
box.error.injection.set('ERRINJ_WAL_IO', true)

test_run:switch('default')
box.space.sync:insert{12}

test_run:switch('replica')
test_run:wait_upstream(1, {status='stopped'})
box.error.injection.set('ERRINJ_WAL_IO', false)

test_run:cmd('restart server replica')
box.space.sync:select{12}

--
-- gh-5147: at local WAL write fail limbo entries should be
-- deleted from the end of the limbo, not from the beginning.
-- Otherwise it should crash.
--
test_run:switch('default')
fiber = require('fiber')
box.cfg{replication_synchro_quorum = 3, replication_synchro_timeout = 1000}
box.error.injection.set("ERRINJ_WAL_DELAY", true)
ok1, err1 = nil
f1 = fiber.create(function()                                                    \
    ok1, err1 = pcall(box.space.sync.replace, box.space.sync, {13})             \
end)
box.error.injection.set("ERRINJ_WAL_IO", true)
box.space.sync:replace({14})
box.error.injection.set("ERRINJ_WAL_IO", false)
box.error.injection.set("ERRINJ_WAL_DELAY", false)
box.cfg{replication_synchro_quorum = 2}
box.space.sync:replace({15})
test_run:wait_cond(function() return f1:status() == 'dead' end)
ok1, err1
box.space.sync:select{13}, box.space.sync:select{14}, box.space.sync:select{15}
test_run:switch('replica')
box.space.sync:select{13}, box.space.sync:select{14}, box.space.sync:select{15}

test_run:cmd('switch default')

box.cfg{                                                                        \
    replication_synchro_quorum = old_synchro_quorum,                            \
    replication_synchro_timeout = old_synchro_timeout,                          \
    replication_timeout = old_timeout,                                          \
}
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')

box.space.sync:drop()
box.schema.user.revoke('guest', 'super')
