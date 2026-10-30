local t = require('luatest')

local g = t.group()

-- Check that fiber_join_timeout is accessible from ffi.
g.test_ffi_fiber_join_timeout = function()
    local ffi = require('ffi')
    local fiber = require('fiber')
    ffi.cdef([[
        struct fiber;
        struct fiber * fiber_self(void);
        int fiber_join_timeout(struct fiber *f, double timeout);
    ]])
    local fiber_ptr = nil
    fiber.create(function()
        fiber_ptr = ffi.C.fiber_self()
        fiber.self():set_joinable(true)
    end)
    t.assert_equals(ffi.C.fiber_join_timeout(fiber_ptr, 100500), 0)
end
