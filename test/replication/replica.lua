#!/usr/bin/env tarantool
box.cfg({
    primary_port        = os.getenv("PRIMARY_PORT"),
    admin_port          = os.getenv("ADMIN_PORT"),
    replication_source  = "127.0.0.1:"..os.getenv("MASTER_PORT"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    logger              = "cat - >> tarantool.log",
    custom_proc_title   = "replica",
})
