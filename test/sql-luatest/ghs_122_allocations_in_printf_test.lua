local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_printf = function()
    g.server:exec(function()
        local msg = [[Failed to execute SQL statement: string or blob too big]]

        local ret, err = box.execute([[SELECT printf('%.*d', 0x7ffffff0, 0);]])
        t.assert(ret == nil)
        t.assert_equals(err.message, msg)

        ret, err = box.execute("SELECT printf('hello %.*d', 0x7fffffff, 0);")
        t.assert(ret == nil)
        t.assert_equals(err.message, msg)
    end)
end
