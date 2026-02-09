local server = require('luatest.server')
local t = require('luatest')

local g = t.group("alter_table", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

--
-- Make sure there are no issues with index deletion after renaming the space.
-- Also, verify that index creation and deletion are being persistenly
-- preserved.
--
g.test_3613_idx_alter_update = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (s1 INT PRIMARY KEY);]])
        box.execute([[CREATE INDEX i ON t (s1);]])
        box.execute([[ALTER TABLE t RENAME TO j3;]])

        -- Due to gh-3613, next stmt caused segfault.
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

        -- Check that _index was altered properly.
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(function()
        local _, err = box.execute([[DROP INDEX i ON j3;]])
        t.assert_equals(err, nil)

        -- Cleanup.
        box.execute([[DROP TABLE j3;]])
    end)
end

--
-- After gh-3613 fix, bug in cmp_def was discovered.
-- Comparison didn't take .opts.sql into account
-- marking corresponding xlog entry useless.
--
g.test_3613_idx_alter_update_2 = function(cg)
    cg.server:exec(function()
        box.execute('CREATE TABLE t (s1 INT PRIMARY KEY);')
        box.execute('CREATE INDEX i ON t (s1);')
        local res = box.execute('ALTER TABLE t RENAME TO j3;')
        t.assert_equals(res, {row_count = 0})
    end)
    cg.server:restart()
    cg.server:exec(function()
        box.execute('DROP INDEX i ON j3;')
        -- Cleanup.
        box.execute('DROP TABLE j3;')
    end)
end
