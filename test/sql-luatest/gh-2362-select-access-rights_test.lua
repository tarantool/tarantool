local server = require('luatest.server')
local t = require('luatest')

local g = t.group("grants", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_gh_2362_select_access_rights = function(cg)
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[SET SESSION "sql_default_engine" = '%s';]], {engine})
        local nb = require('net.box')
        box.execute([[CREATE TABLE t1 (s1 INT PRIMARY KEY, s2 INT UNIQUE);]])
        box.execute([[CREATE TABLE t2 (s1 INT PRIMARY KEY);]])
        box.execute([[INSERT INTO t1 VALUES (1, 1);]])
        box.execute([[INSERT INTO t2 VALUES (1);]])

        box.schema.user.create('test_user', {password = 'test'})
        box.session.su('admin', box.schema.user.grant,
                        'test_user', 'execute', 'sql')
        box.session.su('admin', box.schema.user.grant,
                        'test_user','read', 'space', 't1')

        local c = nb.connect(box.cfg.listen,
                            {user = 'test_user', password = 'test'})
        local exp = {
            metadata = {
                {
                    name = "s1",
                    type = "integer",
                },
                {
                    name = "s2",
                    type =  "integer",
                },
            },
            rows = {{1, 1}},
        }
        local res = c:execute([[SELECT * FROM SEQSCAN t1;]])
        t.assert_equals(res, exp)

        box.schema.user.revoke('test_user','read', 'space', 't1')
        c = nb.connect(box.cfg.listen,
                      {user = 'test_user', password = 'test'})
        local mess = "Read access to space 't1' "..
                     "is denied for user 'test_user'"
        local exp_err = {
            message = mess,
        }
        t.assert_error_covers(
            exp_err,
            function()
                c:execute([[SELECT * FROM SEQSCAN t1;]])
            end
        )

        box.session.su('admin', box.schema.user.grant,
                       'test_user','read', 'space', 't2')
        c = nb.connect(box.cfg.listen,
                      {user = 'test_user', password = 'test'})
        local sql = [[SELECT * FROM SEQSCAN t1,
                      SEQSCAN t2 WHERE t1.s1 = t2.s1;]]
        t.assert_error_covers(
            exp_err,
            function()
                c:execute(sql)
            end
        )
        box.execute([[CREATE VIEW v AS SELECT * FROM SEQSCAN t1;]])

        box.session.su('admin', box.schema.user.grant,
                      'test_user', 'read', 'space', 'v')
        c = nb.connect(box.cfg.listen,
                            {user = 'test_user', password = 'test'})
        t.assert_error_covers(
            exp_err,
            function()
                c:execute([[SELECT * FROM v;]])
            end
        )

        -- Cleanup
        box.schema.user.revoke('test_user','read','space', 'v')
        box.schema.user.revoke('test_user','read','space', 't2')
        box.schema.user.revoke('test_user', 'execute', 'sql')

        box.execute([[DROP VIEW v;]])
        t.assert_equals(box.space.v, nil)
        box.execute([[DROP TABLE t2;]])
        t.assert_equals(box.space.t2, nil)
        box.execute([[DROP TABLE t1;]])
        t.assert_equals(box.space.t1, nil)
    end, {cg.params.engine})
end
