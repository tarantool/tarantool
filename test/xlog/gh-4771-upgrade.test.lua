test_run = require('test_run').new()

test_run:cmd('create server upgrade with script="xlog/upgrade.lua", '..		\
             'workdir="xlog/upgrade/2.1.3/gh-4771-upgrade-sequence"')
test_run:cmd('start server upgrade')
test_run:switch('upgrade')

box.schema.upgrade()

s = box.space.test1
box.space._sequence:select{}
box.space._sequence_data:select{}
box.space._space_sequence:select{}
s:select{}
_ = s:replace{box.NULL}
s:select{}

box.space.test2:select{}

box.sequence.seq3:next()

test_run:switch('default')
test_run:cmd('stop server upgrade')
test_run:cmd('delete server upgrade')
