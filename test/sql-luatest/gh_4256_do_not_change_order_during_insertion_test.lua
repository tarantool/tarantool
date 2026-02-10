local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end)
end)

g.after_each(function()
    g.server:drop()
end)

--
-- Make sure that when inserting, values are inserted in the given
-- order when ephemeral space is used.
--
g.test_gh4256_do_not_change_order_during_insertion = function()
    g.server:exec(function()
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
