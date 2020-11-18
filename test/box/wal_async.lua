#!/usr/bin/env tarantool

box.cfg{
    listen                 = os.getenv("LISTEN"),
    pid_file               = "tarantool.pid",
    wal_mode               = "async",
}

require('console').listen(os.getenv('ADMIN'))
