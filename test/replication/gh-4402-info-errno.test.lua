test_run = require('test_run').new()

--
-- gh-4402: replication info table should contain not only
-- Tarantool's error, but also a system error (errno's message)
-- when possible.
--

box.schema.user.grant('guest', 'replication')

test_run:cmd('create server replica with rpl_master=default, script="replication/replica.lua"')
test_run:cmd('start server replica')
i = box.info
replica_id = i.id % 2 + 1
d = i.replication[replica_id].downstream
d ~= nil and d.status == 'follow' or i

test_run:cmd('stop server replica')
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
i = box.info
d = i.replication[replica_id].downstream
d ~= nil and d.system_message ~= nil and d.message ~= nil or i

box.schema.user.revoke('guest', 'replication')
