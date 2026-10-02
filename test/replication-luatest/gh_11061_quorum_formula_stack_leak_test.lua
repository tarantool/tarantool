local t = require('luatest')
local server = require('luatest.server')

local g = t.group(nil, {
    {option = 'replication_synchro_quorum'},
    {option = 'replication_linearizable_quorum'},
})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_formula_errors = function(cg)
    cg.server:exec(function(option)
        local ffi = require('ffi')
        ffi.cdef([[
            struct lua_State;
            struct lua_State *luaT_state(void);
            int lua_gettop(struct lua_State *L);
        ]])
        local L = ffi.C.luaT_state()
        local top = ffi.C.lua_gettop(L)
        for _, formula in ipairs({'(', 'missing + 1', '"not a number"'}) do
            for _ = 1, 3 do
                local ok, err = pcall(box.cfg, {[option] = formula})
                t.assert_not(ok)
                t.assert_equals(err.code, box.error.CFG)
                t.assert_str_contains(err.message, option)
                t.assert_equals(ffi.C.lua_gettop(L), top)
            end
        end
        local original = box.cfg[option]
        box.cfg{[option] = 'N'}
        t.assert_equals(ffi.C.lua_gettop(L), top)
        box.cfg{[option] = original}
        t.assert_equals(ffi.C.lua_gettop(L), top)
    end, {cg.params.option})
end
