#!/usr/bin/env tarantool

local USER_PASS = arg[1]
local TIMEOUT = arg[2] and tonumber(arg[2]) or 0.1

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = os.getenv("LISTEN"),
    replication = USER_PASS .. "@" .. os.getenv("MASTER"),
    replication_timeout = TIMEOUT,
})
