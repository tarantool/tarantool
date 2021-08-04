#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

local iproto_threads = 1
if arg[1] then
    iproto_threads = tonumber(arg[1])
end

box.cfg({
    listen = os.getenv('LISTEN'),
    iproto_threads = iproto_threads,
})

box.schema.user.grant("guest", "read,write,execute,create,drop", "universe", nil, {if_not_exists = true})
