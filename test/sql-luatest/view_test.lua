local server = require('luatest.server')
local t = require('luatest')

local g = t.group("view", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_view = function(cg)
    cg.server:exec(function()
        -- Create space and view.
        box.execute("CREATE TABLE t1(a INT, b INT, PRIMARY KEY(a, b));")
        t.assert(box.space.t1 ~= nil)
        box.execute("CREATE VIEW v1 AS SELECT a+b FROM t1;")
        t.assert(box.space.v1 ~= nil)

        -- View can't have any indexes.
        local _, err = box.execute("CREATE INDEX i1 ON v1(a);")
        local exp_err = "Can't create or modify index 'i1' "..
                        "in space 'v1': views can not be indexed"
        t.assert_equals(err.message, exp_err)
        local v1 = box.space.v1
        local opts = {parts = {1, 'string'}}
        exp_err = "Can't modify space 'v1': can not add index on a view"
        t.assert_error_msg_equals(exp_err, v1.create_index, v1, 'primary', opts)
        t.assert_error_msg_equals(exp_err, v1.create_index, v1,
                                  'secondary', opts)

        -- View option can't be changed.
        local _space = box.space._space
        v1 = _space.index[2]:select('v1')[1]:totable()
        v1[6]['view'] = false
        exp_err = "Can't modify space 'v1': view can not be altered"
        t.assert_error_msg_equals(exp_err, _space.replace, _space, v1)

        local t1 = _space.index[2]:select('t1')[1]:totable()
        t1[6]['view'] = true
        t1[6]['sql'] = 'SELECT * FROM t1;'
        exp_err = "Can't modify space 't1': "..
                  "can not convert a space to a view and vice versa"
        t.assert_error_msg_equals(exp_err, _space.replace, _space, t1)

        -- View can't exist without SQL statement.
        v1[6] = {}
        v1[6]['view'] = true
        exp_err = "Can't modify space 'v1': view can not be altered"
        t.assert_error_msg_equals(exp_err, _space.replace, _space, v1)

        -- Views can't be created via create_space().
        exp_err = "unexpected option 'view'"
        t.assert_error_msg_equals(exp_err, box.schema.create_space,
                                  'view', {view = true})

        -- Space referenced by a view can't be renamed.
        _, err = box.execute("ALTER TABLE t1 RENAME TO new_name;")
        exp_err = "Can't modify space 't1': can not rename "..
                  "space which is referenced by view"
        t.assert_equals(err.message, exp_err)

        -- View can be created via straight insertion into _space.
        local sp = box.schema.create_space('test')
        local raw_sp = _space:get(sp.id):totable()
        sp:drop()
        raw_sp[6].sql = 'CREATE VIEW v as SELECT * FROM t1;'
        raw_sp[6].view = true
        sp = _space:replace(raw_sp)
        local res = _space:select(sp['id'])[1]['name']
        t.assert_equals(res, "test")

        -- Can't create view with incorrect SELECT statement.
        box.space.test:drop()
        -- This case must fail since parser converts it to expr AST.
        raw_sp[6].sql = 'SELECT 1;'
        exp_err = "Failed to execute SQL statement: SELECT 1;"
        t.assert_error_msg_equals(exp_err, _space.replace, _space, raw_sp)

        box.execute("DROP VIEW v1;")
        box.execute("DROP TABLE t1;")

        -- Can't drop space via Lua if at least one view refers to it.
        box.execute('CREATE TABLE t1(id INT PRIMARY KEY);')
        t.assert(box.space.t1 ~= nil)
        box.execute('CREATE VIEW v1 AS SELECT * FROM t1;')
        t.assert(box.space.v1 ~= nil)
        exp_err = "Can't drop space 't1': other views depend on this space"
        t.assert_error_msg_equals(exp_err, box.space.t1.drop, box.space.t1)
        box.execute('DROP VIEW v1;')
        box.execute('DROP TABLE t1;')

        -- Check that alter transfers reference counter.
        box.execute("CREATE TABLE t1(id INTEGER PRIMARY KEY);")
        t.assert(box.space.t1 ~= nil)
        box.execute("CREATE VIEW v1 AS SELECT * FROM t1;")
        t.assert(box.space.v1 ~= nil)
        _, err = box.execute("DROP TABLE t1;")
        exp_err = "Can't drop space 't1': other views depend on this space"
        t.assert_equals(err.message, exp_err)
        sp = _space:get{box.space.t1.id}
        _space:replace(sp)
        _, err = box.execute("DROP TABLE t1;")
        exp_err = "Can't drop space 't1': other views depend on this space"
        t.assert_equals(err.message, exp_err)
        box.execute("DROP VIEW v1;")
        box.execute("DROP TABLE t1;")

        -- Try to create invalid view using direct insert to space _space.
        box.execute("CREATE TABLE b (s1 INT PRIMARY KEY);")
        t.assert(box.space.b ~= nil)
        box.execute("CREATE TABLE c (s1 INT PRIMARY KEY);")
        t.assert(box.space.c ~= nil)
        box.execute([[CREATE VIEW bcv(x, y) AS VALUES((SELECT 'k' FROM b),
                      (VALUES((SELECT 1 FROM b WHERE s1 IN
                      (VALUES((SELECT 1 + c.s1 FROM c)))))))]])
        t.assert(box.space.bcv ~= nil)
        local space_tuple = _space.index[0]:max():totable()
        space_tuple[1] = space_tuple[1] + 1 -- id
        space_tuple[3] = space_tuple[3] .. '1' -- name
        space_tuple[6].sql = string.gsub(space_tuple[6].sql, 'FROM c',
                                                             'FROM ccc')
        exp_err = "Space 'ccc' does not exist"
        t.assert_error_msg_equals(exp_err, _space.insert, _space, space_tuple)
        box.space.bcv:drop()
        box.execute("DROP TABLE c;")
        box.execute("DROP TABLE b;")

        -- Make sure we can't alter a view.
        box.execute("CREATE TABLE t1 (a INT PRIMARY KEY);")
        t.assert(box.space.t1 ~= nil)
        box.execute("CREATE VIEW v AS SELECT * FROM t1;")
        t.assert(box.space.v ~= nil)

        -- Try to change owner.
        local view = _space.index[2]:select('v')[1]:totable()
        view[2] = 1
        exp_err = "Can't modify space 'v': view can not be altered"
        t.assert_error_msg_equals(exp_err, _space.replace, _space, view)

        -- Try to rename.
        view = _space.index[2]:select('v')[1]:totable()
        view[3] = 'a'
        exp_err = "Can't modify space 'v': view can not be altered"
        t.assert_error_msg_equals(exp_err, _space.replace, _space, view)

        -- Try to change engine.
        view = _space.index[2]:select('v')[1]:totable()
        view[4] = 'a'
        exp_err = "Can't modify space 'v': view can not be altered"
        t.assert_error_msg_equals(exp_err, _space.replace, _space, view)

        -- Try to add a field.
        view = _space.index[2]:select('v')[1]:totable()
        local view_format = box.space.v:format()
        local field = {
            type = 'string',
            nullable_action = 'none',
            name = 'B',
            is_nullable = true,
        }
        table.insert(view_format, field)
        view[5] = 2
        view[7] = view_format
        exp_err = "Can't modify space 'v': view can not be altered"
        t.assert_error_msg_equals(exp_err, _space.replace, _space, view)

        -- Try to modify format only.
        view = box.space.v
        exp_err = "Can't modify space 'v': view can not be altered"
        t.assert_error_msg_equals(exp_err, view.format, view, {})

        view_format = box.space.v:format()
        view_format[1].name = 'B'
        exp_err = "Can't modify space 'v': view can not be altered"
        t.assert_error_msg_equals(exp_err, view.format, view, view_format)

        box.execute("DROP VIEW v;")
        box.execute("DROP TABLE t1;")
    end)
end

--
-- Make sure, that it is possible to create a VIEW which
-- refers to "_v" space, i.e. to sysview engine.
--
g.test_4111_format_in_sysview = function(cg)
    cg.server:exec(function(engine)

        t.assert(box.space._vspace.index[1]:count(1) > 0)

        box.execute([[CREATE VIEW v AS SELECT name, engine FROM _vspace;]])
        t.assert(box.space.v ~= nil)
        local exp = {
            metadata = {
                {name = "name", type = "string"},
                {name = "engine", type = "string"},
            },
            rows = {{'v', engine}},
        }
        local res = box.execute([[SELECT * FROM v WHERE name = 'v';]])
        t.assert_equals(res, exp)
        box.execute([[DROP VIEW v;]])
    end, {cg.params.engine})
end

--
-- Make sure, that user can't access to protected tables via VIEW
--
g.test_4104_view_access_check = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE supersecret(id INT PRIMARY KEY, data TEXT);")
        t.assert(box.space.supersecret ~= nil)
        box.execute([[CREATE TABLE supersecret2(
                      id_1 INT PRIMARY KEY, data_1 TEXT);]])
        t.assert(box.space.supersecret2 ~= nil)
        box.execute([[INSERT INTO supersecret VALUES(1, 'very big secret');]])
        box.execute("INSERT INTO supersecret2 VALUES(1, 'very big secret 2');")
        box.execute([[CREATE VIEW leak AS
                      SELECT * FROM supersecret, supersecret2;]])
        t.assert(box.space.leak ~= nil)

        box.schema.user.create('test_user', {password = 'test'})

        box.session.su('admin', box.schema.user.grant,
                       'test_user', 'execute', 'sql')

        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen,
                                  {user = 'test_user', password = 'test'})
        cn:execute([[SET SESSION "sql_seq_scan" = true;]])

        box.session.su('admin', box.schema.user.grant,
                       'test_user', 'read', 'space', 'leak')

        local exp_err = "Read access to space 'supersecret' "..
                        "is denied for user 'test_user'"
        local sql = 'SELECT * FROM leak;'
        t.assert_error_msg_equals(exp_err, cn.execute, cn, sql)

        box.session.su('admin', box.schema.user.grant, 'test_user',
                       'read', 'space', 'supersecret')

        exp_err = "Read access to space 'supersecret2' "..
                  "is denied for user 'test_user'"
        sql = 'SELECT * FROM leak;'
        t.assert_error_msg_equals(exp_err, cn.execute, cn, sql)

        box.schema.user.revoke('test_user','read', 'space', 'supersecret')
        box.schema.user.revoke('test_user','read', 'space', 'leak')
        box.schema.user.revoke('test_user', 'execute', 'sql')
        box.schema.user.drop('test_user')
        box.execute("DROP VIEW leak;")
        box.execute("DROP TABLE supersecret")
        box.execute("DROP TABLE supersecret2")
    end)
