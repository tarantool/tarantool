#!/usr/bin/env tarantool

local INSTANCE_ID = string.match(arg[0], "%d")
local SOCKET_DIR = require('fio').cwd()
local SYNCHRO_QUORUM = arg[1] and tonumber(arg[1]) or 3
local ELECTION_TIMEOUT = arg[2] and tonumber(arg[2]) or 0.1
local ELECTION_MODE = arg[3] or 'candidate'
local CONNECT_QUORUM = arg[4] and tonumber(arg[4]) or 3

local function instance_uri(instance_id)
    return SOCKET_DIR..'/election_replica'..instance_id..'.sock';
end

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = instance_uri(INSTANCE_ID),
    replication = {
        instance_uri(1),
        instance_uri(2),
        instance_uri(3),
    },
    replication_timeout = 0.1,
    replication_connect_quorum = CONNECT_QUORUM,
    election_mode = ELECTION_MODE,
    election_timeout = ELECTION_TIMEOUT,
    replication_synchro_quorum = SYNCHRO_QUORUM,
    replication_synchro_timeout = 0.1,
    -- To reveal more election logs.
    log_level = 6,
})

box.once("bootstrap", function()
    box.schema.user.grant('guest', 'super')
end)
