#!/usr/bin/env tarantool

-- get instance name from filename (quorum1.lua => quorum1)
local INSTANCE_ID = string.match(arg[0], "%d")

local SOCKET_DIR = require('fio').cwd()

local TIMEOUT = tonumber(arg[1])

local function instance_uri(instance_id)
    return SOCKET_DIR..'/rebootstrap'..instance_id..'.sock';
end

-- start console first
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = instance_uri(INSTANCE_ID),
    instance_uuid = '12345678-abcd-1234-abcd-123456789ef' .. INSTANCE_ID,
    replication_timeout = TIMEOUT,
    replication = {
        instance_uri(1);
        instance_uri(2);
    };
})

box.once("bootstrap", function()
    box.schema.user.grant('guest', 'replication')
end)
