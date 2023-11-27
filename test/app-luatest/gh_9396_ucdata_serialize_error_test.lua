local t = require('luatest')
local g = t.group()

-- XXX: Use `yaml.encode()` to trigger a serialization error.
local yaml = require('yaml')

local ERRMSG = 'error in serializer'

g.test_error_in_ucdata_serializer = function()
    local ud_mt = {__serialize = function()
        error(ERRMSG)
    end}
    ud_mt.__index = ud_mt
    local ud = newproxy(true)
    debug.setmetatable(ud, ud_mt)
    -- XXX: This helper fails if there is no error raised.
    t.assert_error_msg_matches('.*' .. ERRMSG, yaml.encode, ud)
end
