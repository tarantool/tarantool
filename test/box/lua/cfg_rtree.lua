#!/usr/bin/env tarantool
local os = require('os')
box.error.injection.set("ERRINJ_INDEX_RESERVE", true)
box.cfg{
    listen              = os.getenv("LISTEN"),
}
require('console').listen(os.getenv('ADMIN'))
box.schema.user.grant('guest', 'read,write,execute', 'universe')
