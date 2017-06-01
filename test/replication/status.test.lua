env = require('test_run')
test_run = env.new()
test_run:cmd('switch default')
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd('switch replica')

r = box.info.replication[1]
r.status == "follow"
r.lag < 1
r.idle < 1
r.uuid ~= nil

box.space._schema:insert({'dup'})
test_run:cmd('switch default')
box.space._schema:insert({'dup'})
test_run:cmd('switch replica')
r = box.info.replication[1]
r.status == "stopped"
r.message:match('Duplicate') ~= nil

box.cfg { replication = "" }
next(box.info.replication) == nil

test_run:cmd('switch default')
box.schema.user.revoke('guest', 'replication')
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
