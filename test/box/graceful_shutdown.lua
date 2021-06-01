#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    iproto_threads      = 10,
}
box.schema.user.grant("guest", "read,write,execute,create,drop", "universe", nil, {if_not_exists = true})
box.ctl.set_on_shutdown_timeout(3)
require('console').listen(os.getenv('ADMIN'))
