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

g.test_ghs_80 = function()
    g.server:exec(function()
        local sql = [[select 'a' collate a union select 'b' collate "binary";]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, "Collation 'a' does not exist")
    end)
end

g.test_gh_9229 = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t(i INT, a INT, PRIMARY KEY(i, a),
                      b STRING UNIQUE COLLATE "unicode_ci");]]
        box.execute(sql)
        t.assert_equals(box.space.t.index[1].parts[1].collation, "unicode_ci")
        box.execute([[DROP TABLE t;]])
    end)
end
