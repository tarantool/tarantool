#!/usr/bin/env tarantool

function on_gc(t)
end;

function test_finalizers()
    local result = {}
    local i = 1
    local ffi = require('ffi')
    while true do
        result[i] = ffi.gc(ffi.cast('void *', 0), on_gc)
        i = i + 1
    end
    return "done"
end;

test_finalizers()
test_finalizers()

