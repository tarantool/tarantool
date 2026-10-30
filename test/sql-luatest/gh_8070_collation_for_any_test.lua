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

g.test_collation_for_any = function()
    g.server:exec(function()
        local _, err = box.execute([[CREATE TABLE t(i INT PRIMARY KEY,
                                                    a ANY COLLATE "unicode");]])
        t.assert(err == nil)

        _, err = box.execute([[SELECT CAST(1 AS ANY) COLLATE "unicode_ci"]]);
        t.assert(err == nil)
    end)
end
