local server = require('luatest.server')
local t = require('luatest')

local g = t.group("insert", {{engine = 'memtx'}, {engine = 'vinyl'}})

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
-- Make sure that when inserting, values are inserted in the given
-- order when ephemeral space is used.
--
g.test_4256_do_not_change_order_during_insertion = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY AUTOINCREMENT);]])

        -- In order for this INSERT to use the ephemeral space, we created
        -- this trigger.
        box.execute([[CREATE TRIGGER r AFTER INSERT ON t
                      FOR EACH ROW BEGIN SELECT 1; END]])
        local res = box.execute([[INSERT INTO t VALUES (1), (NULL), (10),
                                  (NULL), (NULL), (3), (NULL);]])
        local exp = {
            autoincrement_ids = {2, 11, 12, 13},
            row_count = 7,
        }
        t.assert_equals(res, exp)
        res = box.execute([[SELECT * FROM SEQSCAN t;]])
        t.assert_equals(res.rows, {{1}, {2}, {3}, {10}, {11}, {12}, {13}})

        box.execute([[DROP TABLE t;]])
    end)
end
