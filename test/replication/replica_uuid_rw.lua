#!/usr/bin/env tarantool

local INSTANCE_ID = string.match(arg[0], "%d")
local SOCKET_DIR = require('fio').cwd()

local function instance_uri(instance_id)
    return SOCKET_DIR..'/replica_uuid_rw'..instance_id..'.sock'
end

local repl_tbl = {}
for num in string.gmatch(arg[1] or "", "%d") do
    table.insert(repl_tbl, instance_uri(num))
end

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    instance_uuid       = "aaaaaaaa-aaaa-0000-0000-00000000000"..INSTANCE_ID,
    listen              = instance_uri(INSTANCE_ID),
    replication         = repl_tbl,
    replication_timeout = arg[2] and tonumber(arg[2]) or 0.1,
    replication_connect_timeout = arg[3] and tonumber(arg[3]) or 0.5,
    replication_sync_timeout = arg[4] and tonumber(arg[4]) or 1.0,
})

box.once("bootstrap", function() box.schema.user.grant('guest', 'replication') end)
