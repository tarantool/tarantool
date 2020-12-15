#!/usr/bin/env tarantool

local inspector = require('test_run').new()

print('create instance')
print(inspector:cmd("create server replica with rpl_master=default, script='box/box.lua'\n"))

print('start instance')
print(inspector:cmd('start server replica'))
inspector:cmd('stop server replica')
os.exit(0)
