local tap = require('tap')

local test = tap.test('lj-695-ffi-vararg-call')
test:plan(2)

local ffi = require('ffi')
local str = ffi.new('char[256]')
ffi.cdef('int sprintf(char *str, const char *format, ...)')
local strlen = ffi.C.sprintf(str, 'try vararg function: %s:%.2f(%d) - %llu',
                             'imun', 9, 9LL, -1ULL)

local result = ffi.string(str)
test:is(#result, strlen)
test:is(result, 'try vararg function: imun:9.00(9) - 18446744073709551615')

test:done(true)
