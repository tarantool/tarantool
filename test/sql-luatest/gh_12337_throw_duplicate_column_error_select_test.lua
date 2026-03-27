local server = require('luatest.server')
local t = require('luatest')

local g = t.group("gh", {{engine = 'memtx'}, {engine = 'vinyl'}})

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
    cg.server:exec(function(engine)
        local res, err = box.execute([[SELECT * FROM (SELECT 1 as a, 2 as a, 3 as c);]])
        local exp_err = "ambiguous column name: a"
        t.assert_equals(tostring(err), exp_err)

        res, err = box.execute([[SELECT a FROM (SELECT 1 as a, 2 as a, 3 as c);]])
        t.assert_equals(tostring(err), exp_err)

        res = box.execute([[SELECT c FROM (SELECT 1 as a, 2 as a, 3 as c);]])
        t.assert_equals(res.rows, {{3}})
    end, {cg.params.engine})
end