#!/usr/bin/env tarantool

box.cfg{
    listen = os.getenv("LISTEN"),
}

require('console').listen(os.getenv('ADMIN'))
