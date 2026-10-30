#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

local memtx_use_mvcc_engine = (arg[2] and arg[2] == 'true' and true or false)

box.cfg({
    listen = os.getenv('LISTEN'),
    iproto_threads = tonumber(arg[1]),
    memtx_use_mvcc_engine = memtx_use_mvcc_engine
})

box.schema.user.grant('guest', 'read,write,execute,create,drop', 'universe',  nil, {if_not_exists = true})
