test_run = require('test_run').new()
fiber = require('fiber')

--
-- gh-5536: out of memory on a joining replica. Introduce a WAL queue limit so
-- that appliers stop reading new transactions from master once the queue is
-- full.
--
box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('test')
_ = box.space.test:create_index('pk')

replication_timeout = box.cfg.replication_timeout
box.cfg{replication_timeout=1000}

test_run:cmd('create server replica with rpl_master=default,\
              script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

test_run:switch('replica')

-- Huge replication timeout to not cause reconnects while applier is blocked.
-- Tiny queue size to allow exactly one queue entry at a time.
box.cfg{wal_queue_max_size=1, replication_timeout=1000}
write_cnt = box.error.injection.get("ERRINJ_WAL_WRITE_COUNT")
-- Block WAL writes so that we may test queue overflow.
box.error.injection.set("ERRINJ_WAL_DELAY", true)

test_run:switch('default')

for i = 1,10 do box.space.test:insert{i} end

test_run:switch('replica')
-- Wait for replication. Cannot rely on lsn bump here. It won't happen while
-- WAL is blocked.
test_run:wait_cond(function()\
    return box.error.injection.get("ERRINJ_WAL_WRITE_COUNT") > write_cnt\
end)
require('fiber').sleep(0.5)
-- Only one entry fits when the limit is small.
assert(box.error.injection.get("ERRINJ_WAL_WRITE_COUNT") == write_cnt + 1)
box.error.injection.set("ERRINJ_WAL_DELAY", false)

-- Once the block is removed everything is written.
test_run:wait_cond(function()\
    return box.error.injection.get("ERRINJ_WAL_WRITE_COUNT") == write_cnt + 10\
end)
assert(box.space.test:count() == 10)
assert(box.info.replication[1].upstream.status == 'follow')

test_run:switch('default')

-- Cleanup.
box.cfg{replication_timeout=replication_timeout}
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')

