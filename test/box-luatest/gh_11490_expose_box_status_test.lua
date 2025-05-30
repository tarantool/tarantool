local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(g)
    g.server = server:new()
    g.server:start()
end)

g.test_expose_box_status = function(g)
    g.server:exec(function()
        local ffi = require('ffi')
        ffi.cdef([[
            const char * box_status(void);
        ]])
        local box_status_capi = ffi.string(ffi.C.box_status())
        t.assert_equals(box.info.status, box_status_capi)
    end)
end

g.after_all(function(g)
    g.server:drop()
end)
