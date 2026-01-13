local server = require('luatest.server')
local t = require('luatest')

local g = t.group("duplicate", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-4545: No duplicate column error for a view
--
g.test_duplicate_column_error = function(cg)
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_default_engine" = '%s']], {engine})
        local exp_err = [[Space field 'a' is duplicate]]
        local sql = [[CREATE TABLE t (a INTEGER PRIMARY KEY, a INTEGER);]]
        local exp, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        sql = [[CREATE VIEW v AS SELECT 1 AS a, 1 AS a;]]
        exp, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)
    end, {cg.params.engine})
end
