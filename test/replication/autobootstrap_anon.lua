#!/usr/bin/env tarantool

local INSTANCE_ID = string.match(arg[0], "%d")
local SOCKET_DIR = require('fio').cwd()
local ANON = arg[1] == 'true'

local function instance_uri(instance_id)
    return SOCKET_DIR..'/autobootstrap'..instance_id..'.sock';
end

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = instance_uri(INSTANCE_ID),
    replication = {
        instance_uri(1),
        instance_uri(2),
    };
    replication_anon = ANON,
    read_only = ANON,
})

box.once("bootstrap", function()
    box.schema.user.grant('guest', 'super')
end)
