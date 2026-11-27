#!/usr/bin/env tarantool

-- get instance id from filename (er_load1.lua => 1)
local INSTANCE_ID = string.match(arg[0], '%d')

local SOCKET_DIR =  require('fio').cwd()
local function instance_uri(instance_id)
    return SOCKET_DIR..'/er_load'..instance_id..'.sock'
end

require('console').listen(os.getenv('ADMIN'))

if INSTANCE_ID == '2' then
    -- Fail during join, after the master has connected to the replica.
    box.ctl.on_schema_init(function()
        box.space._cluster:before_replace(function()
            error('bootstrap failure')
        end)
    end)
end

box.cfg{
    listen = instance_uri(INSTANCE_ID);
    replication = {
        instance_uri(INSTANCE_ID),
        instance_uri(INSTANCE_ID % 2 + 1)
    },
    replication_timeout = 0.01,
    read_only = INSTANCE_ID == '2'
}
box.once('bootstrap', function()
    box.schema.user.grant('guest', 'replication')
end)
