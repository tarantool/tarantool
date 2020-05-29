#!/usr/bin/env tarantool

local function on_gc()
end;

local function test_finalizers()
    local result = {}
    local i = 1
    local ffi = require('ffi')
    while i ~= 0 do
        result[i] = ffi.gc(ffi.cast('void *', 0), on_gc)
        i = i + 1
    end
    -- Fake-read 'result' to calm down 'luacheck' complaining that the variable
    -- is never used.
    assert(#result ~= 0)
    return "done"
end;

test_finalizers()
test_finalizers()
