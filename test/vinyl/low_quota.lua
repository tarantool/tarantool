#!/usr/bin/env tarantool

local LIMIT = tonumber(arg[1])

box.cfg{
    vinyl_memory = LIMIT,
    vinyl_max_tuple_size = 2 * LIMIT,
}

require('console').listen(os.getenv('ADMIN'))
