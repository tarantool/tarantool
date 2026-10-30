#!/usr/bin/env tarantool

local ffi = require('ffi')
local tap = require('tap')
local test = tap.test('test hide symbols')

-- gh-5932: Tarantool should not exports symbols which don't specifieds in
-- "exports*", because a library should expose only its public API and should
-- not increase probability of hard to debug problems due to clash of a user's
-- code with an internal name from the library. For a more detailed description
-- see https://github.com/tarantool/tarantool/discussions/5733

-- The "small" symbols are not a part of the tarantool public API and should
-- be undefined. Let's check it.
ffi.cdef[[                                                                     \
void *                                                                         \
smalloc(struct small_alloc *alloc, size_t size);                               \
]]

test:plan(1)
local is_defined = pcall(function() return ffi.C.smalloc end)
test:ok(not is_defined, '"small" symbols undefined')

os.exit(test:check() and 0 or 1)
