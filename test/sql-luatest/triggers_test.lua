local server = require('luatest.server')
local t = require('luatest')

local g = t.group("triggers", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

--
-- ghs-163: Check access privileges when creating triggers.
--
 g.test_163_check_privileges_on_create_trigger = function(cg)
    cg.server:exec(function()
        local admin = box.session.user()
        box.execute([[CREATE TABLE t1 (i INT PRIMARY KEY);]])
        box.execute([[CREATE TABLE t2 (i INT PRIMARY KEY);]])
        box.schema.user.create('user')
        box.schema.user.grant('user', 'read,write,create', 'space', '_trigger')
        box.schema.user.grant('user', 'read', 'space', '_space')
        box.session.su('user')
        local exp_err = "Alter access to space 't1' is denied for user 'user'"
        local sql = "CREATE TRIGGER t1t AFTER INSERT ON t1 "..
                    "FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;"

        local _, err = box.execute(sql)
        t.assert_equals(err.message, exp_err)

        box.session.su(admin)
        box.schema.user.grant('user', 'read,write,create', 'space', 't1')
        box.session.su('user')
        _, err = box.execute(sql)
        t.assert_equals(err.message, exp_err)

        box.session.su(admin)
        box.schema.user.grant('user', 'alter', 'space', 't1')
        box.session.su('user')
        local res, err = box.execute(sql)
        t.assert_equals(res, {row_count = 1})
        t.assert_equals(err, nil)

        box.session.su(admin)
        box.schema.user.drop('user')
        box.execute([[DROP TRIGGER t1t;]])
        box.execute([[DROP TABLE t1;]])
        box.execute([[DROP TABLE t2;]])
    end)
end
