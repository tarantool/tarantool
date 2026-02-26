local server = require('luatest.server')
local t = require('luatest')

local g = t.group("select", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- This test checks the correct handling of NULL values in SQL queries.
--
g.test_select_null = function(cg)
    cg.server:exec(function()
        -- Create space.
        local sql = [[CREATE TABLE t3
                      (id INT, a TEXT, b TEXT, PRIMARY KEY(id));]]
        box.execute(sql)

        -- Seed entries.
        box.execute([[INSERT INTO t3 VALUES (1, 'abc', NULL);]]);
        box.execute([[INSERT INTO t3 VALUES (2, NULL, 'xyz');]]);

        -- Select must return properly decoded `NULL`.
        local exp = {
            {1, 'abc', nil},
            {2, nil, 'xyz'},
        }
        local res = box.execute([[SELECT * FROM t3;]])
        t.assert_equals(res.rows, exp)

        -- Clean up.
        box.execute([[DROP TABLE t3;]])
    end)
end
