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

--This test ensures that there is
--no access denied error after restarting the server.
g.test_2483_remote_persistency_check = function(cg)
    cg.server:exec(function()
        box.schema.user.create('new_user', {password = 'test'})
        box.schema.user.grant('new_user', 'read,write,create,execute', 'universe')
        -- Create a table and insert a datum
        box.execute([[CREATE TABLE t(id int PRIMARY KEY);]])
        box.execute([[INSERT INTO t (id) VALUES (1);]])

        -- Sanity check
        local res = box.execute([[SELECT * FROM SEQSCAN t;]])
        t.assert_equals(res.rows, {{1}})
    end)

    cg.server:restart()
    cg.server:exec(function()
        -- Connect to ourself
        local c = require('net.box').connect(box.cfg.listen, {
                                                user = 'new_user',
                                                password = 'test',
                                             })

        local res, err = c:eval([[return box.execute('SELECT * FROM SEQSCAN t');]])
        t.assert_equals(err, nil)
        t.assert_equals(res.rows, {{1}})

        box.execute([[DROP TABLE t;]])
        box.schema.user.revoke('new_user', 'read,write,create,execute', 'universe')
        box.schema.user.drop('new_user')
    end)
end

--This test checks whether the unique index is restored correctly
--after the restart server.
g.test_gh2828_inline_unique_presistency_check = function(cg)
    cg.server:exec(function()
        -- Create a table and insert a datum
        box.execute([[CREATE TABLE t1(a INT PRIMARY KEY, b INT, UNIQUE(b));]])
        box.execute([[INSERT INTO t1 VALUES(1,2);]])

        -- Sanity check
        local exp = {
            metadata = {
                {
                    name = "a",
                    type = "integer",
                },
                {
                    name = "b",
                    type = "integer",
                },
            },
            rows = {{1, 2}},
        }
        local res = box.execute([[SELECT * FROM SEQSCAN t1;]])
        t.assert_equals(res, exp)
    end)

    cg.server:restart()
    cg.server:exec(function()
        box.execute([[INSERT INTO t1 VALUES(2,3);]])

        -- Sanity check
        local exp = {
            metadata = {
                {
                    name = "a",
                    type = "integer",
                },
                {
                    name = "b",
                    type = "integer",
                },
            },
            rows = {{1, 2}, {2, 3}},
        }
        local res = box.execute([[SELECT * FROM SEQSCAN t1;]])
        t.assert_equals(res, exp)

        -- Cleanup
        box.execute([[DROP TABLE t1;]])
        t.assert_equals(box.space.t1, nil)
    end)
end