local server = require('luatest.server')
local t = require('luatest')

local g = t.group("view", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:drop()
end)

--
-- gh-4545: Make sure that creating a view with columns that have
-- the same name will result in an error.
--
g.test_duplicate_column_error = function(cg)
    cg.server:exec(function()
        local exp_err = [[Space field 'a' is duplicate]]
        local sql = [[CREATE TABLE t (a INTEGER PRIMARY KEY, a INTEGER);]]
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        sql = [[CREATE VIEW v AS SELECT 1 AS a, 1 AS a;]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)
    end)
end
