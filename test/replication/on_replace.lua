#!/usr/bin/env tarantool

-- get instance name from filename (on_replace1.lua => on_replace1)
local INSTANCE_ID = string.match(arg[0], "%d")
local USER = 'cluster'
local PASSWORD = 'somepassword'
local SOCKET_DIR = require('fio').cwd()

local TIMEOUT = tonumber(arg[1])

local function instance_uri(instance_id)
    --return 'localhost:'..(3310 + instance_id)
    return SOCKET_DIR..'/on_replace'..instance_id..'.sock';
end

-- start console first
require('console').listen(os.getenv('ADMIN'))
local env = require('test_run')
local test_run = env.new()
local engine = test_run:get_cfg('engine')

box.cfg({
    listen = instance_uri(INSTANCE_ID);
--    log_level = 7;
    replication = {
        USER..':'..PASSWORD..'@'..instance_uri(1);
        USER..':'..PASSWORD..'@'..instance_uri(2);
    };
    replication_timeout = TIMEOUT,
})

box.once("bootstrap", function()
    box.schema.user.create(USER, { password = PASSWORD })
    box.schema.user.grant(USER, 'replication')
    box.schema.space.create('test', {engine = engine})
    box.space.test:create_index('primary')
end)
