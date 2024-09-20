#!/usr/bin/env tarantool

box.error.injection.set('ERRINJ_VY_RUN_RECOVER_COUNTDOWN', 2)
assert(box.error.injection.get('ERRINJ_VY_RUN_RECOVER_COUNTDOWN'))

box.cfg {
    listen = os.getenv("LISTEN"),
}

require('console').listen(os.getenv('ADMIN'))
