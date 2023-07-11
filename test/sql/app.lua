#!/usr/bin/env tarantool

box.cfg{
    listen              = os.getenv("LISTEN"),
    pid_file            = "tarantool.pid",
    memtx_max_tuple_size = 5 * 1024 * 1024,
    vinyl_max_tuple_size = 5 * 1024 * 1024,
}

require('fiber').set_max_slice(100500)
require('console').listen(os.getenv('ADMIN'))
