#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
}

require('console').listen(os.getenv('ADMIN'))

local _hide = {
    pid_file=1, logger=1, listen=1, phia_dir=1,
    snap_dir=1, wal_dir=1,
    slab_alloc_maximal=1, slab_alloc_minimal=1
}

function cfg_filter(data)
    if type(data)~='table' then return data end
    local keys,k,_ = {}
    for k in pairs(data) do
        table.insert(keys, k)
    end
    table.sort(keys)
    local result = {}
    for _,k in pairs(keys) do
        table.insert(result, {k, _hide[k] and '<hidden>' or cfg_filter(data[k])})
    end
    return result
end

function compare(a,b)
    return a[1] < b[1]
end

function sorted(data)
    table.sort(data, compare)
    return data
end
