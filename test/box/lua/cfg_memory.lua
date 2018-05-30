#!/usr/bin/env tarantool

local LIMIT = tonumber(arg[1])

box.cfg{
    wal_mode = 'none',
    memtx_memory = LIMIT,
}

require('console').listen(os.getenv('ADMIN'))
