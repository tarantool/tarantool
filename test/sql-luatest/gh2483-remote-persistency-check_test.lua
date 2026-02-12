local server = require('luatest.server')
local t = require('luatest')

local g = t.group("restart", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[SET SESSION "sql_default_engine" = '%s';]], {engine})
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_2483_remote_persistency_check = function(cg)
    cg.server:exec(function()
        box.session.su('admin', box.schema.user.grant, 'guest',
                       'read,write,create', 'universe')
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
        local c = require('net.box').connect(box.cfg.listen)
        -- This segfaults due to gh-2483 since
        -- before the patch sql schema was read on-demand.
        -- Which could obviously lead to access denied error.
        local res = c:eval([[return box.execute('SELECT * FROM SEQSCAN t');]])
        t.assert_equals(res.rows, {{1}})

        box.execute([[DROP TABLE t;]])
        box.session.su('admin', box.schema.user.revoke, 'guest',
                       'read,write,create', 'universe')
    end)
end
