local ffi = require('ffi')
local t = require('luatest')
local g = t.group()

g.before_all(function()
    ffi.cdef([[
        void *
        tnt_internal_symbol(const char *name);
    ]])
end)

g.test_fiber_channel = function()
    local symbols = {
        'fiber_channel_close',
        'fiber_channel_create',
        'fiber_channel_delete',
        'fiber_channel_destroy',
        'fiber_channel_get_msg_timeout',
        'fiber_channel_get_timeout',
        'fiber_channel_has_readers',
        'fiber_channel_has_writers',
        'fiber_channel_new',
        'fiber_channel_put_msg_timeout',
        'fiber_channel_put_timeout',
        'ipc_value_delete',
        'ipc_value_new',
        'fiber_lua_state',
    }
    -- Verify only that we can get the address. The functions are
    -- provided 'as is' and we don't guarantee anything regarding
    -- them.
    for _, name in ipairs(symbols) do
        local addr = ffi.C.tnt_internal_symbol(name)
        t.assert_not_equals(addr, nil)
    end
end

g.test_non_existing_symbol = function()
    local addr = ffi.C.tnt_internal_symbol('non_existing_symbol')
    t.assert_equals(addr, nil)
end
