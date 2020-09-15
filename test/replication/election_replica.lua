#!/usr/bin/env tarantool

local INSTANCE_ID = string.match(arg[0], "%d")
local SOCKET_DIR = require('fio').cwd()

local function instance_uri(instance_id)
    return SOCKET_DIR..'/autobootstrap'..instance_id..'.sock';
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
    election_is_enabled = true,
    election_is_candidate = true,
    election_timeout = 0.1,
    replication_synchro_quorum = 3,
    -- To reveal more election logs.
    log_level = 6,
})

box.once("bootstrap", function()
    box.schema.user.grant('guest', 'super')
end)
