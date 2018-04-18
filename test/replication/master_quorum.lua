#!/usr/bin/env tarantool

-- get instance name from filename (master_quorum1.lua => master_quorum1)
local INSTANCE_ID = string.match(arg[0], "%d")

local SOCKET_DIR = require('fio').cwd()
local function instance_uri(instance_id)
    --return 'localhost:'..(3310 + instance_id)
    return SOCKET_DIR..'/master_quorum'..instance_id..'.sock';
end

-- start console first
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = instance_uri(INSTANCE_ID);
--    log_level = 7;
    replication = {
        instance_uri(1);
        instance_uri(2);
    };
    replication_connect_quorum = 0;
    replication_connect_timeout = 0.1;
})

test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

box.once("bootstrap", function()
    box.schema.user.grant("guest", 'replication')
    box.schema.space.create('test', {engine = engine})
    box.space.test:create_index('primary')
end)
