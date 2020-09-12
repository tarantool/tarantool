#!/usr/bin/env tarantool

-- Start the console first to allow test-run to attach even before
-- box.cfg is finished.
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = os.getenv("MASTER"),
    memtx_memory        = 107374182,
--    pid_file            = "tarantool.pid",
--    logger              = "tarantool.log",
})
