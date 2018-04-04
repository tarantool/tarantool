env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

box.schema.user.grant('guest', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'replication')

space = box.schema.space.create('test', {engine = engine});
index = box.space.test:create_index('primary')

test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")
box.cfg{replication_skip_conflict = true}
box.space.test:insert{1}

test_run:cmd("switch default")
space:insert{1, 1}
space:insert{2}
box.info.status

vclock = test_run:get_vclock('default')
_ = test_run:wait_vclock("replica", vclock)
test_run:cmd("switch replica")
box.info.replication[1].upstream.message
box.info.replication[1].upstream.status
box.space.test:select()

test_run:cmd("switch default")
box.info.status

-- cleanup
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
