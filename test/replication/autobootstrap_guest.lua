#!/usr/bin/env tarantool

-- get instance name from filename (autobootstrap_guest1.lua => autobootstrap_guest1)
local INSTANCE_ID = string.match(arg[0], "%d")

local SOCKET_DIR = require('fio').cwd()

local TIMEOUT = tonumber(arg[1])

local function instance_uri(instance_id)
    --return 'localhost:'..(3310 + instance_id)
    return SOCKET_DIR..'/autobootstrap_guest'..instance_id..'.sock';
end

-- start console first
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = instance_uri(INSTANCE_ID);
--    log_level = 7;
    replication = {
        instance_uri(1);
        instance_uri(2);
        instance_uri(3);
    };
    replication_timeout = TIMEOUT;
})

box.once("bootstrap", function()
    local test_run = require('test_run').new()
    box.schema.user.grant("guest", 'replication')
    box.schema.space.create('test', {engine = test_run:get_cfg('engine')})
    box.space.test:create_index('primary')
end)
