#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen              = os.getenv("LISTEN"),
    wal_cleanup_delay   = tonumber(arg[1]) or 0,
})
