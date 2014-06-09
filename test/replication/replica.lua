#!/usr/bin/env tarantool

print('MASTER_PORT: ',os.getenv("MASTER_PORT"))
print('MASTER_URI: ',os.getenv("MASTER_URI"))

box.cfg({
    primary_port        = os.getenv("PRIMARY_PORT"),
    admin_port          = os.getenv("ADMIN_PORT"),
    replication_source  = os.getenv("MASTER_PORT"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    logger              = "tarantool.log",
    custom_proc_title   = "replica",
})
