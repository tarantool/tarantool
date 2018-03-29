env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

box.schema.user.grant('guest', 'read,write,execute', 'universe')

net_box = require('net.box')
errinj = box.error.injection

box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica_timeout.lua'")
test_run:cmd("start server replica with args='1'")
test_run:cmd("switch replica")

test_run:cmd("switch default")
s = box.schema.space.create('test', {engine = engine});
-- vinyl does not support hash index
index = s:create_index('primary', {type = (engine == 'vinyl' and 'tree' or 'hash') })

test_run:cmd("switch replica")
fiber = require('fiber')
while box.space.test == nil do fiber.sleep(0.01) end
test_run:cmd("switch default")
test_run:cmd("stop server replica")

-- insert values on the master while replica is stopped and can't fetch them
for i=1,100 do s:insert{i, 'this is test message12345'} end

-- sleep after every tuple
errinj.set("ERRINJ_RELAY_TIMEOUT", 1000.0)

test_run:cmd("start server replica with args='0.01'")
test_run:cmd("switch replica")

-- Check that replica doesn't enter read-write mode before
-- catching up with the master: to check that we inject sleep into
-- the master relay_send function and attempt a data modifying
-- statement in replica while it's still fetching data from the
-- master.
-- In the next two cases we try to delete a tuple while replica is
-- catching up with the master (local delete, remote delete) case
--
-- #1: delete tuple on replica
--
box.space.test ~= nil
d = box.space.test:delete{1}
box.space.test:get(1) ~= nil

-- case #2: delete tuple by net.box

test_run:cmd("switch default")
test_run:cmd("set variable r_uri to 'replica.listen'")
c = net_box.connect(r_uri)
d = c.space.test:delete{1}
c.space.test:get(1) ~= nil

-- check sync
errinj.set("ERRINJ_RELAY_TIMEOUT", 0)

-- cleanup
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')

