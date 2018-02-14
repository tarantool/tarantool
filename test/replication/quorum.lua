#!/usr/bin/env tarantool

-- get instance name from filename (quorum1.lua => quorum1)
local INSTANCE_ID = string.match(arg[0], "%d")

local SOCKET_DIR = require('fio').cwd()
local function instance_uri(instance_id)
    --return 'localhost:'..(3310 + instance_id)
    return SOCKET_DIR..'/quorum'..instance_id..'.sock';
end

-- start console first
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = instance_uri(INSTANCE_ID);
    replication_timeout = 0.05;
    replication_sync_lag = 0.01;
    replication_connect_timeout = 0.1;
    replication_connect_quorum = 3;
    replication = {
        instance_uri(1);
        instance_uri(2);
        instance_uri(3);
    };
})

box.once("bootstrap", function()
    local test_run = require('test_run').new()
    box.schema.user.grant("guest", 'replication')
    box.schema.space.create('test', {engine = test_run:get_cfg('engine')})
    box.space.test:create_index('primary')
end)
