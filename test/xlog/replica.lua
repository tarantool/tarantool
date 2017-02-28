#!/usr/bin/env tarantool

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = os.getenv("MASTER"),
    memtx_memory        = 107374182,
--    pid_file            = "tarantool.pid",
--    logger              = "tarantool.log",
})

require('console').listen(os.getenv('ADMIN'))
