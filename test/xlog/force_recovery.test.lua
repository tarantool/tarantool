#!/usr/bin/env tarantool

env = require('test_run')
test_run = env.new()

box.cfg{}

test_run:cmd('create server test with script = "xlog/force_recovery.lua"')

test_run:cmd("start server test")
test_run:cmd("switch test")
box.space._schema:replace({'test'})

test_run:cmd("restart server test")
box.space._schema:replace({'lost'})

test_run:cmd("restart server test")
box.space._schema:replace({'tost'})

-- corrupted (empty) in the middle (old behavior: goto error on recovery)
fio = require('fio')
path = fio.pathjoin(box.cfg.wal_dir, string.format('%020d.xlog', box.info.lsn - 2))
fio.truncate(path)

test_run:cmd("restart server test")
box.space._schema:replace({'last'})

-- corrupted (empty), last
fio = require('fio')
path = fio.pathjoin(box.cfg.wal_dir, string.format('%020d.xlog', box.info.lsn - 1))
fio.truncate(path)

test_run:cmd("restart server test")
box.space._schema:replace({'test'})

test_run:cmd("restart server test")
box.space._schema:replace({'tost'})

-- corrupted header, last
fio = require('fio')
path = fio.pathjoin(box.cfg.wal_dir, string.format('%020d.xlog', box.info.lsn - 1))
f = fio.open(path, {'O_WRONLY'})
f:write('DEAD')
f:close()

test_run:cmd("restart server test")
box.space._schema:replace({'post'})
