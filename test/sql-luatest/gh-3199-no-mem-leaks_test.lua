local server = require('luatest.server')
local t = require('luatest')

local g = t.group("restart", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- This test checks that no leaks of region memory happens during
-- executing SQL queries.
g.test_3199_no_mem_leaks = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local exp_memory = fiber.info()[fiber.self().id()].memory.used

        local sql = [[CREATE TABLE test
                      (id INT PRIMARY KEY, x INTEGER, y INTEGER);]]
        box.execute(sql)
        box.execute([[INSERT INTO test VALUES (1, 1, 1), (2, 2, 2);]])
        local exp = {{1, 1, 2}, {2, 2, 4}}
        sql = [[SELECT x, y, x + y FROM test ORDER BY y;]]
        local res, err = box.execute(sql)
        t.assert_equals(err, nil)
        t.assert_equals(res.rows, exp)

        t.assert_equals(fiber.info()[fiber.self().id()].memory.used, exp_memory)

        sql = [[SELECT x, y, x + y FROM test ORDER BY y;]]
        t.assert_equals(box.execute(sql).rows, exp)
        t.assert_equals(box.execute(sql).rows, exp)
        t.assert_equals(box.execute(sql).rows, exp)
        t.assert_equals(box.execute(sql).rows, exp)

        t.assert_equals(fiber.info()[fiber.self().id()].memory.used, exp_memory)

        sql = [[CREATE TABLE test2 (id INT PRIMARY KEY, a TEXT, b INTEGER);]]
        box.execute(sql)
        sql = [[INSERT INTO test2 VALUES (1, 'abc', 1), (2, 'hello', 2);]]
        box.execute(sql)
        box.execute([[INSERT INTO test2 VALUES (3, 'test', 3), (4, 'xx', 4);]])
        exp = {
            {'abc', 3, 1},
            {'hello', 4, 2},
            {'test', 5, 3},
            {'xx', 6, 4},
        }
        sql = [[SELECT a, id + 2, b FROM test2 WHERE b < id * 2 ORDER BY a;]]
        t.assert_equals(box.execute(sql).rows, exp)

        t.assert_equals(fiber.info()[fiber.self().id()].memory.used, exp_memory)

        exp = {
            {'abc', 3, 'abc'},
            {'hello', 6, 'hello'},
            {'test', 9, 'test'},
            {'xx', 12, 'xx'},
        }
        sql = [[SELECT a, id + 2 * b, a FROM
                test2 WHERE b < id * 2 ORDER BY a;]]
        t.assert_equals(box.execute(sql).rows, exp)
        t.assert_equals(box.execute(sql).rows, exp)
        t.assert_equals(box.execute(sql).rows, exp)

        t.assert_equals(fiber.info()[fiber.self().id()].memory.used, exp_memory)

        exp = {
            {1, 4, 1},
            {2, 8, 2},
        }
        sql = [[SELECT x, y + 3 * b, b FROM test2, test WHERE b = x;]]
        t.assert_equals(box.execute(sql).rows, exp)
        t.assert_equals(box.execute(sql).rows, exp)
        t.assert_equals(box.execute(sql).rows, exp)

        t.assert_equals(fiber.info()[fiber.self().id()].memory.used, exp_memory)

        -- Cleanup
        box.execute([[DROP TABLE test;]])
        box.execute([[DROP TABLE test2;]])
    end)
end
