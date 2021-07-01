#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = os.getenv('LISTEN'),
    iproto_threads = tonumber(arg[1]),
})

box.schema.user.grant('guest', 'read,write,execute,create,drop', 'universe',  nil, {if_not_exists = true})