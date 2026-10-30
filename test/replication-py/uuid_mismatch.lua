#!/usr/bin/env tarantool

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = os.getenv("MASTER"),
    replicaset_uuid     = "12345678-abcd-abcd-abcd-123456789000",
})

require('console').listen(os.getenv('ADMIN'))
