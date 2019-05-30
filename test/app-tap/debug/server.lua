#!/usr/bin/env tarantool

box.cfg{
    listen = os.getenv('LISTEN')
}
require('console').listen(os.getenv('ADMIN'))

box.schema.func.create('debug.sourcefile')
box.schema.func.create('debug.sourcedir')
box.schema.user.grant('guest','execute','function','debug.sourcefile')
box.schema.user.grant('guest','execute','function','debug.sourcedir')
