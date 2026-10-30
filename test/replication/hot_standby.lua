#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))
box.cfg({
    listen              = os.getenv("MASTER"),
    memtx_memory        = 107374182,
    custom_proc_title   = "hot_standby",
    wal_dir             = "master",
    memtx_dir           = "master",
    vinyl_dir           = "master",
    hot_standby         = true,
    replication_timeout = 0.1,
})

