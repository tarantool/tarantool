#!/usr/bin/env tarantool

local socket = require('socket')

test_run = require('test_run')
inspector = test_run.new()

print('create instance')
print(inspector:cmd("create server replica with rpl_master=default, script='box/box.lua'\n"))

print('start instance')
print(inspector:cmd('start server replica'))
os.exit(0)
