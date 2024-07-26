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

g.test_exists = function()
    g.server:exec(function()
        local varbinary = require('varbinary')
        local val = varbinary.new('asd')
        t.assert_equals(box.execute([[SELECT ?;]], {val}).rows[1][1], val)

        val = box.execute([[SELECT RANDOMBLOB(10);]]).rows[1][1]
        t.assert(varbinary.is(val))
        t.assert_equals(box.execute([[SELECT ?;]], {val}).rows[1][1], val)
    end)
end
