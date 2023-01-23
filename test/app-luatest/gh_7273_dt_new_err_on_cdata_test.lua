local datetime = require('datetime')
local t = require('luatest')

local g = t.group()

g.test_dt_new_err_on_cdata = function()
    t.assert_error_msg_contains("bad timestamp ('number' expected, got 'cdata'", datetime.new, {timestamp = 1LL})
end
