#!/usr/bin/env tarantool
box.cfg{
    listen              = os.getenv("LISTEN"),
    admin              = os.getenv("ADMIN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    wal_mode            = "none"
}
