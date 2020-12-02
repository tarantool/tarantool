#!/usr/bin/env tarantool

local SOCKET_DIR = require('fio').cwd()

local QUORUM = tonumber(arg[1])
local TIMEOUT = arg[2] and tonumber(arg[2]) or 0.1
local CONNECT_TIMEOUT = arg[3] and tonumber(arg[3]) or 10
INSTANCE_URI = SOCKET_DIR .. '/replica_quorum.sock'

function nonexistent_uri(id)
    return SOCKET_DIR .. '/replica_quorum' .. (1000 + id) .. '.sock'
end

require('console').listen(os.getenv('ADMIN'))

box.cfg{
    listen = INSTANCE_URI,
    replication_timeout = TIMEOUT,
    replication_connect_timeout = CONNECT_TIMEOUT,
    replication_connect_quorum = QUORUM,
    replication = {INSTANCE_URI,
                   nonexistent_uri(1),
                   nonexistent_uri(2)}
}
