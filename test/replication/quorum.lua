#!/usr/bin/env tarantool

-- see autobootstrap_guest.lua
local USER = 'cluster'
local PASSWORD = 'somepassword'
local SOCKET_DIR = require('fio').cwd()
local function instance_uri(instance_id)
    return SOCKET_DIR..'/autobootstrap_guest'..instance_id..'.sock';
end

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    replication_connect_quorum = 2;
    replication_timeout = 0.05;
    replication = {
        instance_uri(1);
        instance_uri(2);
        instance_uri(3);
    };
})
