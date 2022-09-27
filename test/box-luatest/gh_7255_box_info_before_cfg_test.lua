local t = require('luatest')

local g = t.group()

g.test_box_info_before_box_cfg = function()
    local info = box.info()
    t.assert_equals(info.status, "unconfigured")
end
