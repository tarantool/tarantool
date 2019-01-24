env = require('test_run')
test_run = env.new()

--
-- gh-461: check that garbage collection works as expected
-- after rebootstrap.
--
box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('test', { id = 9000, engine = 'vinyl' })
_ = box.space.test:create_index('pk')
pad = string.rep('x', 12 * 1024)
for i = 1, 100 do box.space.test:replace{i, pad} end
box.snapshot()

-- Join a replica. Check its files.
test_run:cmd("create server replica with rpl_master=default, script='vinyl/replica_rejoin.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")
fio = require('fio')
fio.chdir(box.cfg.vinyl_dir)
fio.glob(fio.pathjoin(box.space.test.id, 0, '*'))
test_run:cmd("switch default")
test_run:cmd("stop server replica")

-- Invoke garbage collector on the master.
test_run:cmd("restart server default")
checkpoint_count = box.cfg.checkpoint_count
box.cfg{checkpoint_count = 1}
box.space.test:delete(1)
box.snapshot()
box.cfg{checkpoint_count = checkpoint_count}

-- Rebootstrap the replica. Check that old files are removed
-- by garbage collector.
test_run:cmd("start server replica")
test_run:cmd("switch replica")
box.cfg{checkpoint_count = 1}
box.snapshot()
fio = require('fio')
fio.chdir(box.cfg.vinyl_dir)
fio.glob(fio.pathjoin(box.space.test.id, 0, '*'))
box.space.test:count() -- 99
test_run:cmd("switch default")
test_run:cmd("stop server replica")

-- Invoke garbage collector on the master.
test_run:cmd("restart server default")
checkpoint_count = box.cfg.checkpoint_count
box.cfg{checkpoint_count = 1}
box.space.test:delete(2)
box.snapshot()
box.cfg{checkpoint_count = checkpoint_count}

-- Make the master fail join after sending data. Check that
-- files written during failed rebootstrap attempt are removed
-- by garbage collector.
box.error.injection.set('ERRINJ_RELAY_FINAL_JOIN', true)
test_run:cmd("start server replica with crash_expected=True") -- fail
test_run:cmd("start server replica with crash_expected=True") -- fail again
test_run:cmd("start server replica with args='disable_replication'")
test_run:cmd("switch replica")
fio = require('fio')
fio.chdir(box.cfg.vinyl_dir)
fio.glob(fio.pathjoin(box.space.test.id, 0, '*'))
box.space.test:count() -- 99
test_run:cmd("switch default")
test_run:cmd("stop server replica")
box.error.injection.set('ERRINJ_RELAY_FINAL_JOIN', false)

-- Rebootstrap after several failed attempts and make sure
-- old files are removed.
test_run:cmd("start server replica")
test_run:cmd("switch replica")
box.cfg{checkpoint_count = 1}
box.snapshot()
fio = require('fio')
fio.chdir(box.cfg.vinyl_dir)
fio.glob(fio.pathjoin(box.space.test.id, 0, '*'))
box.space.test:count() -- 98
test_run:cmd("switch default")
test_run:cmd("stop server replica")

-- Cleanup.
test_run:cmd("cleanup server replica")
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
