#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.error.injection.set("ERRINJ_REPLICASET_VCLOCK", true)

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = {os.getenv("MASTER"), os.getenv("LISTEN")},
    memtx_memory        = 107374182,
})

box.error.injection.set("ERRINJ_REPLICASET_VCLOCK", false)
