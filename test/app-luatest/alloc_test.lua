local alloc = require('internal.alloc')

local t = require('luatest')
local g = t.group()

g.test_invalid_arg = function()
    local errmsg
    local function test(...)
        t.assert_error_msg_equals(errmsg, alloc.setmemory, ...)
    end

    errmsg = 'Usage: alloc.setmemory(amount)'
    test()

    errmsg = 'Invalid memory amount: the value must be >= 0'
    test(-1024)

    errmsg = 'Cannot limit the Lua memory with values less than 256MB'
    test(1024)
    test(1024 * 1024)
    test(256 * 1024 * 1024 - 1)
end

g.test_setmemory = function()
    local function test(amount)
        t.assert_equals(alloc.setmemory(amount), amount)
    end

    test(256 * 1024 * 1024)
    test(512 * 1024 * 1024)
    test(1024 * 1024 * 1024)
end
