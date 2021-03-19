#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    wal_mode            = 'none',
    pid_file            = "tarantool.pid",
}

require('console').listen(os.getenv('ADMIN'))
