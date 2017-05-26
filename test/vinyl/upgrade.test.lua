test_run = require('test_run').new()

version = test_run:get_cfg('version')
work_dir = 'vinyl/upgrade/' .. version

test_run:cmd('create server upgrade with script="vinyl/upgrade.lua", workdir="' .. work_dir .. '"')
test_run:cmd('start server upgrade')
test_run:switch('upgrade')

box.space.test.index.i1:select()
box.space.test.index.i2:select()
box.space.test_truncate.index.i1:select()
box.space.test_truncate.index.i2:select()
box.space.test_split:select()
box.space.test_split:select()
box.space.test_drop == nil

test_run:switch('default')
test_run:cmd('stop server upgrade')
test_run:cmd('cleanup server upgrade')
