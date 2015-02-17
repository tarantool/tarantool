#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
}

require('console').listen(os.getenv('ADMIN'))
