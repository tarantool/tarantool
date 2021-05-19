#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

local iproto_threads = arg[1]
if iproto_threads == "negative" then
	iproto_threads = -1
end

box.cfg({
    listen = os.getenv('LISTEN'),
    iproto_threads = tonumber(iproto_threads),
    wal_mode = 'none'
})

box.schema.user.grant('guest', 'read,write,execute,create,drop', 'universe', nil, {if_not_exists = true})
function errinj_set(thread_id)
    if thread_id ~= nil then
        box.error.injection.set("ERRINJ_IPROTO_SINGLE_THREAD_STAT", thread_id)
    end
end
function ping() return "pong" end
