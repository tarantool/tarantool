env = require('test_run')
test_run = env.new()

-- todo: check vinyl
-- engine = test_run:get_cfg('engine')

box.schema.user.grant('guest', 'replication')

NUM_INSTANCES = 2
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=1000}
_ = box.schema.space.create('sync', {is_sync=true, engine="memtx"})
_ = box.space.sync:create_index('pk')

test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica')

test_run:cmd("setopt delimiter ';'");
function async_tx(values)
    box.begin()
    box.space.sync:insert{values[1]}
    box.space.sync:insert{values[2]}
    box.commit({ is_async = true })
end;
test_run:cmd("setopt delimiter ''");

async_tx({1, 2})
box.space.sync:select{}

test_run:cmd("switch replica")
test_run:wait_cond(function() return box.space.sync:count() == 2 end, 10)
box.space.sync:select{}

test_run:cmd("switch default")

errinj = box.error.injection
errinj.set("ERRINJ_WAL_DELAY", true)

async_tx({3, 4})
box.space.sync:select{}

errinj.set("ERRINJ_WAL_DELAY", false)

test_run:cmd('restart server default')

test_run:cmd("setopt delimiter ';'");
function async_tx(values)
    box.begin()
    box.space.sync:insert{values[1]}
    box.space.sync:insert{values[2]}
    box.commit({ is_async = true })
end;
test_run:cmd("setopt delimiter ''");

test_run:wait_cond(function() return box.space.sync:count() == 4 end, 10)
box.space.sync:select{}

test_run:cmd("stop server replica")

errinj = box.error.injection
errinj.set('ERRINJ_RELAY_SEND_DELAY', true)

async_tx({5, 6})
box.space.sync:select{}

test_run:cmd('start server replica')
errinj = box.error.injection
errinj.set('ERRINJ_RELAY_SEND_DELAY', false)

test_run:switch('replica')

test_run:wait_cond(function() return box.space.sync:count() == 6 end, 10)
box.space.sync:select{}

-- Teardown.
test_run:cmd('switch default')

test_run:cmd('stop server replica')
test_run:cmd('delete server replica')

test_run:cleanup_cluster()

