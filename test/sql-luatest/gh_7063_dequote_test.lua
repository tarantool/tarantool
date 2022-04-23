local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'dequote'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_dequote = function()
    g.server:exec(function()
        local t = require('luatest')
        box.execute([[CREATE TABLE "t"("a" INT PRIMARY KEY);]])
        local sql = [[SELECT "t1"."a" FROM (SELECT "a" FROM "t") AS "t1";]]
        t.assert_equals(box.execute(sql).metadata[1].name, 't1.a')
        box.execute([[DROP TABLE "t";]])
    end)
end
