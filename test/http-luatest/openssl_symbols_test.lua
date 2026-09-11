local t = require('luatest')
local ffi = require('ffi')

local g = t.group()

g.test_tls_server_method = function()
    local method = ffi.C.TLS_server_method()
    t.assert_not_equals(method, nil)
end
