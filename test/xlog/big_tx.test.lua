env = require('test_run').new()
digest = require('digest')

_ = box.schema.space.create('big_tx'):create_index('pk')
t = box.space.big_tx:insert({1, digest.urandom(512 * 1024)})
env:cmd('restart server default')

#box.space.big_tx:select{}

box.space.big_tx:drop()
