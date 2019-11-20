#!/usr/bin/env tarantool

repl_include_self = arg[1] and arg[1] == 'true' or false
repl_list = nil

if repl_include_self then
    repl_list = {os.getenv("MASTER"), os.getenv("LISTEN")}
else
    repl_list = os.getenv("MASTER")
end

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = repl_list,
    memtx_memory        = 107374182,
    replication_timeout = 0.1,
})

require('console').listen(os.getenv('ADMIN'))
