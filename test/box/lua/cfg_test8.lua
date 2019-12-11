#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen = os.getenv("LISTEN"),
    read_only = true
}

require('console').listen(os.getenv('ADMIN'))
