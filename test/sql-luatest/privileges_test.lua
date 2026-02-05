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

g.test_2362_select_access_rights = function(cg)
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_default_engine" = '%s';]], {engine})
        local nb = require('net.box')
        box.execute([[CREATE TABLE t1 (s1 INT PRIMARY KEY, s2 INT UNIQUE);]])
        box.execute([[CREATE TABLE t2 (s1 INT PRIMARY KEY);]])
        box.execute([[INSERT INTO t1 VALUES (1, 1);]])
        box.execute([[INSERT INTO t2 VALUES (1);]])

        box.schema.user.create('test_user', {password = 'test'})
        box.schema.user.grant('test_user', 'execute', 'sql')
        box.schema.user.grant('test_user', 'read', 'space', 't1')

        local c = nb.connect(box.cfg.listen, {user = 'test_user',
                                              password = 'test'})
        t.assert_equals(c:execute([[SELECT * FROM SEQSCAN t1;]]).rows, {{1, 1}})

        box.schema.user.revoke('test_user','read', 'space', 't1')
        c = nb.connect(box.cfg.listen, {user = 'test_user', password = 'test'})
        local mess = "Read access to space 't1' is "..
                     "denied for user 'test_user'"
        local exp_err = {
            message = mess,
        }
        local sql = [[SELECT * FROM SEQSCAN t1;]]
        t.assert_error_covers(exp_err, c.execute, c, sql)

        box.schema.user.grant('test_user','read', 'space', 't2')
        c = nb.connect(box.cfg.listen, {user = 'test_user', password = 'test'})
        local sql = [[SELECT * FROM SEQSCAN t1,
                      SEQSCAN t2 WHERE t1.s1 = t2.s1;]]
        t.assert_error_covers(exp_err, c.execute, c, sql)
        box.execute([[CREATE VIEW v AS SELECT * FROM SEQSCAN t1;]])

        box.schema.user.grant('test_user', 'read', 'space', 'v')
        c = nb.connect(box.cfg.listen, {user = 'test_user', password = 'test'})
        local sql = [[SELECT * FROM v;]]
        t.assert_error_covers(exp_err, c.execute, c, sql)

        -- Cleanup
        box.schema.user.revoke('test_user', 'read', 'space', 'v')
        box.schema.user.revoke('test_user', 'read', 'space', 't2')
        box.schema.user.revoke('test_user', 'execute', 'sql')

        box.execute([[DROP VIEW v;]])
        box.execute([[DROP TABLE t2;]])
        box.execute([[DROP TABLE t1;]])
    end, {cg.params.engine})
end

--
-- gh-4546: DROP TABLE must delete all privileges given on that
-- table to any user.
--
g.test_gh_4546_sql_drop_grants = function(cg)
    cg.server:exec(function(engine)
        box.schema.user.create('test_user1')
        box.schema.user.create('test_user2')
        local test_user1_id = box.space._user.index.name:get({'test_user1'}).id
        local test_user2_id = box.space._user.index.name:get({'test_user2'}).id
        local s = box.schema.create_space('T', {
            engine = engine,
            format = {{'i', 'integer'}}
        })

        box.schema.user.grant('test_user1', 'read', 'space', 'T')
        box.schema.user.grant('test_user2', 'write', 'space', 'T')
        t.assert(box.space._priv:get({test_user1_id, 'space', s.id}) ~= nil)
        t.assert(box.space._priv:get({test_user2_id, 'space', s.id}) ~= nil)

        box.execute([[DROP TABLE T;]])
        t.assert_equals(box.space.T, nil)

        t.assert(box.space._priv:get({test_user1_id, 'space', s.id}) == nil)
        t.assert(box.space._priv:get({test_user2_id, 'space', s.id}) == nil)
        box.schema.user.drop('test_user1')
        box.schema.user.drop('test_user2')
    end, {cg.params.engine})
end
