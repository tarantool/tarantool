#!/usr/bin/env tarantool

-- get instance name from filename (replica_uuid_ro1.lua => replica_uuid_ro1)
local INSTANCE_ID = string.match(arg[0], "%d")
local USER = 'cluster'
local PASSWORD = 'somepassword'
local SOCKET_DIR = require('fio').cwd()
local function instance_uri(instance_id)
    --return 'localhost:'..(3310 + instance_id)
    return SOCKET_DIR..'/replica_uuid_ro'..instance_id..'.sock';
end

-- start console first
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    instance_uuid = arg[1];
    listen = instance_uri(INSTANCE_ID);
--    log_level = 7;
    replication = {
        USER..':'..PASSWORD..'@'..instance_uri(1);
        USER..':'..PASSWORD..'@'..instance_uri(2);
    };
    read_only = (INSTANCE_ID ~= '1' and true or false);
})

box.once("bootstrap", function()
    local test_run = require('test_run').new()
    box.schema.user.create(USER, { password = PASSWORD })
    box.schema.user.grant(USER, 'replication')
    box.schema.space.create('test', {engine = test_run:get_cfg('engine')})
    box.space.test:create_index('primary')
end)
