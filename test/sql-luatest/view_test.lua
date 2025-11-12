local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end)
end)

g.after_all(function()
    g.server:stop()
end)

--
-- Make sure, that it is possible to create a VIEW which
-- refers to "_v" space, i.e. to sysview engine.
--
g.test_4111_format_in_sysview = function()
    g.server:exec(function()

        t.assert(box.space._vspace.index[1]:count(1) > 0)

        box.execute([[CREATE VIEW v AS SELECT name, engine FROM _vspace;]])
        t.assert(box.space.v ~= nil)
        local exp = {
            metadata = {
                {name = "name", type = "string"},
                {name = "engine", type = "string"}
            },
            rows = {{'v', 'memtx'}},
        }
        local res = box.execute([[SELECT * FROM v WHERE name = 'v';]])
        t.assert_equals(res, exp)
        box.execute([[DROP VIEW v;]])
    end)
end

--
-- Make sure, that user can't access to protected tables via VIEW
--
g.test_4104_view_access_check = function()
    g.server:exec(function()
        box.execute("CREATE TABLE supersecret(id INT PRIMARY KEY, data TEXT);")
        t.assert(box.space.supersecret ~= nil)
        box.execute("CREATE TABLE supersecret2(id INT PRIMARY KEY, data TEXT);")
        t.assert(box.space.supersecret2 ~= nil)
        box.execute("INSERT INTO supersecret VALUES(1, 'very very big secret');")
        box.execute("INSERT INTO supersecret2 VALUES(1, 'very big secret 2');")
        box.execute("CREATE VIEW leak AS SELECT * FROM supersecret, supersecret2;")
        t.assert(box.space.leak ~= nil)

        box.schema.user.create('test_user', {password = 'test'})

        box.session.su('admin', box.schema.user.grant, 'test_user', 'execute', 'sql')

        remote = require 'net.box'
        local listen = require('uri').parse(box.cfg.listen)
        local cn = remote.connect(listen.host, listen.service, {user = 'test_user', password = 'test'})
        cn:execute([[SET SESSION "sql_seq_scan" = true;]])

        box.session.su('admin', box.schema.user.grant, 'test_user', 'read', 'space', 'leak')

        local _, err = pcall(function ()
            return cn:execute('SELECT * FROM leak;')
        end)
        t.assert_equals(err.message, "Read access to space 'supersecret' is denied for user 'test_user'")

        box.session.su('admin', box.schema.user.grant, 'test_user', 'read', 'space', 'supersecret')

        _, err = pcall(function ()
            return cn:execute('SELECT * FROM leak;')
        end)
        t.assert_equals(err.message, "Read access to space 'supersecret2' is denied for user 'test_user'")

        box.schema.user.revoke('test_user','read', 'space', 'supersecret')
        box.schema.user.revoke('test_user','read', 'space', 'leak')
        box.schema.user.revoke('test_user', 'execute', 'sql')
        box.schema.user.drop('test_user')
        box.execute("DROP VIEW leak;")
        box.execute("DROP TABLE supersecret")
        box.execute("DROP TABLE supersecret2")
    end)
end