end

--
-- gh-3849: failed to create VIEW in form of AS VALUES (const);
--
g.test_3849_creating_view_in_form_as_values_check = function(cg)
    cg.server:exec(function()
        box.execute("CREATE VIEW cv AS VALUES(1);")
        t.assert(box.space.cv ~= nil)
        box.execute("CREATE VIEW cv1 AS VALUES('k', 1);")
        t.assert(box.space.cv1 ~= nil)
        box.execute("CREATE VIEW cv2 AS VALUES((VALUES((SELECT 1))));")
        t.assert(box.space.cv2 ~= nil)
        box.execute("CREATE VIEW cv3 AS VALUES(1+2, 1+2);")
        t.assert(box.space.cv3 ~= nil)
        box.execute("DROP VIEW cv;")
        box.execute("DROP VIEW cv1;")
        box.execute("DROP VIEW cv2;")
        box.execute("DROP VIEW cv3;")
    end)
end

--
-- gh-3815: AS VALUES syntax didn't increment VIEW reference
-- counter. Moreover, tables within sub-select were not accounted
-- as well.
--
g.test_3815_as_values_check = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE b (s1 INT PRIMARY KEY);")
        t.assert(box.space.b ~= nil)
        box.execute("CREATE VIEW bv (wombat) AS VALUES ((SELECT 'k' FROM b));")
        t.assert(box.space.bv ~= nil)
        local _, err = box.execute("DROP TABLE b;")
        local exp_err = "Can't drop space 'b': other views depend on this space"
        t.assert_equals(err.message, exp_err)
        box.execute("DROP VIEW bv;")
        box.execute("DROP TABLE b;")

        box.execute("CREATE TABLE b (s1 INT PRIMARY KEY);")
        t.assert(box.space.b ~= nil)
        box.execute("CREATE TABLE c (s1 INT PRIMARY KEY);")
        t.assert(box.space.c ~= nil)
        box.execute([[CREATE VIEW bcv AS SELECT * FROM b
                      WHERE s1 IN (SELECT * FROM c);]])
        t.assert(box.space.bcv ~= nil)
        _, err = box.execute("DROP TABLE c;")
        exp_err = "Can't drop space 'c': other views depend on this space"
        t.assert_equals(err.message, exp_err)
        box.execute("DROP VIEW bcv;")
        box.execute("DROP TABLE c;")

        box.execute("CREATE TABLE c (s1 INT PRIMARY KEY);")
        t.assert(box.space.c ~= nil)
        box.execute([[CREATE VIEW bcv(x, y) AS VALUES((SELECT 'k' FROM b),
                      (VALUES((SELECT 1 FROM b WHERE s1 IN
                      (VALUES((SELECT 1 + c.s1 FROM c)))))));]])
        t.assert(box.space.bcv ~= nil)
        _, err = box.execute("DROP TABLE c;")
        exp_err = "Can't drop space 'c': other views depend on this space"
        t.assert_equals(err.message, exp_err)
        box.space.bcv:drop()
        box.execute("DROP TABLE c;")
        box.execute("DROP TABLE b;")
    end)
