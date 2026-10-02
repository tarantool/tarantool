local t = require('luatest')
local server = require('luatest.server')

local g = t.group(nil, {
    {option = 'replication_connect_quorum'},
    {option = 'replication_sync_lag'},
    {option = 'txn_timeout'},
})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_config_updates_preserve_stack = function(cg)
    cg.server:exec(function(option)
        local ffi = require('ffi')
        ffi.cdef([[
            struct lua_State;
            struct lua_State *luaT_state(void);
            int lua_gettop(struct lua_State *L);
        ]])
        local L = ffi.C.luaT_state()
        local top = ffi.C.lua_gettop(L)
        local default = box.internal.default_cfg[option]
        local values = option == 'replication_connect_quorum' and
                       {1, 2} or {0.125, 1.5, box.NULL}
        for _ = 1, 3 do
            for _, value in ipairs(values) do
                box.cfg{[option] = value}
                t.assert_equals(ffi.C.lua_gettop(L), top)
                local expected = value
                if value == box.NULL then
                    expected = default
                end
                t.assert_equals(box.cfg[option], expected)
            end
        end
    end, {cg.params.option})
end
