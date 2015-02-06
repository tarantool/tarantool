#!/usr/bin/env tarantool

--# setopt delimiter ';'

function on_gc(t)
end;

function test_finalizers()
    local result = {}
    local i = 1
    local ffi = require('ffi')
    while true do
        result[i] = ffi.gc(ffi.cast('void *', NULL), on_gc)
        i = i + 1
    end
    return "done"
end;

--# setopt delimiter ''

test_finalizers()
test_finalizers()