end

--
-- gh-3814: make sure that recovery of view processed without
-- unexpected errors.
--
g.test_3814_view_recovery_check = function(cg)
    cg.server:exec(function()
        box.snapshot()
        box.execute("CREATE TABLE t1 (id INT PRIMARY KEY);")
        t.assert(box.space.t1 ~= nil)
        box.execute("CREATE VIEW v1 AS SELECT * FROM t1;")
        t.assert(box.space.v1 ~= nil)
    end)
    cg.server:restart()
    cg.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local _, err = box.execute("DROP TABLE t1;")
        local exp_err = "Can't drop space 't1': "..
                        "other views depend on this space"
        t.assert_equals(err.message, exp_err)
        local exp = {
            metadata = {
                {name = "id", type = "integer"},
            },
            rows = {},
        }
        local res = box.execute("SELECT * FROM v1;")
        t.assert_equals(exp, res)
        box.space.v1:drop()
        box.space.t1:drop()
    end)
end

--
-- gh-4740: make sure INSTEAD OF DELETE and INSTEAD OF UPDATE
-- triggers work for each row of view.
--
g.test_4740_instead_of_delete_update_check = function(cg)
    cg.server:exec(function()
        box.execute('CREATE TABLE t (i INT PRIMARY KEY AUTOINCREMENT, a INT);')
        t.assert(box.space.t ~= nil)
        box.execute('CREATE TABLE t1 (i INT PRIMARY KEY AUTOINCREMENT, a INT);')
        t.assert(box.space.t1 ~= nil)
        box.execute('CREATE VIEW v AS SELECT a FROM t;')
        t.assert(box.space.v ~= nil)
        box.execute([[CREATE TRIGGER r1 INSTEAD OF DELETE ON v
                      FOR EACH ROW BEGIN INSERT INTO t1 VALUES (NULL, 1);
                      END;]])
        box.execute([[CREATE TRIGGER r2 INSTEAD OF UPDATE ON v
                      FOR EACH ROW BEGIN INSERT INTO t1 VALUES (NULL, 2);
                      END;]])
        local exp = {
            autoincrement_ids = {1, 2, 3, 4, 5, 6},
            row_count = 6,
        }
        local res = box.execute([[INSERT INTO t VALUES (NULL, 1), (NULL, 1),
                                  (NULL, 1), (NULL, 2), (NULL, 3), (NULL, 3);]])
        t.assert_equals(res, exp)
        exp = {
            row_count = 0,
        }
        res = box.execute('DELETE FROM v;')
        t.assert_equals(res, exp)
        res = box.execute('UPDATE v SET a = 10;')
        t.assert_equals(res, exp)
        exp = {
            metadata = {
                {name = "i", type = "integer"},
                {name = "a", type = "integer"},
            },
            rows = {
                {1, 1},
                {2, 1},
                {3, 1},
                {4, 1},
                {5, 1},
                {6, 1},
                {7, 2},
                {8, 2},
                {9, 2},
                {10, 2},
                {11, 2},
                {12, 2},
            },
        }
        res = box.execute('SELECT * FROM t1;')
        t.assert_equals(res, exp)
        box.execute('DROP VIEW v;')
        box.execute('DROP TABLE t;')
        box.execute('DROP TABLE t1;')
    end)
end

--
-- gh-4545: Make sure that creating a view with columns that have
-- the same name will result in an error.
--
g.test_duplicate_column_error = function(cg)
    cg.server:exec(function()
        local exp_err = [[Space field 'a' is duplicate]]
        local sql = [[CREATE TABLE t (a INTEGER PRIMARY KEY, a INTEGER);]]
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        sql = [[CREATE VIEW v AS SELECT 1 AS a, 1 AS a;]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)
    end)
end
