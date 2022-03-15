#!/usr/bin/env tarantool

box.cfg{
    vinyl_cache = 15 * 1024, -- 15K to test cache eviction
    vinyl_defer_deletes = true,
}

require('console').listen(os.getenv('ADMIN'))
