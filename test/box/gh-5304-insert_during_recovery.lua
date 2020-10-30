#!/usr/bin/env tarantool

function none(old_space, new_space)
end

function trigger_replace(old_space, new_space)
    box.space.temp:replace({1})
    box.space.loc:replace({1})
end

function trigger_insert(old_space, new_space)
    box.space.temp:insert({1})
    box.space.loc:insert({1})
end

function trigger_upsert(old_space, new_space)
    box.space.temp:upsert({1}, {{'=', 1, 4}})
    box.space.loc:upsert({1}, {{'=', 1, 4}})
end

trigger = nil

if arg[1] == 'none' then
    trigger = none
elseif arg[1] == 'replace' then
    trigger = trigger_replace
elseif arg[1] == 'insert' then
    trigger = trigger_insert
elseif arg[1] == 'upsert' then
    trigger = trigger_upsert
end

if arg[2] == 'is_recovery_finished' then
    box.ctl.on_schema_init(function()
        if box.ctl.is_recovery_finished() then
            box.space._user:on_replace(trigger)
        end
    end)
else
    box.ctl.on_schema_init(function()
        box.space._user:on_replace(trigger)
    end)
end

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = os.getenv("LISTEN"),
})
