local t = require('luatest')
local server = require('luatest.server')

local g = t.group('gh-10378')

g.before_all(function()
    g.server = server:new{alias = 'default'}
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

-- Test 4 more symbols
g.test_new_symbols = function()
    g.server:exec(function()
        local ffi = require('ffi');
        local fiber = require('fiber');
        ffi.cdef([[
            int64_t
            box_info_lsn(void);

            bool
            box_is_ro(void);

            const char*
            box_ro_reason(void);

            int
            box_wait_ro(bool ro, double timeout);
        ]])

        t.assert_equals(box.info.lsn, ffi.C.box_info_lsn())
        t.assert_equals(box.info.ro, ffi.C.box_is_ro())
        box.cfg{read_only = true}
        t.assert_equals(box.info.ro, ffi.C.box_is_ro())
        t.assert_equals(box.info.ro_reason, ffi.string(ffi.C.box_ro_reason()))

        local state = ''
        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            state = 'wait started'
            ffi.C.box_wait_ro(false, 100500)
            state = 'wait finished'
        end)

        t.assert_equals(state, 'wait started')
        box.cfg{read_only = false}
        f:join()
        t.assert_equals(state, 'wait finished')
    end)
end
