local server = require('luatest.server')
local t = require('luatest')

local g = t.group("Persistent GC consumers on old schema")

g.before_all(function(cg)
    cg.server = server:new({datadir = 'test/box-luatest/upgrade/2.11.0'})
    cg.server:start()
    cg.server:exec(function()
        local ffi = require('ffi')
        ffi.cdef([[
            int box_schema_upgrade_begin(void);
            void box_schema_upgrade_end(void);
        ]])
        rawset(_G, 'builtins', ffi.C)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_no_crash_when_instance_is_deleted = function(cg)
    cg.server:exec(function()
        _G.builtins.box_schema_upgrade_begin()
        box.space._cluster:insert{2, require('uuid').str()}
        -- Before the patch it's segfault after this line.
        box.space._cluster:delete(2)
        _G.builtins.box_schema_upgrade_end()
    end)
end
