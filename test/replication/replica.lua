#!/usr/bin/env tarantool

local repl_include_self = arg[1] and arg[1] == 'true' or false
local repl_list

if repl_include_self then
    repl_list = {os.getenv("MASTER"), os.getenv("LISTEN")}
else
    repl_list = os.getenv("MASTER")
end

-- Start the console first to allow test-run to attach even before
-- box.cfg is finished.
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = repl_list,
    memtx_memory        = 107374182,
    replication_timeout = 0.1,
})
