#!/usr/bin/env tarantool

box.cfg{
    listen                 = os.getenv("LISTEN"),
    memtx_memory           = 33554432
}

require('console').listen(os.getenv('ADMIN'))
