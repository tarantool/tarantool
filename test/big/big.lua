#!/usr/bin/env tarantool

box.cfg{
    listen              = os.getenv("LISTEN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    rows_per_wal        = 500000
}

require('console').listen(os.getenv('ADMIN'))

function compare(a,b)
    return a[1] < b[1]
end

function sorted(data)
    table.sort(data, compare)
    return data
end

