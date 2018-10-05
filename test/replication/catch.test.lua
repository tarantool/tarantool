env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

net_box = require('net.box')
errinj = box.error.injection

box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica_timeout.lua'")
test_run:cmd("start server replica with args='0.1'")
test_run:cmd("switch replica")

test_run:cmd("switch default")
s = box.schema.space.create('test', {engine = engine});
-- Vinyl does not support hash index.
index = s:create_index('primary', {type = (engine == 'vinyl' and 'tree' or 'hash') })

test_run:cmd("switch replica")
fiber = require('fiber')
while box.space.test == nil do fiber.sleep(0.01) end
test_run:cmd("switch default")
test_run:cmd("stop server replica")

-- Insert values on the master while replica is stopped and can't
-- fetch them.
errinj.set('ERRINJ_RELAY_SEND_DELAY', true)
for i = 1, 100 do s:insert{i, 'this is test message12345'} end

test_run:cmd("start server replica with args='0.01'")
test_run:cmd("switch replica")

-- Check that replica doesn't enter read-write mode before
-- catching up with the master: to check that we stop sending
-- rows on the master in relay_send function and attempt a data
-- modifying statement in replica while it's still fetching data
-- from the master.
--
-- In the next two cases we try to replace a tuple while replica
-- is catching up with the master (local replace, remote replace)
-- case.
--
-- Case #1: replace tuple on replica locally.
--
box.space.test ~= nil
box.space.test:replace{1}

-- Case #2: replace tuple on replica by net.box.

test_run:cmd("switch default")
test_run:cmd("set variable r_uri to 'replica.listen'")
c = net_box.connect(r_uri)
d = c.space.test:replace{1}

-- Resume replication.
errinj.set('ERRINJ_RELAY_SEND_DELAY', false)

-- Cleanup.
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
