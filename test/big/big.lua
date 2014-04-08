#!/usr/bin/env tarantool_box

box.cfg{
    primary_port        = os.getenv("PRIMARY_PORT"),
    admin_port          = os.getenv("ADMIN_PORT"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    rows_per_wal        = 50
}
