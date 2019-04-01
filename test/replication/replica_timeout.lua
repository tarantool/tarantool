#!/usr/bin/env tarantool

local TIMEOUT = tonumber(arg[1])
local CON_TIMEOUT = arg[2] and tonumber(arg[2]) or 60.0

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = os.getenv("MASTER"),
    memtx_memory        = 107374182,
    replication_timeout = TIMEOUT,
    replication_connect_timeout = CON_TIMEOUT,
})

require('console').listen(os.getenv('ADMIN'))
