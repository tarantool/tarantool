local server = require('luatest.server')
local t = require('luatest')

local g = t.group("restart", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_3613_idx_alter_update = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (s1 INT PRIMARY KEY);]])
        box.execute([[CREATE INDEX i ON t (s1);]])
        box.execute([[ALTER TABLE t RENAME TO j3;]])

        -- Due to gh-3613, next stmt caused segfault
        box.execute([[DROP INDEX i ON j3;]])

        -- Make sure that no artifacts remain after restart.
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(function()
        local exp_err = [[No index 'i' is defined in space 'j3']]
        local _, err = box.execute([[DROP INDEX i ON j3;]])
        t.assert_equals(tostring(err), exp_err)

        box.execute([[CREATE INDEX i ON j3 (s1);]])

        -- Check that _index was altered properly
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(function()
        local _, err = box.execute([[DROP INDEX i ON j3;]])
        t.assert_equals(err, nil)

        -- Cleanup
        box.execute([[DROP TABLE j3;]])
    end)
end
