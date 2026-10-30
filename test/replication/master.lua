#!/usr/bin/env tarantool
local os = require('os')
box.cfg({
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    replication_timeout = 0.1
})

require('console').listen(os.getenv('ADMIN'))
