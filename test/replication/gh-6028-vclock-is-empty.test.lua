test_run = require('test_run').new()

box.schema.user.grant('guest', 'replication')
s = box.schema.create_space('test')
_ = s:create_index('pk')


-- Case 1
test_run:cmd('create server replica with rpl_master=default,\
              script="replication/gh-6028-replica.lua"')
test_run:cmd('start server replica')

test_run:cmd('stop server replica')
s:insert{1}


-- Case 2
test_run:cmd('start server replica')
s:insert{2}


test_run:cmd('stop server replica')
test_run:cmd('cleanup server replica')
test_run:cmd('delete server replica')
