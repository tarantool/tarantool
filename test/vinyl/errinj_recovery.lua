#!/usr/bin/env tarantool

box.error.injection.set('ERRINJ_VY_RUN_OPEN', 2)
assert(box.error.injection.get('ERRINJ_VY_RUN_OPEN'))

box.cfg {
    listen = os.getenv("LISTEN"),
}

require('console').listen(os.getenv('ADMIN'))
