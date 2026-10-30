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
