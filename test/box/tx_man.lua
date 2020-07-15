#!/usr/bin/env tarantool

box.cfg{
    listen                 = os.getenv("LISTEN"),
    memtx_memory           = 107374182,
    pid_file               = "tarantool.pid",
    memtx_use_mvcc_engine  = true,
}

require('console').listen(os.getenv('ADMIN'))
