#!/usr/bin/env tarantool

print('MASTER: ',os.getenv("MASTER"))
print('MASTER_URI: ',os.getenv("MASTER_URI"))

box.cfg({
    listen              = os.getenv("LISTEN"),
    admin               = os.getenv("ADMIN"),
    replication_source  = os.getenv("MASTER"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    logger              = "tarantool.log",
    custom_proc_title   = "replica",
})
