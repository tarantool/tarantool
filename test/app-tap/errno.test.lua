#!/usr/bin/env tarantool

local tap = require('tap')
local errno = require('errno')

local test = tap.test("errno")

test:plan(1)
test:test("primary", function(test)
    test:plan(10)
    test:is(type(errno), "table", "type of table")
    test:ok(errno.EINVAL ~= nil, "errno.EINVAL is available")
    test:ok(errno.EBADF ~= nil , "errno.EBADF is available" )
    test:ok(errno(0) ~= nil, "errno set to 0")
    test:is(errno(errno.EBADF), 0, "setting errno.EBADF")
    test:is(errno(), errno.EBADF, "checking errno.EBADF")
    test:is(errno(errno.EINVAL), errno.EBADF, "setting errno.EINVAL")
    test:is(errno(), errno.EINVAL, "checking errno.EINVAL")
    test:is(errno.strerror(), "Invalid argument", "checking strerror without argument")
    test:is(errno.strerror(errno.EBADF), "Bad file descriptor", "checking strerror with argument")
end)
