#!/usr/bin/env tarantool
local os = require('os')
box.cfg({
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    force_recovery      = true,
})

require('console').listen(os.getenv('ADMIN'))
