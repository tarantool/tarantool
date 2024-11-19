local alloc = require('internal.alloc')

local t = require('luatest')
local g = t.group()

g.test_invalid_arg = function()
    local errmsg
    local function test(...)
        t.assert_error_msg_equals(errmsg, alloc.setlimit, ...)
    end

    errmsg = 'Usage: alloc.setlimit(amount)'
    test()

    errmsg = 'bad argument #1 to \'?\' (number expected, got ' ..
             'string)'
    test('foo')

    errmsg = 'Invalid memory limit: the value must be >= 0'
    test(-1024)

    errmsg = 'Cannot limit the Lua memory with values less ' ..
             'than the currently allocated amount'
    test(16)
end

g.test_basic = function()
    local function test(amount)
        local old_memory_limit = alloc.getlimit()
        t.assert_equals(alloc.setlimit(amount), old_memory_limit)
        t.assert_equals(alloc.getlimit(), amount)
    end

    test(256 * 1024 * 1024)
    test(1024 * 1024 * 1024)
    test(2 * 1024 * 1024 * 1024)
    test(4 * 1024 * 1024 * 1024)
    test(8 * 1024 * 1024 * 1024)
end

g.test_used_and_unused = function()
    local function test(amount)
        alloc.setlimit(amount)
        t.assert_equals(collectgarbage('count'), alloc.used() / 1024)
        t.assert_equals(amount / 1024 - collectgarbage('count'),
                        alloc.unused() / 1024)
        t.assert_equals(alloc.used() + alloc.unused(), alloc.getlimit())
    end

    test(256 * 1024 * 1024)
    test(1024 * 1024 * 1024)
    test(2 * 1024 * 1024 * 1024)
    test(4 * 1024 * 1024 * 1024)
    test(8 * 1024 * 1024 * 1024)
end
