local server = require('luatest.server')
local t = require('luatest')

local g = t.group("alter", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- After gh-3613 fix, bug in cmp_def was discovered.
-- Comparison didn't take .opts.sql into account
-- marking corresponding xlog entry useless
--
g.test_3613_idx_alter_update_2 = function(cg)
    cg.server:exec(function()
        box.execute('CREATE TABLE t (s1 INT PRIMARY KEY)')
        box.execute('CREATE INDEX i ON t (s1)')
        local res = box.execute('ALTER TABLE t RENAME TO j3')
        t.assert_equals(res, {row_count = 0})
    end)
    cg.server:restart()
    cg.server:exec(function()
        box.execute('DROP INDEX i ON j3')
        -- Cleanup
        box.execute('DROP TABLE j3')
    end)
end
