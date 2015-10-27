engine = 'memtx'
box.schema.user.grant('guest', 'read,write,execute', 'universe')
s = box.schema.create_space('engine', {engine=engine})
i = s:create_index('primary')
function demo() require('log').info('TEST') end
box.space.engine:insert{1,2,3}

test_run = require('test_run')
inspector = test_run.new()
inspector:eval('default', 'return demo()')
inspector:cmd("create server replica with rpl_master=default, script='replication/replica.lua'\n")
inspector:cmd('start server replica')
inspector:eval('default', 'return 2+2')

box.schema.user.revoke('guest', 'read,write,execute', 'universe')
