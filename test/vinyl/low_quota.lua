#!/usr/bin/env tarantool

box.cfg{
    vinyl_memory = 1024 * 1024,
}

require('console').listen(os.getenv('ADMIN'))
