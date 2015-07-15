#!/usr/bin/env tarantool

box.cfg{
    listen              = os.getenv("LISTEN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    rows_per_wal        = 50,
    logger = "tarantool.log"
}

package.cpath = '../app/?.so;../app/?.dylib;'..package.cpath

log = require('log')
net = require('net.box')

box.schema.func.create('function1', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'function1')
box.schema.func.create('function1.test', {language = "C"})
box.schema.user.grant('guest', 'execute', 'function', 'function1.test')

c = net:new(os.getenv("LISTEN"))
c:call('function1')
c:call('function1.test')

os.exit(0)
