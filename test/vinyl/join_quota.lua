#!/usr/bin/env tarantool

box.cfg({
    listen                    = os.getenv("LISTEN"),
    replication               = os.getenv("MASTER"),
    vinyl_memory              = 1024 * 1024,
})

require('console').listen(os.getenv('ADMIN'))
