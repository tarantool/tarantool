local server = require('luatest.server')
local t = require('luatest')

local g = t.group("errinj", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

-- gh-3924 Check that tuple_formats of ephemeral spaces are
-- reused.
g.test_3924_check_tuple_format_ephemeral_space_reused = function(cg)
    cg.server:exec(function()
        local errinj = box.error.injection
        box.execute("CREATE TABLE t4 (id INTEGER PRIMARY KEY, a INTEGER);")
        box.execute("INSERT INTO t4 VALUES (1,1);")
        box.execute("INSERT INTO t4 VALUES (2,1);")
        box.execute("INSERT INTO t4 VALUES (3,2);")
        t.assert_equals(errinj.set('ERRINJ_TUPLE_FORMAT_COUNT', 200), 'ok')
        t.assert_equals(errinj.set('ERRINJ_MEMTX_DELAY_GC', true), 'ok')

        for _ = 1, 201 do box.execute("SELECT DISTINCT a FROM t4;") end

        t.assert_equals(errinj.set('ERRINJ_MEMTX_DELAY_GC', false), 'ok')
        t.assert_equals(errinj.set('ERRINJ_TUPLE_FORMAT_COUNT', -1), 'ok')

        box.execute('DROP TABLE t4;')
    end)
end

-- gh-2601 iproto messages are corrupted
g.test_2601_iproto_messages_are_corrupted = function(cg)
    cg.server:exec(function()
        local remote = require('net.box')
        local fiber = require('fiber')
        box.execute('CREATE TABLE test (ID INT PRIMARY KEY, a NUMBER, b TEXT);')
        box.schema.user.create('test_user', {password = 'test'})
        box.schema.user.grant('test_user','read,write,execute', 'universe')
        local cn = remote.connect(box.cfg.listen,
                                  {user = 'test_user', password = 'test'})
        t.assert_equals(cn:ping(), true)

        local errinj = box.error.injection
        t.assert_equals(errinj.set('ERRINJ_WAL_DELAY', true), 'ok')
        local insert_res = nil
        local select_res = nil
        local function execute_yield()
            insert_res = cn:execute("INSERT INTO test VALUES (100, 1, '1');")
        end
        local function execute_notyield()
            select_res = cn:execute('SELECT 1;')
        end
        local f1 = fiber.create(execute_yield)
        while f1:status() ~= 'suspended' do fiber.sleep(0) end
        local f2 = fiber.create(execute_notyield)
        while f2:status() ~= 'dead' do fiber.sleep(0) end
        t.assert_equals(errinj.set('ERRINJ_WAL_DELAY', false), 'ok')
        local cn = remote.connect(box.cfg.listen,
                                  {user = 'test_user', password = 'test'})

        while f1:status() ~= 'dead' do fiber.sleep(0) end
        t.assert_not_equals(insert_res, nil)
        t.assert_equals(select_res.rows, {{1}})

        cn:close()
        box.execute('DROP TABLE test;')
        box.schema.user.drop('test_user')
    end)
end

--
-- gh-3326: after the iproto start using new buffers rotation
-- policy, SQL responses could be corrupted, when DDL/DML is mixed
-- with DQL. Same as gh-3255.
--
g.test_3326_after_iproto_start_using_new_rotation = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local remote = require('net.box')

        local txn_isolation_default = box.cfg.txn_isolation
        box.cfg{txn_isolation = 'read-committed'}

        box.schema.user.create('test_user', {password = 'test'})
        box.schema.user.grant('test_user','read,write,execute', 'universe')
        box.execute('CREATE TABLE test (id INTEGER PRIMARY KEY);')

        local errinj = box.error.injection
        local ch = fiber.channel(200)
        local cn = remote.connect(box.cfg.listen,
                                  {user = 'test_user', password = 'test'})

        t.assert_equals(errinj.set("ERRINJ_IPROTO_TX_DELAY", true), 'ok')
        local func_replace = function()
            for _ = 1, 10 do
                cn:execute('REPLACE INTO test VALUES (1);')
            end
            ch:put(true)
        end
        local func_get = function()
            for _ = 1, 10 do
                cn.space.test:get{1}
            end
            ch:put(true)
        end
        for _ = 1, 100 do
            fiber.create(func_replace)
        end
        for _ = 1, 100 do
            fiber.create(func_get)
        end
        for _ = 1, 200 do
            ch:get()
        end
        t.assert_equals(errinj.set("ERRINJ_IPROTO_TX_DELAY", false), 'ok')
        t.assert_equals(box.space.test:select(), {{1}})

        cn:close()
        box.execute('DROP TABLE test;')
        box.schema.user.drop('test_user')
        box.cfg{txn_isolation = txn_isolation_default}
    end)
end

-- gh-3273: Move SQL TRIGGERs into server.
g.test_3273_move_sql_triggers_into_server = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE t1 (id INTEGER PRIMARY KEY, a INTEGER);");
        box.execute("CREATE TABLE t2 (id INTEGER PRIMARY KEY, a INTEGER);");
        t.assert_equals(box.error.injection.set('ERRINJ_WAL_IO', true), 'ok')

        local exp_err = "Failed to write to disk"
        local sql = [[CREATE TRIGGER t1t INSERT ON t1
                      FOR EACH ROW BEGIN INSERT INTO t2 VALUES (1, 1); END;]]
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute("CREATE INDEX t1a ON t1(a);")
        t.assert_equals(tostring(err), exp_err)

        t.assert_equals(box.error.injection.set('ERRINJ_WAL_IO', false), 'ok')

        box.execute(sql)
        box.execute("INSERT INTO t1 VALUES (3, 3);")
        local res = box.execute("SELECT * FROM t1;")
        t.assert_equals(res.rows, {{3, 3}})

        res = box.execute("SELECT * FROM t2;")
        t.assert_equals(res.rows, {{1, 1}})

        t.assert_equals(box.error.injection.set('ERRINJ_WAL_IO', true), 'ok')
        local t1 = box.space._trigger:get('t1t')
        local t_new = t1:totable()
        exp_err = {
            message = "Failed to write to disk",
        }
        sql = [[CREATE TRIGGER t1t INSERT ON t1
                FOR EACH ROW BEGIN INSERT INTO t2 VALUES (2, 2); END;]]
        t_new[3]['sql'] = sql
        t.assert_error_covers(exp_err, function()
                                           box.space._trigger:replace(t1, t_new)
                                        end)

        t.assert_equals(box.error.injection.set('ERRINJ_WAL_IO', false), 'ok')

        box.space._trigger:replace(t1, t_new)
        t.assert_equals(box.error.injection.set('ERRINJ_WAL_IO', true), 'ok')

        exp_err = "Failed to write to disk"
        _, err = box.execute("DROP TRIGGER t1t;")
        t.assert_equals(tostring(err), exp_err)

        t.assert_equals(box.error.injection.set('ERRINJ_WAL_IO', false), 'ok')

        box.execute("DELETE FROM t1;")
        box.execute("DELETE FROM t2;")
        box.execute("INSERT INTO t1 VALUES (3, 3);")
        res = box.execute("SELECT * FROM t1")
        t.assert_equals(res.rows, {{3, 3}})

        res = box.execute("SELECT * FROM t2")
        t.assert_equals(res.rows, {{1, 1}})

        box.execute("DROP TABLE t1;")
        box.execute("DROP TABLE t2;")
    end)
end
