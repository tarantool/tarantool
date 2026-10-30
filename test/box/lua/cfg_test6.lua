#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen = os.getenv("LISTEN"),
    replication         = "admin:test-cluster-cookie@" .. os.getenv("LISTEN"),
    replication_connect_timeout = 0.1,
}

require('console').listen(os.getenv('ADMIN'))
