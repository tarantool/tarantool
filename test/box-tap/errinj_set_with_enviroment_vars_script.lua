-- Script for box-tap/errinj_set_with_enviroment_vars.test.lua test.

local tap = require('tap')
local errinj = box.error.injection

local test = tap.test('set errinjs via environment variables')

test:plan(3)

test:test('Set boolean error injections', function(test)
    test:plan(6)
    test:is(errinj.get('ERRINJ_TESTING'), true, 'true')
    test:is(errinj.get('ERRINJ_WAL_IO'), true, 'True')
    test:is(errinj.get('ERRINJ_WAL_ROTATE'), true, 'TRUE')
    test:is(errinj.get('ERRINJ_WAL_WRITE'), false, 'false')
    test:is(errinj.get('ERRINJ_INDEX_ALLOC'), false, 'False')
    test:is(errinj.get('ERRINJ_WAL_WRITE_DISK'), false, 'FALSE')
end)

test:test('Set integer error injections', function(test)
    test:plan(3)
    test:is(errinj.get('ERRINJ_WAL_WRITE_PARTIAL'), 2, '2')
    test:is(errinj.get('ERRINJ_WAL_FALLOCATE'), 2, '+2')
    test:is(errinj.get('ERRINJ_VY_INDEX_DUMP'), -2, '-2')
end)

test:test('Set double error injections', function(test)
    test:plan(3)
    test:is(errinj.get('ERRINJ_VY_READ_PAGE_TIMEOUT'), 2.5, "2.5")
    test:is(errinj.get('ERRINJ_VY_SCHED_TIMEOUT'), 2.5, "+2.5")
    test:is(errinj.get('ERRINJ_RELAY_TIMEOUT'), -2.5, "-2.5")
end)

os.exit(test:check() and 0 or 1)
