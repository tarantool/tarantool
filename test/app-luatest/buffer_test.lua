local tarantool = require('tarantool')
local buffer = require('buffer')
local utils = require('internal.utils')
local ffi = require('ffi')
local t = require('luatest')

local g = t.group()

g.test_poison = function()
    t.skip_if(not tarantool.build.asan, 'only make sense with ASAN')

    local buf = buffer.ibuf()
    local is_poisoned = function(buf, ptr)
        t.assert(utils.memory_region_is_poisoned(ptr, buf:unused()))
    end

    -- Test poison on allocation. --
    local ptr = buf:alloc(99)
    t.assert(ptr ~= nil)
    ffi.fill(ptr, 99)
    is_poisoned(buf, ptr + 99)

    t.assert(buf:unused() > 133)
    ptr = buf:alloc(133)
    t.assert(ptr ~= nil)
    ffi.fill(ptr, 133)
    is_poisoned(buf, ptr + 133)

    local size = buf:unused() + 77
    ptr = buf:alloc(size)
    t.assert(ptr ~= nil)
    ffi.fill(ptr, size)
    t.assert_gt(buf:unused(), 0)
    is_poisoned(buf, ptr + size)

    -- Test poison after reset. --
    buf:reset()
    ptr = buf:alloc(0)
    is_poisoned(buf, ptr)

    -- Test poison on reserve. --
    ptr = buf:reserve(777)
    t.assert(ptr ~= nil)
    t.assert_ge(buf:unused(), 777)
    ffi.fill(ptr, buf:unused())
    ptr = buf:alloc(333)
    t.assert(ptr ~= nil)
    ffi.fill(ptr, 333)
    is_poisoned(buf, ptr + 333)

    t.assert_gt(buf:unused(), 888)
    ptr = buf:reserve(333)
    t.assert(ptr ~= nil)
    t.assert_ge(buf:unused(), 333)
    ffi.fill(ptr, buf:unused())
    ptr = buf:alloc(888)
    t.assert(ptr ~= nil)
    ffi.fill(ptr, 888)
    is_poisoned(buf, ptr + 888)

    size = buf:unused() + 133;
    ptr = buf:reserve(size)
    t.assert(ptr ~= nil)
    t.assert_ge(buf:unused(), size)
    ffi.fill(ptr, buf:unused())
    t.assert_gt(buf:unused(), 0)
    ptr = buf:alloc(size)
    t.assert(ptr ~= nil)
    t.assert_gt(buf:unused(), 0)
    is_poisoned(buf, ptr + size)

    size = buf:unused(buf) + 221;
    buf:read(337)
    ptr = buf:reserve(size)
    t.assert(ptr ~= nil)
    t.assert_ge(buf:unused(), size)
    ffi.fill(ptr, buf:unused())
    ptr = buf:alloc(size)
    t.assert_gt(buf:unused(), 0)
    is_poisoned(buf, ptr + size)
end
