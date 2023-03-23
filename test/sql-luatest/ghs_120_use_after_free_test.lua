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

g.test_use_after_free = function()
    g.server:exec(function()
        local sql = [[WITH RECURSIVE t(x) AS
                      (VALUES('1' collate "binary") UNION ALL SELECT '2' FROM t
                       WHERE 'x' != '2' ORDER BY 1 collate "asd")
                      SELECT x FROM t;]]
        local ret, err = box.execute(sql)
        t.assert(ret == nil)
        local msg = [[Collation 'asd' does not exist]]
        t.assert_equals(err.message, msg)
    end)
end
