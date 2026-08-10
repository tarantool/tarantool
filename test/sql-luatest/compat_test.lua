local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_old_behavior_init = function()
    local s = server:new({
        alias = 'old_behavior',
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG = [[
                require('compat').sql_seq_scan_default = 'old'
            ]]
        }
    })
    s:start()
    s:exec(function()
        local res = box.execute([[SELECT * FROM "_vspace";]])
        t.assert(res.rows ~= nil)
    end)
    s:stop()
end

g.test_new_behavior_init = function()
    local s = server:new({
        alias = 'new_behavior',
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG = [[
                require('compat').sql_seq_scan_default = 'new'
            ]]
        }
    })
    s:start()
    s:exec(function()
        local _, err = box.execute([[SELECT * FROM "_vspace";]])
        t.assert_equals(err.message, "Scanning is not allowed for '_vspace'")
    end)
    s:stop()
end

g.test_new_sessions = function()
    g.server:exec(function()
        local func = function()
            return box.execute([[select * from "_vspace";]])
        end
        local fiber = require('fiber')

        require('compat').sql_seq_scan_default = 'new'
        local f = fiber.new(func)
        f:set_joinable(true)
        local _, res, err = f:join()
        t.assert(res == nil)
        t.assert_equals(err.message, "Scanning is not allowed for '_vspace'")

        require('compat').sql_seq_scan_default = 'old'
        f = fiber.new(func)
        f:set_joinable(true)
        _, res, err = f:join()
        t.assert(res ~= nil)
        t.assert(err == nil)
    end)
end

g = t.group("compat", {{engine = 'memtx'}, {engine = 'vinyl'}})

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
-- Check old and new option with sql_uppercase_id in compat
g.test_option_sql_uppercase_id = function(cg)
    cg.server:exec(function()
        box.schema.func.create('FOO', {
            language = 'LUA',
            body = [[
                function(val)
                    if val ~= nil and val > 1 then
                        return val
                    else
                        return nil
                    end
                end
            ]],
            param_list = {'number'},
            returns = 'number',
            exports = {'LUA', 'SQL'},
        })

        box.execute([[CREATE TABLE ASD (I INT PRIMARY KEY);]])

        box.space.ASD:insert{1}
        box.space.ASD:insert{2}
        box.space.ASD:insert{3}

        box.execute([[CREATE VIEW BSD as SELECT * FROM ASD;]])

        local res = box.execute([[SELECT * FROM aSd;]])
        t.assert_equals(res.rows, {{1}, {2}, {3}})

        res = box.execute([[SELECT i FROM ASD;]])
        t.assert_equals(res.rows, {{1}, {2}, {3}})

        res = box.execute([[SELECT * FROM bSd;]])
        t.assert_equals(res.rows, {{1}, {2}, {3}})

        local sql = [[CREATE TABLE T (id INT PRIMARY KEY, FOREIGN KEY(id)
                      REFERENCES t(id));]]
        local _, err = box.execute(sql)
        t.assert_equals(err, nil)

        res = box.execute([[SELECT foo(I) FROM ASD;]])
        t.assert_equals(res.rows, {{nil}, {2}, {3}})

        _, err = box.execute([[INSERT INTO ASD (i) VALUES (4);]])
        t.assert_equals(err, nil)

        sql = [[CREATE TABLE test(id INT PRIMARY KEY, A INT, b text);]]
        box.execute(sql)
        box.execute([[CREATE INDEX IDX ON test(A);]])
        box.space.test:insert{2, 2, 'a'}
        box.space.test:insert{1, 1, 'b'}

        res = box.execute([[SELECT * FROM test INDEXED BY idx WHERE A > 1;]])
        t.assert_equals(res.rows, {{2, 2, 'a'}})

        res = box.execute([[SELECT * FROM test ORDER BY a;]])
        t.assert_equals(res.rows, {{1, 1, 'b'}, {2, 2, 'a'}})

        box.execute([[START TRANSACTION;]])
        box.execute([[SAVEPOINT SP;]])
        _, err = box.execute([[ROLLBACK TO sp;]])
        t.assert_equals(err, nil)

        box.execute([[DROP TABLE T;]])

        local compat = require('compat')
        compat.sql_uppercase_id = 'new'

        local exp_err = [[Space 'aSd' does not exist]]
        _, err = box.execute([[SELECT * FROM aSd;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Can't resolve field 'i']]
        _, err = box.execute([[SELECT i FROM ASD;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Space 'bSd' does not exist]]
        local _, err = box.execute([[SELECT * FROM bSd;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Space 't' does not exist]]
        sql = [[CREATE TABLE T (id INT PRIMARY KEY, FOREIGN KEY(id)
                REFERENCES t(id));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Function 'foo' does not exist]]
        _, err = box.execute([[SELECT foo(I) FROM ASD;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Field 'i' was not found in space 'ASD' format]]
        _, err = box.execute([[INSERT INTO ASD (i) VALUES (5);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Can not rollback to savepoint: "..
                  "the savepoint does not exist"
        _, err = box.execute([[ROLLBACK TO sp;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[No index 'idx' is defined in space 'test']]
        _, err = box.execute([[SELECT * FROM test INDEXED BY idx WHERE A > 1;]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Can't resolve field 'a']]
        _, err = box.execute([[SELECT * FROM test ORDER BY a;]])
        t.assert_equals(tostring(err), exp_err)

        box.execute([[CREATE TABLE Bst (i INT PRIMARY KEY);]])
        box.execute([[CREATE TABLE BST (i INT PRIMARY KEY,
                      a INT REFERENCES Bst(i));]])
        local foreign_key = box.space.BST:format()[2]['foreign_key']
        t.assert_equals(foreign_key['fk_unnamed_BST_a_1']['space'],
                        box.space.Bst.id)

        box.execute("DROP INDEX idx ON test;")
        box.execute([[DROP TABLE ASD;]])
        box.execute([[DROP TABLE T;]])
        box.execute([[DROP TABLE test;]])
        box.execute([[DROP VIEW BSD;]])
        box.execute([[COMMIT;]])
        box.execute([[DROP TABLE BSD;]])
        box.execute([[DROP TABLE Bsd;]])
    end)
end
