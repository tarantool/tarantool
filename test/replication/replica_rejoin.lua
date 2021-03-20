#!/usr/bin/env tarantool

-- Start the console first to allow test-run to attach even before
-- box.cfg is finished.
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = {os.getenv("MASTER"), os.getenv("LISTEN")},
    wal_cleanup_delay   = 0,
})
