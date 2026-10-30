local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'gh_7992'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_ignore_collate_for_builtins = function()
    g.server:exec(function()
        local res = box.execute([[select ABS(NULL collate "unicode_ci");]])
        t.assert_equals(res.rows, {{box.NULL}})
    end)
end
