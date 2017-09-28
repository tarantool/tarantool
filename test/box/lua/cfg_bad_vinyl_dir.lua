#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    vinyl_dir           = 'path/to/nowhere'
}

require('console').listen(os.getenv('ADMIN'))
