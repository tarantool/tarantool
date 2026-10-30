local t = require('luatest')

local g = t.group()

g.test_box_execute_before_box_cfg = function()
    t.assert_equals(rawget(box, 'execute'), nil)
end

g.test_sql_language_in_unconfigured_console = function()
    local res = [[
---
- error: Unable to set language to 'sql' with unconfigured box
...
]]
    t.assert_equals(require('console').eval('\\s l sql'), res)
end
