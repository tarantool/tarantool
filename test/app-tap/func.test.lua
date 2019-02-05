#!/usr/bin/env tarantool

local tap = require('tap')

local test = tap.test("func")

test:plan(2)

-- gh-3770 Check no segfault with module_reload() without box.cfg{}.

test:ok(not pcall(box.internal.module_reload, ''),
        'expected error: no module')
test:ok(not pcall(box.internal.module_reload, 'xxx'),
        'expected error: no module')
