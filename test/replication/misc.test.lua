uuid = require('uuid')
test_run = require('test_run').new()

box.schema.user.grant('guest', 'replication')

-- gh-2991 - Tarantool asserts on box.cfg.replication update if one of
-- servers is dead
replication_timeout = box.cfg.replication_timeout
box.cfg{replication_timeout=0.05, replication={}}
box.cfg{replication = {'127.0.0.1:12345', box.cfg.listen}}
box.cfg{replication_timeout = replication_timeout}

-- gh-3111 - Allow to rebootstrap a replica from a read-only master
replica_uuid = uuid.new()
test_run:cmd('create server test with rpl_master=default, script="replication/replica_uuid.lua"')
test_run:cmd(string.format('start server test with args="%s"', replica_uuid))
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
box.cfg{read_only = true}
test_run:cmd(string.format('start server test with args="%s"', replica_uuid))
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
box.cfg{read_only = false}

box.schema.user.revoke('guest', 'replication')
