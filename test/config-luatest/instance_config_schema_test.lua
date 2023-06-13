local t = require('luatest')
local instance_config = require('internal.config.instance_config')

local g = t.group()

g.test_general = function()
    t.assert_equals(instance_config.name, 'instance_config')
end
