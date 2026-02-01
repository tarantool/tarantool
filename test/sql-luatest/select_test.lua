local server = require('luatest.server')
local t = require('luatest')

local g = t.group("dup")

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- No duplicate column error for a select
g.test_12337_throw_duplicate_from_select = function(cg)
    cg.server:exec(function()
        local sql = [[SELECT * FROM (SELECT 1 as a, 2 as a, 3 as c);]]
        local _, err = box.execute(sql)
        local exp_err = "ambiguous column name: a"
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a FROM (SELECT 1 as a, 2 as a, 3 as c);]])
        t.assert_equals(tostring(err), exp_err)

        sql = [[SELECT c FROM (SELECT 1 as a, 2 as a, 3 as c);]]
        local res = box.execute(sql)
        t.assert_equals(res.rows, {{3}})

        _, err = box.execute([[SELECT a FROM (SELECT 1 as A, 2 as A, 3 as c);]])
        t.assert_equals(tostring(err), exp_err)

        res = box.execute([[SELECT * FROM (SELECT 1 as a, 2 as A, 3 as c);]])
        t.assert_equals(res.rows, {{1, 2, 3}})
    end)
end
