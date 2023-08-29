local tarantool = require('tarantool')

local ffi = require('ffi')
ffi.cdef([[
    void *malloc(size_t size);
    void free(void *p);
]])
local malloc = ffi.C.malloc
local free = ffi.C.free

local t = require('luatest')
local g = t.group()

local ALLOC_SIZE = 10 * 1000 * 1000
local MARGIN = ALLOC_SIZE * 0.05

local function is_supported()
    -- malloc_info() is a GNU extension available only on Linux.
    if jit.os ~= 'Linux' then
        return false
    end
    -- If ASAN is enabled, malloc_info() exists but it is not implemented
    -- (all counters in the returned document are set to zeros).
    if tarantool.build.asan then
        return false
    end
    return true
end

local function skip_if_supported()
    t.skip_if(is_supported(), 'malloc info is supported')
end

local function skip_if_unsupported()
    t.skip_if(not is_supported(), 'malloc info is not supported')
end

local function check_malloc_info(info)
    t.assert_ge(info.used, 0)
    t.assert_ge(info.size, info.used)
    -- It's totally up to the malloc implementation whether to release memory
    -- to the system on free() immediately or keep it for future allocations.
    -- Still, it's reasonable to assume that a repetitive allocation and
    -- freeing of an object of the same size won't result in growing total
    -- memory usage infinitely. So we check that the system memory usage never
    -- exceeds the allocated memory usage by more than the test allocation size
    -- plus 10% overhead for internal housekeeping and fragmentation.
    t.assert_le(info.size, info.used + 1.1 * ALLOC_SIZE)
end

g.test_malloc_info = function()
    t.assert_type(box.malloc, 'table')
    t.assert_type(box.malloc.info, 'function')
    t.assert_type(box.malloc.info(), 'table')
    t.assert_type(box.malloc.internal, 'table')
    t.assert_type(box.malloc.internal.info, 'function')
    t.assert_type(box.malloc.internal.info(), 'table')
end

g.test_unsupported = function()
    skip_if_supported()

    t.assert_equals(box.malloc.info(), {size = 0, used = 0})
end

g.test_malloc_small = function()
    skip_if_unsupported()

    local p = {}
    local count = 10000
    local size = ALLOC_SIZE / count
    t.assert_ge(size, 100)

    local info1 = box.malloc.info()
    for i = 1, count do
        p[i] = malloc(size)
        t.assert_not_equals(p[i], nil)
    end
    local info2 = box.malloc.info()
    for i = 1, count, 2 do
        free(p[i])
    end
    local info3 = box.malloc.info()
    for i = 2, count, 2 do
        free(p[i])
    end
    local info4 = box.malloc.info()

    check_malloc_info(info1)
    check_malloc_info(info2)
    check_malloc_info(info3)
    check_malloc_info(info4)

    t.assert_almost_equals(info2.used - info1.used, ALLOC_SIZE, MARGIN)
    t.assert_almost_equals(info2.used - info3.used, ALLOC_SIZE / 2, MARGIN)
    t.assert_almost_equals(info4.used, info1.used, MARGIN)
end

g.test_malloc_huge = function()
    skip_if_unsupported()

    local info1 = box.malloc.info()
    local p = malloc(ALLOC_SIZE)
    t.assert_not_equals(p, nil)
    local info2 = box.malloc.info()
    free(p)
    local info3 = box.malloc.info()

    check_malloc_info(info1)
    check_malloc_info(info2)
    check_malloc_info(info3)

    t.assert_almost_equals(info2.used - info1.used, ALLOC_SIZE, MARGIN)
    t.assert_almost_equals(info3.used, info1.used, MARGIN)
end
